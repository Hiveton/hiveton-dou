#include "ui.h"
#include "ui_components.h"
#include "ui_helpers.h"
#include "ui_i18n.h"
#include "ui_runtime_adapter.h"

#include <stddef.h>
#include <stdint.h>

lv_obj_t *ui_File_Manager = NULL;

typedef struct
{
    const char *zh;
    const char *en;
    const char *path;
    ui_file_manager_category_t category;
} ui_file_manager_row_t;

static const ui_file_manager_row_t s_file_manager_rows[] = {
    {"字体", "Fonts", "font", UI_FILE_MANAGER_CATEGORY_FONT},
    {"音乐", "Music", "mp3", UI_FILE_MANAGER_CATEGORY_MUSIC},
    {"录音", "Recordings", "record", UI_FILE_MANAGER_CATEGORY_RECORD},
    {"阅读", "Books", "books", UI_FILE_MANAGER_CATEGORY_READING},
};

static lv_obj_t *ui_file_manager_plain_obj(lv_obj_t *parent,
                                           int x,
                                           int y,
                                           int w,
                                           int h,
                                           int radius,
                                           lv_opa_t opa,
                                           uint32_t bg,
                                           int border_w)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(obj, ui_px_w(w), ui_px_h(h));
    lv_obj_set_style_radius(obj, ui_px_x(radius), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(obj, border_w, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static void ui_file_manager_row_event_cb(lv_event_t *e)
{
    uintptr_t index;
    const ui_file_manager_row_t *row;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    index = (uintptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (index >= sizeof(s_file_manager_rows) / sizeof(s_file_manager_rows[0]))
    {
        return;
    }

    row = &s_file_manager_rows[index];
    ui_file_manager_detail_set_category(row->category);
    ui_runtime_switch_to(UI_SCREEN_FILE_MANAGER_DETAIL);
}

static void ui_file_manager_create_row(lv_obj_t *parent,
                                       const ui_file_manager_row_t *row,
                                       size_t index,
                                       int y)
{
    lv_obj_t *hit;

    hit = ui_file_manager_plain_obj(parent, 0, y, 528, 84, 0, LV_OPA_TRANSP, 0xffffff, 0);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(hit, (void *)(uintptr_t)index);
    lv_obj_add_event_cb(hit, ui_file_manager_row_event_cb, LV_EVENT_CLICKED, NULL);

    ui_create_label(hit,
                    ui_i18n_pick(row->zh, row->en),
                    32,
                    28,
                    180,
                    31,
                    26,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);

    ui_create_label(hit,
                    row->path,
                    250,
                    28,
                    220,
                    31,
                    26,
                    LV_TEXT_ALIGN_RIGHT,
                    false,
                    false);

    ui_create_label(hit,
                    ">",
                    480,
                    22,
                    20,
                    34,
                    26,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);
}

void ui_File_Manager_screen_init(void)
{
    size_t i;

    if (ui_File_Manager != NULL)
    {
        return;
    }

    ui_File_Manager = ui_create_screen_base();
    lv_obj_set_style_bg_color(ui_File_Manager, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(ui_File_Manager, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ui_File_Manager, LV_OBJ_FLAG_SCROLLABLE);

    ui_secondary_top_nav_create(ui_File_Manager, ui_i18n_pick("文件管理", "File Manager"), UI_SCREEN_SETTINGS);

    for (i = 0; i < sizeof(s_file_manager_rows) / sizeof(s_file_manager_rows[0]); ++i)
    {
        ui_file_manager_create_row(ui_File_Manager, &s_file_manager_rows[i], i, 98 + (int)i * 89);
    }
}

void ui_File_Manager_screen_destroy(void)
{
    if (ui_File_Manager != NULL)
    {
        lv_obj_delete(ui_File_Manager);
        ui_File_Manager = NULL;
    }
}
