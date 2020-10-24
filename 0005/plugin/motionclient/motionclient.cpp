#include <cstdint>
#include <chrono>
#include <thread>
#include <mutex>

#include "motionclient.h"
#include <foundation/math.inl>
#define NUMBER_OF_KNOWN_BONES 66

const char* KNOWN_BONE_NAMES[NUMBER_OF_KNOWN_BONES] = {
"Armature",
"mixamorig:Hips",
"mixamorig:LeftUpLeg",
"mixamorig:LeftLeg",
"mixamorig:LeftFoot",
"mixamorig:LeftToeBase",
"mixamorig:LeftToe_End",
"mixamorig:RightUpLeg",
"mixamorig:RightLeg",
"mixamorig:RightFoot",
"mixamorig:RightToeBase",
"mixamorig:RightToe_End",
"mixamorig:Spine",
"mixamorig:Spine1",
"mixamorig:Spine2",
"mixamorig:LeftShoulder",
"mixamorig:LeftArm",
"mixamorig:LeftForeArm",
"mixamorig:LeftHand",
"mixamorig:LeftHandIndex1",
"mixamorig:LeftHandIndex2",
"mixamorig:LeftHandIndex3",
"mixamorig:LeftHandIndex4",
"mixamorig:LeftHandMiddle1",
"mixamorig:LeftHandMiddle2",
"mixamorig:LeftHandMiddle3",
"mixamorig:LeftHandMiddle4",
"mixamorig:LeftHandPinky1",
"mixamorig:LeftHandPinky2",
"mixamorig:LeftHandPinky3",
"mixamorig:LeftHandPinky4",
"mixamorig:LeftHandRing1",
"mixamorig:LeftHandRing2",
"mixamorig:LeftHandRing3",
"mixamorig:LeftHandRing4",
"mixamorig:LeftHandThumb1",
"mixamorig:LeftHandThumb2",
"mixamorig:LeftHandThumb3",
"mixamorig:LeftHandThumb4",
"mixamorig:Neck",
"mixamorig:Head",
"mixamorig:HeadTop_End",
"mixamorig:RightShoulder",
"mixamorig:RightArm",
"mixamorig:RightForeArm",
"mixamorig:RightHand",
"mixamorig:RightHandIndex1",
"mixamorig:RightHandIndex2",
"mixamorig:RightHandIndex3",
"mixamorig:RightHandIndex4",
"mixamorig:RightHandMiddle1",
"mixamorig:RightHandMiddle2",
"mixamorig:RightHandMiddle3",
"mixamorig:RightHandMiddle4",
"mixamorig:RightHandPinky1",
"mixamorig:RightHandPinky2",
"mixamorig:RightHandPinky3",
"mixamorig:RightHandPinky4",
"mixamorig:RightHandRing1",
"mixamorig:RightHandRing2",
"mixamorig:RightHandRing3",
"mixamorig:RightHandRing4",
"mixamorig:RightHandThumb1",
"mixamorig:RightHandThumb2",
"mixamorig:RightHandThumb3",
"mixamorig:RightHandThumb4"
};

static std::uint8_t retain_count = 0;
static motion_listener_transform_data_t transform_data = { 0, nullptr, nullptr, nullptr };
static std::mutex motionclient_lock_guard;

bool motionclient_started() {
	return (retain_count > 0);
}

void motionclient_start(tm_string_repository_i* string_repository) {
	if (motionclient_started()) {
		retain_count++;
		return;
	}

	retain_count = 1;
	transform_data.count = 0;
	transform_data.hashes = new uint64_t[NUMBER_OF_KNOWN_BONES];
	transform_data.translations = new tm_vec3_t[NUMBER_OF_KNOWN_BONES];
	transform_data.rotations = new tm_vec4_t[NUMBER_OF_KNOWN_BONES];

	for (uint8_t i = 0; i < NUMBER_OF_KNOWN_BONES; i++) {
		transform_data.hashes[i] = string_repository->add(string_repository->inst, KNOWN_BONE_NAMES[i]);
		transform_data.rotations[i] = {0, 0, 0, 1};

		//const float angle = 0.1f;
		//transform_data.rotations[i] = tm_quaternion_from_rotation({ 0, 1, 0 }, angle);

		transform_data.translations[i] = { 0, 0, 0 };
	}

	transform_data.count = NUMBER_OF_KNOWN_BONES;

	// block
	while (retain_count > 0) {

		{
			std::lock_guard<std::mutex> lock(motionclient_lock_guard);

			// randomly changes bone rotaion for testing
			const auto time = std::chrono::steady_clock::now();
			const auto index = time.time_since_epoch().count() % NUMBER_OF_KNOWN_BONES;
			const float angle = 0.1f;
			const auto q = tm_quaternion_from_rotation({ 0, 1, 0 }, angle);
			transform_data.rotations[index] = tm_quaternion_mul(q, transform_data.rotations[index]);
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(66));
	}
}

void motionclient_stop(tm_string_repository_i* string_repository) {
	if (!motionclient_started()) {
		return;
	}

	retain_count--;

	if (retain_count == 0) {
		transform_data.count = 0;

		for (uint8_t i = 0; i < NUMBER_OF_KNOWN_BONES; i++) {
			string_repository->remove(string_repository->inst, transform_data.hashes[i]);
		}

		delete transform_data.hashes;
		delete transform_data.translations;
		delete transform_data.rotations;
	}
}

motion_listener_transform_data_t* motionclient_poll() {
	std::lock_guard<std::mutex> lock(motionclient_lock_guard);
	return &transform_data;
}