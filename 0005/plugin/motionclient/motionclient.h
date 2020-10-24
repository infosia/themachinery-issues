#ifndef MACHINERY_MOTIONCLIENT_H_INCLUDED__
#define MACHINERY_MOTIONCLIENT_H_INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

#include <foundation/string_repository.h>
#include <foundation/log.h>

typedef struct motion_listener_transform_data_t
{
	uint8_t count;
	uint64_t* hashes;
	tm_vec3_t* translations;
	tm_vec4_t* rotations;
} motion_listener_transform_data_t;

bool motionclient_started();
void motionclient_start(tm_string_repository_i*);
void motionclient_stop(tm_string_repository_i*);
motion_listener_transform_data_t* motionclient_poll();


#ifdef __cplusplus
}
#endif

#endif /* #ifndef MACHINERY_MOTIONCLIENT_H_INCLUDED__ */