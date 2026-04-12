#ifndef RECORDER_SERVICE_H
#define RECORDER_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    char name[96];
    char path[192];
    uint32_t size_bytes;
    uint32_t duration_ms;
    time_t mtime;
} recorder_service_file_t;

void recorder_service_init(void);
bool recorder_service_start_record(void);
bool recorder_service_stop_record(void);
bool recorder_service_is_recording(void);
uint32_t recorder_service_get_record_elapsed_ms(void);
void recorder_service_get_record_status_text(char *buffer, size_t buffer_size);
bool recorder_service_storage_ready(void);
const char *recorder_service_get_record_dir(void);
const char *recorder_service_get_recording_file_name(void);
size_t recorder_service_scan_files(recorder_service_file_t *files, size_t max_files);
bool recorder_service_play_file(const char *path);
bool recorder_service_stop_playback(void);
bool recorder_service_is_playing(void);
const char *recorder_service_get_playing_path(void);
const char *recorder_service_get_playing_name(void);
void recorder_service_get_play_status_text(char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif
