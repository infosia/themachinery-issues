#define CGLTF_IMPLEMENTATION
#define CGLTF_VRM_v0_0_IMPLEMENTATION

#include <cstdint>
#include <chrono>
#include <thread>
#include <mutex>
#include <unordered_map>

#include "motionclient.h"
#include <foundation/math.inl>

#include "osc/OscPacketListener.h"
#include "ip/UdpSocket.h"
#include "osc/OscOutboundPacketStream.h"
#include "cgltf/cgltf.h"
#include "cgltf_func.inl"

static std::mutex motionclient_lock_guard;
static struct tm_logger_api* tm_logger_api = nullptr;
static struct tm_string_repository_i* tm_string_repository = nullptr;

class VmcPacketListener : public osc::OscPacketListener {
public:
	VmcPacketListener(const vmc_options& options) : osc::OscPacketListener()
		, options(options)
		, vrmdata(nullptr)
		, state{ false, false }
		, humanoid_mapping{}
		, transform_data{ 0, nullptr, nullptr, nullptr }
	{
		TM_LOG("[INFO] VmcPacketListener created");
	}

	virtual ~VmcPacketListener()
	{
		TM_LOG("[INFO] VmcPacketListener cleaning up");
		for (const auto& iter : hash_to_index_map) {
			tm_string_repository->remove(tm_string_repository->inst, iter.first);
		}
		TM_LOG("[INFO] VmcPacketListener destroyed");
	}

	virtual void ProcessMessage(const osc::ReceivedMessage& m,
		const IpEndpointName& remoteEndpoint) override
	{
		try {
			auto arg = m.ArgumentsBegin();
			const auto address = m.AddressPattern();
			if (std::strcmp(address, "/VMC/PING") == 0) {
				return;
			}
			else if (!state.loaded && std::strcmp(address, "/VMC/Ext/OK") == 0 && arg->IsInt32()) {
				const auto loaded = (arg++)->AsInt32Unchecked();
				const auto calibrated = (arg++)->AsInt32Unchecked();
				if (loaded == 1 && calibrated == 3) {
					state.loaded = true;
				}
			}
			else if (!state.received && state.loaded && std::strcmp(address, "/VMC/Ext/VRM") == 0) {
				// Collect bone information. This should be done only once.
				if (arg->IsString() && !state.received) {
					const auto value = (arg++)->AsStringUnchecked();
					if (strlen(value) > 0) {

						cgltf_options parse_options = {};
						parse_options.file.read = &vrm_file_read;

						if (vrmdata != nullptr) {
							cgltf_free(vrmdata);
						}

						const auto result = cgltf_parse_file(&parse_options, value, &vrmdata);

						if (result == cgltf_result_success) {

							// Constructs humanoid-bone => node mapping 
							humanoid_mapping = vrm_get_humanoid_mapping(vrmdata);
							std::lock_guard<std::mutex> lock(motionclient_lock_guard);
							transform_data.availableCount = 0;

							assert(vrmdata->vrm_v0_0.humanoid.humanBones_count < 255);

							uint8_t stored_index = 0;
							cgltf_size rootbone_index;
							const auto rootbone_found = vrm_get_root_bone(vrmdata, options.rootbone, &rootbone_index);

							// initialize hashes
							const auto availableCount = static_cast<uint8_t>(vrmdata->vrm_v0_0.humanoid.humanBones_count + (rootbone_found ? 1 : 0));
							transform_data.hashes = new uint64_t[availableCount];
							transform_data.rotations = new tm_vec4_t[availableCount];
							transform_data.translations = new tm_vec3_t[availableCount];

							if (rootbone_found) {
								const auto rootnode = vrmdata->nodes[rootbone_index];

								const auto rootbone_hash = tm_string_repository->add(tm_string_repository->inst, options.rootbone.c_str());
								hash_map.emplace(options.rootbone, rootbone_hash);
								hash_to_index_map.emplace(rootbone_hash, stored_index);
								transform_data.hashes[stored_index] = rootbone_hash;
								transform_data.rotations[stored_index] = { rootnode.rotation[0], rootnode.rotation[1], rootnode.rotation[2], rootnode.rotation[3] };
								transform_data.translations[stored_index] = { rootnode.translation[0], rootnode.translation[1], rootnode.translation[2] };
								stored_index++;
							}

							for (cgltf_size i = 0; i < vrmdata->vrm_v0_0.humanoid.humanBones_count; i++) {
								const auto bone = vrmdata->vrm_v0_0.humanoid.humanBones[i];
								const auto name = vrmdata->nodes[bone.node].name;
								const auto stored_hash = tm_string_repository->add(tm_string_repository->inst, name);
								hash_map.emplace(name, stored_hash);
								hash_to_index_map.emplace(stored_hash, stored_index);

								transform_data.hashes[stored_index] = stored_hash;
								transform_data.rotations[stored_index] = { 0, 0, 0, 1 };
								transform_data.translations[stored_index] = { 0, 0, 0 };

								// Consider first bone as a root bone when actual root bone is not found
								if (i == 0 && !rootbone_found) {
									options.rootbone = name;
								}

								stored_index++;
							}

							transform_data.availableCount = availableCount;

							TM_LOG("[INFO] VmcPacketListener starts recording...");
							state.received = true;
						}
					}
				}
			}
			else if (state.received && std::strcmp(address, "/VMC/Ext/Root/Pos") == 0) {

				const auto name = (arg++)->AsStringUnchecked();
				const auto px = (arg++)->AsFloatUnchecked();
				const auto py = (arg++)->AsFloatUnchecked();
				const auto pz = (arg++)->AsFloatUnchecked();
				const auto qx = (arg++)->AsFloatUnchecked();
				const auto qy = (arg++)->AsFloatUnchecked();
				const auto qz = (arg++)->AsFloatUnchecked();
				const auto qw = (arg++)->AsFloatUnchecked();

				(void)name; // unused

				const auto hash  = getStringHash(options.rootbone);
				const auto index = getStringIndex(hash);

				{
					std::lock_guard<std::mutex> lock(motionclient_lock_guard);
					transform_data.hashes[index] = hash; // "Armature" etc
					transform_data.translations[index] = { px, py, pz };
					transform_data.rotations[index] = { qx, -qy, -qz, qw };
				}
			}
			else if (state.received && std::strcmp(address, "/VMC/Ext/Bone/Pos") == 0) {

				const auto name = (arg++)->AsStringUnchecked();
				const auto px = (arg++)->AsFloatUnchecked();
				const auto py = (arg++)->AsFloatUnchecked();
				const auto pz = (arg++)->AsFloatUnchecked();
				const auto qx = (arg++)->AsFloatUnchecked();
				const auto qy = (arg++)->AsFloatUnchecked();
				const auto qz = (arg++)->AsFloatUnchecked();
				const auto qw = (arg++)->AsFloatUnchecked();

				cgltf_node* node = vrm_get_humanoid_bone(name, &humanoid_mapping);

				if (node != nullptr) {
					const auto hash  = getStringHash(node->name); // "mixamorig:Hips" etc
					const auto index = getStringIndex(hash);

					{
						std::lock_guard<std::mutex> lock(motionclient_lock_guard);
						transform_data.hashes[index] = hash;
						transform_data.translations[index] = { px, py, pz };
						transform_data.rotations[index] = { qx, -qy, -qz, qw };
					}
				}
			}

			const auto time = std::chrono::steady_clock::now();
			const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(time - lasttime_checked);
			if (delta > options.interval) {
				transform_data.availableCount = getAvailableCount(); // This practically enables polling
				lasttime_checked = time;

			}
		}
		catch (...) {

		}
	}

