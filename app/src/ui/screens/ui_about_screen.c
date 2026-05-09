#include "ui.h"
#include "ui_helpers.h"
#include "ui_i18n.h"
#include "../../xiaozhi/xiaozhi_client_public.h"

lv_obj_t *ui_About = NULL;

#define UI_ABOUT_PRODUCT_MODEL "AI小豆"
#define UI_ABOUT_HARDWARE_MODEL "sf32lb52-lcd_n16r8"
#define UI_ABOUT_VERSION VERSION

static const char *ui_about_safe_text(const char *text)
{
    if (text == NULL || text[0] == '\0')
    {
        return "--";
    }

    return text;
}

static void ui_about_create_row(lv_obj_t *parent,
                                int y,
                                const char *label_zh,
                                const char *label_en,
                                const char *value)
{
    lv_obj_t *row;
    lv_obj_t *value_label;
    lv_obj_t *line;

    row = lv_obj_create(parent);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(row, ui_px_x(24), ui_px_y(y));
    lv_obj_set_size(row, ui_px_w(432), ui_px_h(56));
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_outline_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    (void)ui_create_label(row,
                          ui_i18n_pick(label_zh, label_en),
                          0,
                          8,
                          132,
                          34,
                          20,
                          LV_TEXT_ALIGN_LEFT,
                          false,
                          true);

    value_label = ui_create_label(row,
                                  ui_about_safe_text(value),
                                  140,
                                  8,
                                  292,
                                  34,
                                  18,
                                  LV_TEXT_ALIGN_RIGHT,
                                  false,
                                  true);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);

    line = lv_obj_create(row);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(line, ui_px_x(0), ui_px_y(55));
    lv_obj_set_size(line, ui_px_w(432), ui_px_h(1));
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(line, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_shadow_width(line, 0, 0);
    lv_obj_set_style_outline_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
}

void ui_About_screen_init(void)
{
    lv_obj_t *panel;

    if (ui_About != NULL)
    {
        return;
    }

    ui_About = ui_create_screen_base();

    panel = ui_create_card(ui_About, 24, 32, 480, 430, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 10, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);

    (void)ui_create_label(panel,
                          ui_i18n_pick("设备信息", "Device Info"),
                          24,
                          24,
                          432,
                          36,
                          24,
                          LV_TEXT_ALIGN_LEFT,
                          false,
                          true);

    ui_about_create_row(panel, 78, "产品型号", "Model", UI_ABOUT_PRODUCT_MODEL);
    ui_about_create_row(panel, 134, "硬件平台", "Hardware", UI_ABOUT_HARDWARE_MODEL);
    ui_about_create_row(panel, 190, "MAC 地址", "MAC Address", get_mac_address());
    ui_about_create_row(panel, 246, "唯一 ID", "Unique ID", get_client_id());
    ui_about_create_row(panel, 302, "版本号", "Version", UI_ABOUT_VERSION);
    ui_about_create_row(panel, 358, "构建时间", "Build Time", __DATE__ " " __TIME__);
}

void ui_About_screen_destroy(void)
{
    if (ui_About != NULL)
    {
        lv_obj_delete(ui_About);
        ui_About = NULL;
    }
}
