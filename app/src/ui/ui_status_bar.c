#include "ui_status_bar.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "rtthread.h"
#include "ui_dispatch.h"
#include "ui_helpers.h"
#include "ui_i18n.h"
#include "../bq27220_monitor.h"
#include "../network/net_manager.h"
#include "../sleep_manager.h"
#include "cat1_modem.h"
#include "../xiaozhi/weather/weather.h"

#define UI_STATUS_BAR_HEIGHT 68
#define UI_STATUS_BAR_REFRESH_THREAD_STACK_SIZE 1024
#define UI_STATUS_BAR_REFRESH_THREAD_PRIORITY 22
#define UI_STATUS_BAR_REFRESH_THREAD_TICK 10

typedef struct
{
    lv_obj_t *screen;
    xiaozhi_home_screen_refs_t refs;
    bool used;
} ui_status_bar_refs_entry_t;

typedef struct
{
    bool valid;
    bool time_valid;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    bool weather_available;
    int weather_temperature;
    int battery_percent;
    int charge_state;
    uint8_t aw_charge_state;
    uint8_t aw_fault_status;
    int bt_visual_state;
    int network_visual_state;
    net_manager_mode_t desired_mode;
    net_manager_link_t active_link;
    bool bt_enabled;
    bool net_4g_enabled;
    char network_detail[16];
} ui_status_bar_snapshot_t;

typedef enum
{
    UI_STATUS_BLUETOOTH_DISCONNECTED = 0,
    UI_STATUS_BLUETOOTH_WAITING,
    UI_STATUS_BLUETOOTH_CONNECTED,
} ui_status_bluetooth_state_t;

extern const lv_image_dsc_t ble_icon_img;
extern const lv_image_dsc_t network_icon_img;

static ui_status_bar_refs_entry_t s_status_bar_refs[UI_SCREEN_COUNT];
static struct rt_thread s_status_bar_refresh_thread;
static rt_uint8_t s_status_bar_refresh_thread_stack[UI_STATUS_BAR_REFRESH_THREAD_STACK_SIZE];
static bool s_status_bar_refresh_thread_started = false;
static volatile int s_status_pending_charge = -1;
static volatile int s_status_pending_battery_percent = -1;
static int s_status_applied_charge = -1;
static int s_status_applied_battery_percent = -1;
static int s_status_last_bt_icon_state = -1;
static int s_status_last_network_icon_state = -1;
static bool s_status_last_net_4g_enabled = false;
static char s_status_last_network_text[16];
static ui_status_bar_snapshot_t s_status_bar_snapshot = {0};

static lv_coord_t ui_status_bar_screen_width(void)
{
    lv_coord_t width = lv_disp_get_hor_res(NULL);

    return (width > 0) ? width : UI_FIGMA_WIDTH;
}

static void ui_status_bar_apply_basic_object_style(lv_obj_t *obj,
                                                   bool filled,
                                                   int32_t radius,
                                                   uint16_t border_width)
{
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj,
                              filled ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF),
                              0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(obj, border_width, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

static const char *ui_status_bar_weekday_from_index(int weekday)
{
    static const char *const k_weekdays[] = {
        "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"
    };

    if (weekday >= 0 && weekday < (int)(sizeof(k_weekdays) / sizeof(k_weekdays[0])))
    {
        return k_weekdays[weekday];
    }

    return "星期一";
}

static void ui_status_bar_set_label_text(lv_obj_t *label, const char *text)
{
    const char *old_text;

    if (label == NULL || text == NULL)
    {
        return;
    }

    old_text = lv_label_get_text(label);
    if ((old_text == NULL) || (strcmp(old_text, text) != 0))
    {
        lv_label_set_text(label, text);
    }
}

static void ui_status_bar_set_object_hidden(lv_obj_t *obj, bool hidden)
{
    bool hidden_now;

    if (obj == NULL)
    {
        return;
    }

    hidden_now = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if (hidden)
    {
        if (!hidden_now)
        {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else if (hidden_now)
    {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_status_bar_register_refs(lv_obj_t *screen,
                                        const xiaozhi_home_screen_refs_t *refs)
{
    size_t i;

    if (screen == NULL || refs == NULL)
    {
        return;
    }

    for (i = 0; i < sizeof(s_status_bar_refs) / sizeof(s_status_bar_refs[0]); ++i)
    {
        if (s_status_bar_refs[i].screen == screen || !s_status_bar_refs[i].used)
        {
            s_status_bar_refs[i].screen = screen;
            s_status_bar_refs[i].refs = *refs;
            s_status_bar_refs[i].refs.screen = screen;
            s_status_bar_refs[i].used = true;
            return;
        }
    }
}

static ui_status_bluetooth_state_t ui_status_bar_get_bluetooth_state(void)
{
    if (net_manager_bt_connected())
    {
        return UI_STATUS_BLUETOOTH_CONNECTED;
    }

    if (net_manager_bt_enabled())
    {
        return UI_STATUS_BLUETOOTH_WAITING;
    }

    return UI_STATUS_BLUETOOTH_DISCONNECTED;
}

static int ui_status_bar_get_cat1_visual_state(char *detail_text, size_t detail_size)
{
    char status_text[96];

    if (detail_text != NULL && detail_size > 0U)
    {
        detail_text[0] = '\0';
    }

    cat1_modem_get_status_text(status_text, sizeof(status_text));

    if (strstr(status_text, "已联网") != NULL)
    {
        if (detail_text != NULL && detail_size > 0U)
        {
            rt_snprintf(detail_text, detail_size, "在线");
        }
        return 3;
    }

    if (strstr(status_text, "LTE已注册") != NULL ||
        strstr(status_text, "分组域已注册") != NULL ||
        strstr(status_text, "获取IP地址") != NULL ||
        strstr(status_text, "激活数据链路") != NULL ||
        strstr(status_text, "配置APN") != NULL)
    {
        if (detail_text != NULL && detail_size > 0U)
        {
            rt_snprintf(detail_text, detail_size, "注册");
        }
        return 2;
    }

    if (strstr(status_text, "启动入网流程") != NULL ||
        strstr(status_text, "AT客户端已连接") != NULL ||
        strstr(status_text, "检查SIM卡") != NULL ||
        strstr(status_text, "等待网络注册") != NULL ||
        strstr(status_text, "搜网中") != NULL ||
        strstr(status_text, "待首页启动") != NULL ||
        strstr(status_text, "关闭回显") != NULL ||
        strstr(status_text, "模组上电中") != NULL ||
        strstr(status_text, "准备重新上电") != NULL ||
        strstr(status_text, "断电重启中") != NULL)
    {
        if (detail_text != NULL && detail_size > 0U)
        {
            rt_snprintf(detail_text, detail_size, "搜网");
        }
        return 1;
    }

    if (strstr(status_text, "SIM未就绪") != NULL)
    {
        if (detail_text != NULL && detail_size > 0U)
        {
            rt_snprintf(detail_text, detail_size, "SIM");
        }
        return 1;
    }

    if (strstr(status_text, "注册超时") != NULL ||
        strstr(status_text, "模组无响应") != NULL ||
        strstr(status_text, "未获取到IP") != NULL ||
        strstr(status_text, "IP已丢失") != NULL ||
        strstr(status_text, "激活失败") != NULL)
    {
        if (detail_text != NULL && detail_size > 0U)
        {
            rt_snprintf(detail_text, detail_size, "异常");
        }
        return -1;
    }

    if (detail_text != NULL && detail_size > 0U)
    {
        rt_snprintf(detail_text, detail_size, "离线");
    }
    return 0;
}

static void ui_status_bar_capture_snapshot(ui_status_bar_snapshot_t *snapshot)
{
    date_time_t current_time;
    bq27220_power_snapshot_t power_snapshot;
    weather_info_t current_weather = {0};
    bool use_fallback_time = false;
    char cat1_detail[16];

    if (snapshot == NULL)
    {
        return;
    }

    rt_memset(snapshot, 0, sizeof(*snapshot));
    snapshot->valid = true;

    rt_memset(&current_time, 0, sizeof(current_time));
    if (xiaozhi_time_get_current(&current_time) != RT_EOK)
    {
        use_fallback_time = true;
    }
    else if (current_time.year < 2026 ||
             current_time.month < 1 || current_time.month > 12 ||
             current_time.day < 1 || current_time.day > 31 ||
             current_time.hour < 0 || current_time.hour > 23 ||
             current_time.minute < 0 || current_time.minute > 59)
    {
        use_fallback_time = true;
    }

    if (use_fallback_time)
    {
        current_time.year = 2026;
        current_time.month = 1;
        current_time.day = 1;
        current_time.hour = 1;
        current_time.minute = 1;
        current_time.second = 0;
    }

    snapshot->time_valid = !use_fallback_time;
    snapshot->year = current_time.year;
    snapshot->month = current_time.month;
    snapshot->day = current_time.day;
    snapshot->hour = current_time.hour;
    snapshot->minute = current_time.minute;

    rt_memset(&current_weather, 0, sizeof(current_weather));
    if (xiaozhi_weather_peek(&current_weather) == RT_EOK && current_weather.last_update > 0)
    {
        snapshot->weather_available = true;
        snapshot->weather_temperature = current_weather.temperature;
    }
    else
    {
        snapshot->weather_available = false;
        snapshot->weather_temperature = INT32_MIN;
    }

    rt_memset(&power_snapshot, 0, sizeof(power_snapshot));
    bq27220_monitor_get_power_snapshot(&power_snapshot);
    if (power_snapshot.valid)
    {
        snapshot->battery_percent = (int)power_snapshot.battery_percent;
        snapshot->charge_state = power_snapshot.charging ? 1 : 0;
        snapshot->aw_charge_state = power_snapshot.aw_charge_state;
        snapshot->aw_fault_status = power_snapshot.aw_fault_status;
    }
    else
    {
        snapshot->battery_percent = (s_status_pending_battery_percent >= 0) ?
                                    s_status_pending_battery_percent :
                                    s_status_applied_battery_percent;
        snapshot->charge_state = (s_status_pending_charge >= 0) ?
                                 s_status_pending_charge :
                                 s_status_applied_charge;
    }

    snapshot->bt_visual_state = (int)ui_status_bar_get_bluetooth_state();
    snapshot->desired_mode = net_manager_get_desired_mode();
    snapshot->active_link = net_manager_get_active_link();
    snapshot->bt_enabled = net_manager_bt_enabled();
    snapshot->net_4g_enabled = net_manager_4g_enabled();
    snapshot->network_visual_state = ui_status_bar_get_cat1_visual_state(cat1_detail, sizeof(cat1_detail));

    if (snapshot->active_link == NET_MANAGER_LINK_BT_PAN)
    {
        rt_snprintf(snapshot->network_detail, sizeof(snapshot->network_detail), "在线");
    }
    else if (snapshot->active_link == NET_MANAGER_LINK_4G_CAT1)
    {
        rt_snprintf(snapshot->network_detail, sizeof(snapshot->network_detail), "%s", cat1_detail);
    }
    else if (snapshot->desired_mode == NET_MANAGER_MODE_BT)
    {
        rt_snprintf(snapshot->network_detail, sizeof(snapshot->network_detail), "%s",
                    snapshot->bt_enabled ? "蓝牙" : "蓝牙关闭");
    }
    else if (snapshot->desired_mode == NET_MANAGER_MODE_4G)
    {
        rt_snprintf(snapshot->network_detail, sizeof(snapshot->network_detail), "%s",
                    snapshot->net_4g_enabled ? cat1_detail : "4G关闭");
    }
    else if (snapshot->desired_mode == NET_MANAGER_MODE_SLEEP)
    {
        rt_snprintf(snapshot->network_detail, sizeof(snapshot->network_detail), "睡眠");
    }
    else
    {
        rt_snprintf(snapshot->network_detail, sizeof(snapshot->network_detail), "关闭");
    }
}

static bool ui_status_bar_snapshot_equal(const ui_status_bar_snapshot_t *lhs,
                                         const ui_status_bar_snapshot_t *rhs)
{
    if (lhs == NULL || rhs == NULL)
    {
        return false;
    }

    return lhs->valid == rhs->valid &&
           lhs->time_valid == rhs->time_valid &&
           lhs->year == rhs->year &&
           lhs->month == rhs->month &&
           lhs->day == rhs->day &&
           lhs->hour == rhs->hour &&
           lhs->minute == rhs->minute &&
           lhs->weather_available == rhs->weather_available &&
           lhs->weather_temperature == rhs->weather_temperature &&
           lhs->battery_percent == rhs->battery_percent &&
           lhs->charge_state == rhs->charge_state &&
           lhs->aw_charge_state == rhs->aw_charge_state &&
           lhs->aw_fault_status == rhs->aw_fault_status &&
           lhs->bt_visual_state == rhs->bt_visual_state &&
           lhs->network_visual_state == rhs->network_visual_state &&
           lhs->desired_mode == rhs->desired_mode &&
           lhs->active_link == rhs->active_link &&
           lhs->bt_enabled == rhs->bt_enabled &&
           lhs->net_4g_enabled == rhs->net_4g_enabled &&
           strcmp(lhs->network_detail, rhs->network_detail) == 0;
}

static void ui_status_bar_refresh_datetime(void)
{
    date_time_t current_time;
    weather_info_t current_weather = {0};
    bool weather_available = false;
    bool use_fallback_time = false;
    const char *weekday_label = NULL;
    char time_text[16];
    char meta_text[48];
    size_t i;

    memset(&current_time, 0, sizeof(current_time));

    if (xiaozhi_time_get_current(&current_time) != RT_EOK)
    {
        use_fallback_time = true;
    }

    if (!use_fallback_time)
    {
        if (current_time.year < 2026 ||
            current_time.month < 1 || current_time.month > 12 ||
            current_time.day < 1 || current_time.day > 31 ||
            current_time.hour < 0 || current_time.hour > 23 ||
            current_time.minute < 0 || current_time.minute > 59)
        {
            use_fallback_time = true;
        }
    }

    if (use_fallback_time)
    {
        current_time.year = 2026;
        current_time.month = 1;
        current_time.day = 1;
        current_time.hour = 1;
        current_time.minute = 1;
        current_time.second = 0;
        current_time.weekday = 4;
        weekday_label = ui_status_bar_weekday_from_index(current_time.weekday);
    }
    else
    {
        const char *raw_weekday = current_time.weekday_str;

        if (raw_weekday != NULL &&
            raw_weekday[0] != '\0' &&
            strchr(raw_weekday, '?') == NULL)
        {
            weekday_label = ui_i18n_translate_weekday_label(raw_weekday);
        }

        if (weekday_label == NULL || weekday_label[0] == '\0' || strchr(weekday_label, '?') != NULL)
        {
            weekday_label = ui_status_bar_weekday_from_index(current_time.weekday);
        }
    }

    if (xiaozhi_weather_peek(&current_weather) == RT_EOK && current_weather.last_update > 0)
    {
        weather_available = true;
    }

    rt_snprintf(time_text, sizeof(time_text), "%02d:%02d", current_time.hour, current_time.minute);

    if (weather_available)
    {
        rt_snprintf(meta_text,
                    sizeof(meta_text),
                    "%04d/%02d/%02d\n%s %d°C",
                    current_time.year,
                    current_time.month,
                    current_time.day,
                    weekday_label,
                    current_weather.temperature);
    }
    else
    {
        rt_snprintf(meta_text,
                    sizeof(meta_text),
                    "%04d/%02d/%02d\n%s",
                    current_time.year,
                    current_time.month,
                    current_time.day,
                    weekday_label);
    }

    for (i = 0; i < sizeof(s_status_bar_refs) / sizeof(s_status_bar_refs[0]); ++i)
    {
        xiaozhi_home_screen_refs_t *refs = &s_status_bar_refs[i].refs;

        if (!s_status_bar_refs[i].used)
        {
            continue;
        }

        if (refs->time_label != NULL)
        {
            ui_status_bar_set_label_text(refs->time_label, time_text);
        }

        if (refs->meta_label != NULL)
        {
            ui_status_bar_set_label_text(refs->meta_label, meta_text);
        }
    }
}

static void ui_status_bar_refresh_connection_icons(bool force)
{
    const ui_status_bar_snapshot_t *snapshot = &s_status_bar_snapshot;
    net_manager_link_t active_link = snapshot->active_link;
    bool bt_enabled = snapshot->bt_enabled;
    bool net_4g_enabled = snapshot->net_4g_enabled;
    const char *network_text = snapshot->network_detail;
    size_t i;

    if (!force &&
        s_status_last_bt_icon_state == (bt_enabled ? 1 : 0) &&
        s_status_last_network_icon_state == (int)active_link &&
        s_status_last_net_4g_enabled == net_4g_enabled &&
        strcmp(s_status_last_network_text, network_text) == 0)
    {
        return;
    }

    s_status_last_bt_icon_state = bt_enabled ? 1 : 0;
    s_status_last_network_icon_state = (int)active_link;
    s_status_last_net_4g_enabled = net_4g_enabled;
    rt_snprintf(s_status_last_network_text, sizeof(s_status_last_network_text), "%s", network_text);

    for (i = 0; i < sizeof(s_status_bar_refs) / sizeof(s_status_bar_refs[0]); ++i)
    {
        xiaozhi_home_screen_refs_t *refs = &s_status_bar_refs[i].refs;

        if (!s_status_bar_refs[i].used)
        {
            continue;
        }

        if (refs->bluetooth_icon != NULL)
        {
            ui_img_set_src(refs->bluetooth_icon, &ble_icon_img);
            ui_status_bar_set_object_hidden(refs->bluetooth_icon, false);
            lv_obj_set_style_opa(refs->bluetooth_icon,
                                 bt_enabled ? LV_OPA_COVER : LV_OPA_50,
                                 0);
        }

        if (refs->network_icon != NULL)
        {
            ui_img_set_src(refs->network_icon, &network_icon_img);
            ui_status_bar_set_object_hidden(refs->network_icon, false);
            lv_obj_set_style_opa(refs->network_icon,
                                 (active_link == NET_MANAGER_LINK_BT_PAN ||
                                  active_link == NET_MANAGER_LINK_4G_CAT1) ? LV_OPA_COVER : LV_OPA_50,
                                 0);
        }

        if (refs->ec800_status_label != NULL)
        {
            ui_status_bar_set_label_text(refs->ec800_status_label, "");
            ui_status_bar_set_object_hidden(refs->ec800_status_label, true);
        }
    }
}

static void ui_status_bar_refresh_charging_icons(void)
{
    size_t i;

    if (s_status_pending_charge < 0)
    {
        return;
    }

    s_status_applied_charge = s_status_pending_charge;

    for (i = 0; i < sizeof(s_status_bar_refs) / sizeof(s_status_bar_refs[0]); ++i)
    {
        lv_obj_t *icon;
        bool hidden_now;

        if (!s_status_bar_refs[i].used)
        {
            continue;
        }

        icon = s_status_bar_refs[i].refs.standby_charging_icon;
        if (icon == NULL)
        {
            continue;
        }

        hidden_now = lv_obj_has_flag(icon, LV_OBJ_FLAG_HIDDEN);
        if (s_status_applied_charge != 0)
        {
            if (hidden_now)
            {
                lv_obj_clear_flag(icon, LV_OBJ_FLAG_HIDDEN);
            }
        }
        else if (!hidden_now)
        {
            lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ui_status_bar_refresh_battery_percent(void)
{
    size_t i;
    char battery_text[8];
    int percent = s_status_pending_battery_percent;

    if (percent < 0)
    {
        return;
    }
    if (percent > 100)
    {
        percent = 100;
    }

    s_status_applied_battery_percent = percent;
    rt_snprintf(battery_text, sizeof(battery_text), "%d%%", percent);

    for (i = 0; i < sizeof(s_status_bar_refs) / sizeof(s_status_bar_refs[0]); ++i)
    {
        lv_obj_t *label;
        lv_obj_t *fill;
        lv_coord_t target_fill_w;

        if (!s_status_bar_refs[i].used)
        {
            continue;
        }

        label = s_status_bar_refs[i].refs.battery_percent_label;
        if (label != NULL)
        {
            ui_status_bar_set_label_text(label, battery_text);
        }

        fill = s_status_bar_refs[i].refs.battery_arc;
        if (fill != NULL)
        {
            target_fill_w = ui_px_w((percent <= 0) ? 0 : ((percent * 30 + 99) / 100));
            if (lv_obj_get_width(fill) != target_fill_w)
            {
                lv_obj_set_width(fill, target_fill_w);
            }
        }
    }
}

static void ui_status_bar_async_refresh_charge_cb(void *user_data)
{
    LV_UNUSED(user_data);
    ui_status_bar_refresh_charging_icons();
}

static void ui_status_bar_async_refresh_battery_cb(void *user_data)
{
    LV_UNUSED(user_data);
    ui_status_bar_refresh_battery_percent();
}

static uint32_t ui_status_bar_next_refresh_delay_ms(void)
{
    date_time_t current_time;
    uint32_t delay_ms = 60000U;

    if (xiaozhi_time_get_current(&current_time) == RT_EOK)
    {
        int second = current_time.second;

        if (second < 0)
        {
            second = 0;
        }
        if (second > 59)
        {
            second = 59;
        }

        delay_ms = (uint32_t)(60 - second) * 1000U;
        if (delay_ms == 0U)
        {
            delay_ms = 60000U;
        }
    }

    return delay_ms;
}

static void ui_status_bar_refresh_thread_entry(void *parameter)
{
    LV_UNUSED(parameter);

    while (1)
    {
        uint32_t delay_ms = ui_status_bar_next_refresh_delay_ms();

        if (sleep_manager_is_sleeping())
        {
            if (delay_ms < 60000U)
            {
                delay_ms = 60000U;
            }
            rt_thread_mdelay(delay_ms);
            continue;
        }

        if (delay_ms < 1000U)
        {
            delay_ms = 1000U;
        }

        rt_thread_mdelay(delay_ms);

        if (sleep_manager_is_sleeping() ||
            ui_dispatch_get_active_screen() == UI_SCREEN_STANDBY)
        {
            continue;
        }

        ui_dispatch_request_time_refresh();
    }
}

static void ui_status_bar_ensure_refresh_thread(void)
{
    if (!s_status_bar_refresh_thread_started)
    {
        if (rt_thread_init(&s_status_bar_refresh_thread,
                           "ui_statu",
                           ui_status_bar_refresh_thread_entry,
                           NULL,
                           s_status_bar_refresh_thread_stack,
                           sizeof(s_status_bar_refresh_thread_stack),
                           UI_STATUS_BAR_REFRESH_THREAD_PRIORITY,
                           UI_STATUS_BAR_REFRESH_THREAD_TICK) == RT_EOK)
        {
            rt_thread_startup(&s_status_bar_refresh_thread);
            s_status_bar_refresh_thread_started = true;
        }
    }
}

void ui_status_bar_component_build(lv_obj_t *parent,
                                   xiaozhi_home_screen_refs_t *refs,
                                   bool enable_detail_touch,
                                   lv_event_cb_t detail_touch_cb)
{
    lv_obj_t *bar;
    lv_obj_t *right_box;
    lv_obj_t *bar_touch_zone;
    lv_obj_t *time_label;
    lv_obj_t *meta_label;
    lv_obj_t *battery_label;
    lv_obj_t *battery_fill;
    lv_obj_t *battery_body;
    lv_obj_t *battery_cap;
    lv_obj_t *charge_icon;
    lv_obj_t *bluetooth_img;
    lv_obj_t *network_img;
    lv_coord_t screen_width;

    if (parent == NULL)
    {
        return;
    }

    screen_width = ui_status_bar_screen_width();

    bar = lv_obj_create(parent);
    ui_status_bar_apply_basic_object_style(bar, false, 0, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 2, 0);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, screen_width, ui_px_h(UI_STATUS_BAR_HEIGHT));

    charge_icon = lv_obj_create(bar);
    ui_status_bar_apply_basic_object_style(charge_icon, false, 0, 0);
    lv_obj_set_pos(charge_icon, ui_px_x(446), ui_px_y(18));
    lv_obj_set_size(charge_icon, ui_px_w(18), ui_px_h(28));
    lv_obj_set_style_border_width(charge_icon, 0, 0);
    lv_obj_set_style_bg_opa(charge_icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(charge_icon, 0, 0);
    {
        lv_obj_t *part = lv_obj_create(charge_icon);
        ui_status_bar_apply_basic_object_style(part, true, 0, 0);
        lv_obj_set_style_border_width(part, 0, 0);
        lv_obj_set_pos(part, ui_px_x(7), ui_px_y(0));
        lv_obj_set_size(part, ui_px_w(7), ui_px_h(8));
    }
    {
        lv_obj_t *part = lv_obj_create(charge_icon);
        ui_status_bar_apply_basic_object_style(part, true, 0, 0);
        lv_obj_set_style_border_width(part, 0, 0);
        lv_obj_set_pos(part, ui_px_x(4), ui_px_y(7));
        lv_obj_set_size(part, ui_px_w(7), ui_px_h(8));
    }
    {
        lv_obj_t *part = lv_obj_create(charge_icon);
        ui_status_bar_apply_basic_object_style(part, true, 0, 0);
        lv_obj_set_style_border_width(part, 0, 0);
        lv_obj_set_pos(part, ui_px_x(8), ui_px_y(14));
        lv_obj_set_size(part, ui_px_w(7), ui_px_h(8));
    }
    {
        lv_obj_t *part = lv_obj_create(charge_icon);
        ui_status_bar_apply_basic_object_style(part, true, 0, 0);
        lv_obj_set_style_border_width(part, 0, 0);
        lv_obj_set_pos(part, ui_px_x(5), ui_px_y(20));
        lv_obj_set_size(part, ui_px_w(7), ui_px_h(8));
    }
    lv_obj_add_flag(charge_icon, LV_OBJ_FLAG_HIDDEN);

    time_label = ui_create_label(bar, "15:30", 14, 12, 100, 40, 34, LV_TEXT_ALIGN_LEFT, false, false);
    meta_label = ui_create_label(bar,
                                 ui_i18n_pick("2026/01/14\n星期三 23°C", "2026/01/14\nWed 23C"),
                                 116,
                                 14,
                                 178,
                                 38,
                                 18,
                                 LV_TEXT_ALIGN_LEFT,
                                 false,
                                 true);
    lv_obj_set_style_text_line_space(meta_label, 0, 0);

    right_box = lv_obj_create(bar);
    ui_status_bar_apply_basic_object_style(right_box, false, 0, 0);
    lv_obj_set_style_bg_opa(right_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_box, 0, 0);
    lv_obj_set_style_pad_all(right_box, 0, 0);
    lv_obj_set_pos(right_box, ui_px_x(386), ui_px_y(12));
    lv_obj_set_size(right_box, ui_px_w(128), ui_px_h(42));

    bluetooth_img = ui_create_image_slot(right_box, 0, 6, 24, 24);
    ui_img_set_src(bluetooth_img, &ble_icon_img);
    lv_obj_add_flag(bluetooth_img, LV_OBJ_FLAG_HIDDEN);

    network_img = ui_create_image_slot(right_box, 30, 6, 24, 24);
    ui_img_set_src(network_img, &network_icon_img);
    lv_obj_add_flag(network_img, LV_OBJ_FLAG_HIDDEN);

    battery_body = lv_obj_create(right_box);
    ui_status_bar_apply_basic_object_style(battery_body, true, ui_px_x(6), 0);
    lv_obj_set_style_bg_opa(battery_body, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(battery_body, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(battery_body, 2, 0);
    lv_obj_set_style_border_color(battery_body, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(battery_body, 0, 0);
    lv_obj_set_pos(battery_body, ui_px_x(62), ui_px_y(5));
    lv_obj_set_size(battery_body, ui_px_w(54), ui_px_h(30));

    battery_fill = lv_obj_create(battery_body);
    ui_status_bar_apply_basic_object_style(battery_fill, true, 0, 0);
    lv_obj_set_style_radius(battery_fill, ui_px_x(3), 0);
    lv_obj_set_style_border_width(battery_fill, 0, 0);
    lv_obj_set_style_bg_opa(battery_fill, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(battery_fill, lv_color_hex(0x000000), 0);
    lv_obj_set_pos(battery_fill, ui_px_x(3), ui_px_y(23));
    lv_obj_set_size(battery_fill, ui_px_w(16), ui_px_h(4));

    battery_cap = lv_obj_create(right_box);
    ui_status_bar_apply_basic_object_style(battery_cap, true, ui_px_x(2), 0);
    lv_obj_set_style_border_width(battery_cap, 0, 0);
    lv_obj_set_pos(battery_cap, ui_px_x(116), ui_px_y(14));
    lv_obj_set_size(battery_cap, ui_px_w(6), ui_px_h(12));

    battery_label = ui_create_label(battery_body,
                                    "80%",
                                    0,
                                    4,
                                    54,
                                    20,
                                    18,
                                    LV_TEXT_ALIGN_CENTER,
                                    false,
                                    false);
    lv_obj_set_style_text_color(battery_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_align(battery_label, LV_TEXT_ALIGN_CENTER, 0);

    bar_touch_zone = lv_obj_create(bar);
    ui_status_bar_apply_basic_object_style(bar_touch_zone, false, 0, 0);
    lv_obj_set_style_bg_opa(bar_touch_zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar_touch_zone, 0, 0);
    lv_obj_set_pos(bar_touch_zone, 0, 0);
    lv_obj_set_size(bar_touch_zone, screen_width, ui_px_h(UI_STATUS_BAR_HEIGHT));
    if (enable_detail_touch && detail_touch_cb != NULL)
    {
        lv_obj_add_flag(bar_touch_zone, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(bar_touch_zone, detail_touch_cb, LV_EVENT_CLICKED, NULL);
    }

    if (refs != NULL)
    {
        lv_obj_t *hidden_refs = lv_obj_create(parent);

        memset(refs, 0, sizeof(*refs));
        refs->screen = parent;
        refs->time_label = time_label;
        refs->meta_label = meta_label;
        refs->bluetooth_icon = bluetooth_img;
        refs->network_icon = network_img;
        refs->ec800_status_label = NULL;
        refs->battery_arc = battery_fill;
        refs->battery_percent_label = battery_label;
        refs->standby_charging_icon = charge_icon;

        ui_status_bar_apply_basic_object_style(hidden_refs, false, 0, 0);
        lv_obj_set_size(hidden_refs, 1, 1);
        lv_obj_set_pos(hidden_refs, -10, -10);
        lv_obj_add_flag(hidden_refs, LV_OBJ_FLAG_HIDDEN);

        refs->weather_icon = lv_img_create(hidden_refs);
        refs->ui_Label_ip = ui_create_hidden_label(hidden_refs);
        refs->tf_dir_label = ui_create_hidden_label(hidden_refs);
        ui_status_bar_register_refs(parent, refs);
    }

    ui_status_bar_ensure_refresh_thread();
    ui_status_bar_component_force_refresh();
}

const xiaozhi_home_screen_refs_t *ui_status_bar_component_refs_get(lv_obj_t *screen)
{
    size_t i;

    if (screen == NULL)
    {
        return NULL;
    }

    for (i = 0; i < sizeof(s_status_bar_refs) / sizeof(s_status_bar_refs[0]); ++i)
    {
        if (s_status_bar_refs[i].used && s_status_bar_refs[i].screen == screen)
        {
            return &s_status_bar_refs[i].refs;
        }
    }

    return NULL;
}

void ui_status_bar_component_refs_unregister(lv_obj_t *screen)
{
    size_t i;

    if (screen == NULL)
    {
        return;
    }

    for (i = 0; i < sizeof(s_status_bar_refs) / sizeof(s_status_bar_refs[0]); ++i)
    {
        if (s_status_bar_refs[i].screen == screen)
        {
            s_status_bar_refs[i].screen = NULL;
            memset(&s_status_bar_refs[i].refs, 0, sizeof(s_status_bar_refs[i].refs));
            s_status_bar_refs[i].used = false;
        }
    }
}

void ui_status_bar_component_refresh(void)
{
    ui_status_bar_snapshot_t old_snapshot;
    ui_status_bar_snapshot_t snapshot;
    bool time_changed;
    bool battery_changed;
    bool charge_changed;
    bool connection_changed;

    old_snapshot = s_status_bar_snapshot;
    ui_status_bar_capture_snapshot(&snapshot);
    if (ui_status_bar_snapshot_equal(&old_snapshot, &snapshot))
    {
        return;
    }

    s_status_bar_snapshot = snapshot;

    time_changed = (old_snapshot.time_valid != snapshot.time_valid) ||
                   (old_snapshot.year != snapshot.year) ||
                   (old_snapshot.month != snapshot.month) ||
                   (old_snapshot.day != snapshot.day) ||
                   (old_snapshot.hour != snapshot.hour) ||
                   (old_snapshot.minute != snapshot.minute) ||
                   (old_snapshot.weather_available != snapshot.weather_available) ||
                   (old_snapshot.weather_temperature != snapshot.weather_temperature);
    battery_changed = old_snapshot.battery_percent != snapshot.battery_percent;
    charge_changed = old_snapshot.charge_state != snapshot.charge_state;
    connection_changed = (old_snapshot.bt_visual_state != snapshot.bt_visual_state) ||
                         (old_snapshot.network_visual_state != snapshot.network_visual_state) ||
                         (old_snapshot.active_link != snapshot.active_link) ||
                         (old_snapshot.bt_enabled != snapshot.bt_enabled) ||
                         (old_snapshot.net_4g_enabled != snapshot.net_4g_enabled) ||
                         (strncmp(old_snapshot.network_detail,
                                  snapshot.network_detail,
                                  sizeof(old_snapshot.network_detail)) != 0);

    if (time_changed)
    {
        ui_status_bar_refresh_datetime();
    }
    if (battery_changed)
    {
        s_status_pending_battery_percent = snapshot.battery_percent;
        ui_status_bar_refresh_battery_percent();
    }
    if (charge_changed)
    {
        s_status_pending_charge = snapshot.charge_state;
        ui_status_bar_refresh_charging_icons();
    }
    if (connection_changed)
    {
        ui_status_bar_refresh_connection_icons(false);
    }
}

void ui_status_bar_component_force_refresh(void)
{
    memset(&s_status_bar_snapshot, 0, sizeof(s_status_bar_snapshot));
    s_status_bar_snapshot.valid = false;
    s_status_last_bt_icon_state = -1;
    s_status_last_network_icon_state = -1;
    s_status_last_net_4g_enabled = false;
    s_status_last_network_text[0] = '\0';
    ui_status_bar_component_refresh();
}

void ui_status_bar_component_update_charge(uint8_t is_charging)
{
    int pending = is_charging ? 1 : 0;

    if (s_status_pending_charge == pending ||
        (s_status_pending_charge < 0 && s_status_applied_charge == pending))
    {
        return;
    }

    s_status_pending_charge = pending;
    lv_async_call(ui_status_bar_async_refresh_charge_cb, NULL);
}

void ui_status_bar_component_update_battery_percent(uint8_t percent)
{
    if (percent > 100U)
    {
        percent = 100U;
    }

    if (s_status_pending_battery_percent == (int)percent ||
        (s_status_pending_battery_percent < 0 &&
         s_status_applied_battery_percent == (int)percent))
    {
        return;
    }

    s_status_pending_battery_percent = (int)percent;
    lv_async_call(ui_status_bar_async_refresh_battery_cb, NULL);
}
