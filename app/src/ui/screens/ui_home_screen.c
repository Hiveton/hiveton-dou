#include <string.h>
#include <rtthread.h>

#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "../../network/net_manager.h"
#include "../../xiaozhi/weather/weather.h"

#ifndef UI_HOME_ENABLE_4G_TEST_PANEL
#define UI_HOME_ENABLE_4G_TEST_PANEL 0
#endif

#ifndef UI_HOME_ENABLE_BOOT_4G_AUTO_CONNECT
#define UI_HOME_ENABLE_BOOT_4G_AUTO_CONNECT 0
#endif

#if UI_HOME_ENABLE_4G_TEST_PANEL
#include "cat1_modem.h"
#endif

lv_obj_t *ui_Home = NULL;

static xiaozhi_home_screen_refs_t s_home_refs;
#if UI_HOME_ENABLE_4G_TEST_PANEL
static lv_obj_t *s_home_4g_panel = NULL;
static lv_obj_t *s_home_4g_label = NULL;
static lv_timer_t *s_home_4g_timer = NULL;

#define HOME_4G_STATUS_REFRESH_MS 1000U
#define HOME_4G_STATUS_TEXT_MAX   320U
#endif
#if 0
static lv_obj_t *s_aw32001_debug_panel = NULL;
static lv_obj_t *s_aw32001_debug_label = NULL;
static lv_obj_t *s_bq27220_debug_label = NULL;
static lv_obj_t *s_cat1_debug_label = NULL;
static lv_timer_t *s_aw32001_debug_timer = NULL;

#define AW32001_DEBUG_REFRESH_MS 1000U
#define AW32001_DEBUG_STATUS_TEXT_MAX 140U
#endif

extern const lv_image_dsc_t home_ai;
extern const lv_image_dsc_t home_calendar;
extern const lv_image_dsc_t home_clock;
extern const lv_image_dsc_t home_music;
extern const lv_image_dsc_t home_pet;
extern const lv_image_dsc_t home_reading;
extern const lv_image_dsc_t home_record;
extern const lv_image_dsc_t home_settings;
extern const lv_image_dsc_t home_weather;

typedef struct
{
    const lv_image_dsc_t *icon;
    const char *label_zh;
    const char *label_en;
    ui_screen_id_t target;
} ui_home_tile_t;

static const ui_home_tile_t s_home_tiles[] = {
    {&home_reading, "阅读", "Reading", UI_SCREEN_READING_LIST},
    {&home_pet, "陪伴成长", "Companion Growth", UI_SCREEN_PET},
    {&home_ai, "AI小豆", "AI Dou", UI_SCREEN_AI_DOU},
    {&home_clock, "时间管理", "Time", UI_SCREEN_TIME_MANAGE},
    {&home_weather, "天气", "Weather", UI_SCREEN_WEATHER},
    {&home_calendar, "日历", "Calendar", UI_SCREEN_CALENDAR},
    {&home_record, "录音", "Recorder", UI_SCREEN_RECORDER},
    {&home_music, "音乐", "Music", UI_SCREEN_MUSIC_LIST},
    {&home_settings, "设置", "Settings", UI_SCREEN_SETTINGS},
};

#if UI_HOME_ENABLE_4G_TEST_PANEL
static const char *ui_home_net_mode_text(net_manager_mode_t mode)
{
    switch (mode)
    {
    case NET_MANAGER_MODE_BT:
        return "蓝牙";
    case NET_MANAGER_MODE_4G:
        return "4G";
    case NET_MANAGER_MODE_SLEEP:
        return "睡眠";
    case NET_MANAGER_MODE_NONE:
    default:
        return "关闭";
    }
}

static const char *ui_home_net_link_text(net_manager_link_t link)
{
    switch (link)
    {
    case NET_MANAGER_LINK_BT_PAN:
        return "蓝牙PAN";
    case NET_MANAGER_LINK_4G_CAT1:
        return "4G";
    case NET_MANAGER_LINK_NONE:
    default:
        return "无";
    }
}

static const char *ui_home_net_service_text(net_manager_service_state_t state)
{
    switch (state)
    {
    case NET_MANAGER_SERVICE_RADIO_READY:
        return "无线就绪";
    case NET_MANAGER_SERVICE_LINK_READY:
        return "链路就绪";
    case NET_MANAGER_SERVICE_DNS_READY:
        return "DNS就绪";
    case NET_MANAGER_SERVICE_INTERNET_READY:
        return "公网可用";
    case NET_MANAGER_SERVICE_OFFLINE:
    default:
        return "离线";
    }
}

static const char *ui_home_bt_state_text(const net_manager_snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return "关";
    }

    if (snapshot->active_link == NET_MANAGER_LINK_BT_PAN)
    {
        return "连通";
    }

    if (snapshot->desired_mode == NET_MANAGER_MODE_BT)
    {
        return snapshot->bt_enabled ? "待连" : "关";
    }

    return snapshot->bt_enabled ? "开" : "关";
}

