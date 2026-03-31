#ifndef XIAOZHI_H
#define XIAOZHI_H

#include "xiaozhi_client_public.h"

#ifdef __cplusplus
extern "C" {
#endif

void xz_mqtt_button_init(void);
void xz_audio_init(void);
void xz_speaker_open(xz_audio_t *thiz);
void xz_speaker_close(xz_audio_t *thiz);
void xz_speaker_abort(xz_audio_t *thiz);
void xz_mic_open(xz_audio_t *thiz);
void xz_mic_close(xz_audio_t *thiz);
void xz_mic_stop_capture(xz_audio_t *thiz);
rt_err_t xz_mic_flush_pending(xz_audio_t *thiz, rt_int32_t timeout_ms);
void reinit_audio(void);


#ifdef __cplusplus
}
#endif


#endif
