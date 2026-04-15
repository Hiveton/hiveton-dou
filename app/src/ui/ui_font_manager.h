#ifndef XIAOZHI_UI_FONT_MANAGER_H
#define XIAOZHI_UI_FONT_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_FONT_MANAGER_PATH_MAX 256
#define UI_FONT_MANAGER_NAME_MAX 96

typedef struct
{
    char path[UI_FONT_MANAGER_PATH_MAX];
    char name[UI_FONT_MANAGER_NAME_MAX];
    bool selected;
    bool system;
} ui_font_manager_item_t;

void ui_font_manager_init(void);
void ui_font_manager_deinit(void);
void ui_font_manager_notify_storage_ready(void);
void ui_font_manager_notify_storage_removed(void);
bool ui_font_manager_using_system_font(void);
bool ui_font_manager_get_active_font_path(char *buffer, size_t buffer_size);
const char *ui_font_manager_get_active_font_name(void);
bool ui_font_manager_select_system_font(void);
bool ui_font_manager_select_font_file(const char *path);
uint16_t ui_font_manager_list_items(ui_font_manager_item_t *items, uint16_t max_items);
void ui_font_manager_rebuild_ui(void);

#ifdef __cplusplus
}
#endif

#endif