static const char *ui_home_4g_state_text(const net_manager_snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return "关";
    }

    if (snapshot->active_link == NET_MANAGER_LINK_4G_CAT1)
    {
        return "连通";
    }

    if (snapshot->desired_mode == NET_MANAGER_MODE_4G)
    {
        return snapshot->net_4g_enabled ? "待连" : "关";
    }

    return snapshot->net_4g_enabled ? "开" : "关";
}

static void ui_home_get_4g_problem_text(const net_manager_snapshot_t *snapshot, char *buffer, size_t size)
{
    if (snapshot == NULL || buffer == NULL || size == 0U)
    {
        return;
    }

    if (snapshot->radios_suspended)
    {
        rt_snprintf(buffer, size, "无线休眠中");
    }
    else if (snapshot->desired_mode != NET_MANAGER_MODE_4G)
    {
        rt_snprintf(buffer, size, "当前模式不是4G");
    }
    else if (snapshot->bt_enabled || snapshot->bt_connected || snapshot->pan_ready)
    {
        rt_snprintf(buffer, size, "蓝牙仍在占用链路");
    }
    else if (!snapshot->net_4g_enabled)
    {
        rt_snprintf(buffer, size, "4G未开启");
    }
    else if (snapshot->active_link != NET_MANAGER_LINK_4G_CAT1)
    {
        char cat1_status[96];

        cat1_modem_get_status_text(cat1_status, sizeof(cat1_status));
        rt_snprintf(buffer, size, "%s", cat1_status);
    }
    else if (!net_manager_dns_ready())
    {
        rt_snprintf(buffer, size, "DNS未就绪");
    }
    else if (!net_manager_internet_ready())
    {
        rt_snprintf(buffer, size, "公网未验证");
    }
    else
    {
        rt_snprintf(buffer, size, "无");
    }
}

static void ui_home_refresh_4g_status(void)
{
    net_manager_snapshot_t snapshot;
    char problem[96];
    char text[HOME_4G_STATUS_TEXT_MAX];

    if (s_home_4g_label == NULL)
    {
        return;
    }

    net_manager_get_snapshot(&snapshot);
    ui_home_get_4g_problem_text(&snapshot, problem, sizeof(problem));
    rt_snprintf(text,
                sizeof(text),
                "4G测试  模式:%s  BT:%s\n4G:%s  CAT1:%s  链路:%s\n服务:%s  DNS:%s  网络:%s\n问题:%s",
                ui_home_net_mode_text(snapshot.desired_mode),
                ui_home_bt_state_text(&snapshot),
                ui_home_4g_state_text(&snapshot),
                snapshot.cat1_ready ? "就绪" : "未就绪",
                ui_home_net_link_text(snapshot.active_link),
                ui_home_net_service_text(net_manager_get_service_state()),
                net_manager_dns_ready() ? "就绪" : "未就绪",
                net_manager_internet_ready() ? "可用" : "不可用",
                problem);
    lv_label_set_text(s_home_4g_label, text);
}

static void ui_home_4g_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    ui_home_refresh_4g_status();
}

static void ui_home_create_4g_test_panel(lv_obj_t *parent)
{
    if (parent == NULL)
    {
        return;
    }

    s_home_4g_panel = lv_obj_create(parent);
    lv_obj_remove_flag(s_home_4g_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_home_4g_panel, ui_px_x(16), ui_px_y(632));
    lv_obj_set_size(s_home_4g_panel, ui_px_w(496), ui_px_h(86));
    lv_obj_set_style_radius(s_home_4g_panel, 0, 0);
    lv_obj_set_style_bg_color(s_home_4g_panel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_home_4g_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_home_4g_panel, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_home_4g_panel, 2, 0);
    lv_obj_set_style_shadow_width(s_home_4g_panel, 0, 0);
    lv_obj_set_style_pad_all(s_home_4g_panel, 6, 0);

    s_home_4g_label = lv_label_create(s_home_4g_panel);
    lv_obj_set_width(s_home_4g_label, ui_px_w(484));
    lv_obj_set_style_text_font(s_home_4g_label, home_screen_font_get(18), 0);
    lv_obj_set_style_text_color(s_home_4g_label, lv_color_black(), 0);
    lv_obj_set_style_text_line_space(s_home_4g_label, 2, 0);
    lv_label_set_long_mode(s_home_4g_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_home_4g_label, LV_ALIGN_TOP_LEFT, 0, 0);

    if (s_home_4g_timer != NULL)
    {
        lv_timer_delete(s_home_4g_timer);
    }
    s_home_4g_timer = lv_timer_create(ui_home_4g_timer_cb, HOME_4G_STATUS_REFRESH_MS, NULL);
    ui_home_refresh_4g_status();
}
#endif

