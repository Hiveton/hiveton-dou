#ifndef MUSIC_SERVICE_H
#define MUSIC_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MUSIC_SERVICE_DIR "/mp3"

int music_service_refresh(void);
uint16_t music_service_count(void);
uint16_t music_service_selected_index(void);
bool music_service_select(uint16_t index);
bool music_service_select_prev(void);
bool music_service_select_next(void);
const char *music_service_get_title(uint16_t index);
const char *music_service_get_selected_title(void);
const char *music_service_get_selected_path(void);
bool music_service_play_selected(void);
bool music_service_toggle_playback(void);
void music_service_stop(void);
bool music_service_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif
