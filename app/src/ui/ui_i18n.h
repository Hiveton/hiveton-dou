#ifndef UI_I18N_H
#define UI_I18N_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

const char *ui_i18n_pick(const char *zh_cn, const char *en_us);
bool ui_i18n_is_english(void);
const char *ui_i18n_weekday_short(int monday_index);
const char *ui_i18n_weekday_fallback(void);
const char *ui_i18n_translate_weekday_label(const char *text);
const char *ui_i18n_translate_weather_text(const char *text);

#ifdef __cplusplus
}
#endif

#endif