#if 0
static void ui_home_aw32001_debug_refresh_cb(lv_timer_t *timer)
{
    char text[AW32001_DEBUG_STATUS_TEXT_MAX];
    char bq_text[AW32001_DEBUG_STATUS_TEXT_MAX];
    char cat1_text[AW32001_DEBUG_STATUS_TEXT_MAX];

    LV_UNUSED(timer);

    aw32001_debug_poll_once();
    aw32001_debug_get_status_text(text, sizeof(text));
    bq27220_monitor_get_status_text(bq_text, sizeof(bq_text));
    cat1_modem_get_status_text(cat1_text, sizeof(cat1_text));
    if (s_aw32001_debug_label != NULL)
    {
        lv_label_set_text(s_aw32001_debug_label, text);
    }
    if (s_bq27220_debug_label != NULL)
    {
        lv_label_set_text(s_bq27220_debug_label, bq_text);
    }
    if (s_cat1_debug_label != NULL)
    {
        lv_label_set_text(s_cat1_debug_label, cat1_text);
    }
}
#endif

static void ui_home_maybe_request_boot_network(void)
{
#if UI_HOME_ENABLE_BOOT_4G_AUTO_CONNECT
    net_manager_mode_t desired_mode = net_manager_get_desired_mode();

    if (desired_mode == NET_MANAGER_MODE_BT ||
        desired_mode == NET_MANAGER_MODE_SLEEP)
    {
        rt_kprintf("[ui_home] skip boot 4G auto-connect, keep user mode=%d\n", (int)desired_mode);
        return;
    }

    rt_kprintf("[ui_home] request boot 4G auto-connect, previous mode=%d\n", (int)desired_mode);
    net_manager_request_4g_mode();
#else
    /* Keep the user's network mode unless a product build explicitly opts in. */
#endif
}

static lv_obj_t *create_home_hotspot(lv_obj_t *parent,
                                     int x,
                                     int y,
                                     int w,
                                     int h,
                                     ui_screen_id_t target)
{
    lv_obj_t *zone = lv_obj_create(parent);

    lv_obj_remove_flag(zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(zone, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(zone, ui_px_w(w), ui_px_h(h));
    lv_obj_set_style_bg_opa(zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(zone, 0, 0);
    lv_obj_set_style_radius(zone, 0, 0);
    lv_obj_set_style_shadow_width(zone, 0, 0);
    lv_obj_set_style_outline_width(zone, 0, 0);
    lv_obj_set_style_pad_all(zone, 0, 0);
    ui_attach_nav_event(zone, target);
    return zone;
}

const xiaozhi_home_screen_refs_t *ui_home_screen_refs_get(void)
{
    return &s_home_refs;
}

void ui_Home_screen_init(void)
{
    lv_obj_t *section;
    size_t i;
    size_t visible_index = 0U;
    static const int home_tile_x_positions[3] = {18, 186, 354};
    static const int home_tile_y_positions[3] = {18, 220, 422};

    if (ui_Home != NULL)
    {
        return;
    }

    memset(&s_home_refs, 0, sizeof(s_home_refs));
    ui_Home = ui_create_screen_base();
    s_home_refs.screen = ui_Home;
    ui_build_status_bar(ui_Home, &s_home_refs);
    ui_home_maybe_request_boot_network();
    xiaozhi_weather_request_force_refresh();
    ui_force_refresh_global_status_bar();
    section = lv_obj_create(ui_Home);
    lv_obj_remove_flag(section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(section, 0, 0);
    lv_obj_set_pos(section, 0, ui_px_y(68));
    lv_obj_set_size(section, ui_px_w(528), ui_px_h(724));

#if UI_HOME_ENABLE_4G_TEST_PANEL
    ui_home_create_4g_test_panel(section);
#endif

    for (i = 0; i < sizeof(s_home_tiles) / sizeof(s_home_tiles[0]); ++i)
    {
        const ui_home_tile_t *tile = &s_home_tiles[i];
        int x;
        int y;
        lv_obj_t *zone;
        lv_obj_t *icon;

        if (tile->target == UI_SCREEN_WEATHER &&
            !xiaozhi_weather_is_home_entry_enabled())
        {
            continue;
        }

        x = home_tile_x_positions[visible_index % 3U];
        y = home_tile_y_positions[visible_index / 3U];
        zone = create_home_hotspot(section,
                                             x,
                                             y,
                                             156,
                                             198,
                                             tile->target);
        icon = ui_create_image_slot(zone, 38, 24, 80, 80);

        ui_img_set_src(icon, tile->icon);
        ui_create_label(zone,
                        ui_i18n_pick(tile->label_zh, tile->label_en),
                        0,
                        126,
                        156,
                        30,
                        26,
                        LV_TEXT_ALIGN_CENTER,
                        false,
                        false);
        ++visible_index;
    }

}

void ui_Home_screen_destroy(void)
{
#if UI_HOME_ENABLE_4G_TEST_PANEL
    if (s_home_4g_timer != NULL)
    {
        lv_timer_delete(s_home_4g_timer);
        s_home_4g_timer = NULL;
    }
    s_home_4g_panel = NULL;
    s_home_4g_label = NULL;
#endif

    if (ui_Home != NULL)
    {
        lv_obj_delete(ui_Home);
        ui_Home = NULL;
    }

    memset(&s_home_refs, 0, sizeof(s_home_refs));
}