	uint8_t getAvailableCount() {
		return static_cast<uint8_t>(hash_map.size());
	}

	uint8_t getStringIndex(uint64_t hash) {
		const auto iter = hash_to_index_map.find(hash);
		assert(iter != hash_to_index_map.end());
		return iter->second;
	}

	uint64_t getStringHash(std::string key) {
		const auto iter = hash_map.find(key);
		assert(iter != hash_map.end());
		return iter->second;
	}

	motion_listener_transform_data_t transform_data;

private:

	cgltf_data* vrmdata;
	vmc_humanoid_mapping humanoid_mapping;
	vmc_state state;
	vmc_options options;
	std::chrono::steady_clock::time_point lasttime_checked;

	std::unordered_map<std::string, uint64_t> hash_map;
	std::unordered_map<uint64_t, uint8_t> hash_to_index_map;

};

static std::uint8_t retain_count = 0;
static VmcPacketListener* packetListener = nullptr;
static UdpListeningReceiveSocket* receiveSocket = nullptr;
static std::uint16_t port = 39539;

bool motionclient_started() {
	return (retain_count > 0);
}

void motionclient_start(struct tm_string_repository_i* string_repository, struct tm_logger_api* tm_logger_api_) {
	if (motionclient_started()) {
		retain_count++;
		return;
	}

	tm_logger_api = tm_logger_api_;
	tm_string_repository = string_repository;

	try {
		vmc_options options = {};
		options.rootbone = "ROOT";
		options.interval = std::chrono::milliseconds(1000 / 30);

		packetListener = new VmcPacketListener(options);
		receiveSocket = new UdpListeningReceiveSocket(
			IpEndpointName(IpEndpointName::ANY_ADDRESS, port),
			packetListener);

		retain_count = 1;

		receiveSocket->Run();

		// retain_count should equal zero here because this should happens after vmcclient_stop().
		assert(retain_count == 0);

		delete receiveSocket;
		receiveSocket = nullptr;

		delete packetListener;
		packetListener = nullptr;
	}
	catch (...) {
		TM_LOG("Failed to start packet listener");
		retain_count = 0;
	}
}

void motionclient_stop() {
	if (!motionclient_started()) {
		return;
	}

	retain_count--;

	if (retain_count == 0 && receiveSocket != nullptr && receiveSocket->IsBound()) {
		receiveSocket->Break();

		try {
			// ping listener in order to break the loop when listener is blocking
			UdpTransmitSocket transmitSocket(IpEndpointName("127.0.0.1", port));

			char buffer[64];
			osc::OutboundPacketStream p(buffer, 64);
			p << osc::BeginMessage("/VMC/PING") << osc::EndMessage;

			transmitSocket.Send(p.Data(), p.Size());
		}
		catch (...) {
			// you can ignore this because listener may be already stopped depending on the break timing
		}
	}
}

motion_listener_transform_data_t* motionclient_poll() {
	std::lock_guard<std::mutex> lock(motionclient_lock_guard);
	if (packetListener == nullptr) {
		return nullptr;
	}
	return &packetListener->transform_data;
}