/*
 * SPDX-FileCopyrightText: 2024-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "rtthread.h"
#include "rtdevice.h"
#include "xiaozhi_client_public.h"
#include "lwip/api.h"
#include "lwip/dns.h"
#include <webclient.h>
#include <cJSON.h>
#include "ulog.h"
#include "ntp.h"
#include "weather.h"
#include "littlevgl2rtt.h"
#include "ui.h"
#include "ui/ui_dispatch.h"
#include "ui_helpers.h"
#include <stdlib.h>
#include <stdio.h>
#include "lv_image_dsc.h"

#define XIAOZHI_CHINA_TIMEZONE_MINUTES (-480)

static volatile int g_weather_sync_in_progress = 0;  // 天气同步进行标志
static volatile int g_ntp_sync_in_progress = 0;      // NTP同步进行标志
static volatile int g_network_available = 0;
static volatile int g_weather_service_started = 0;
static rt_thread_t g_weather_service_thread = RT_NULL;
static rt_event_t g_weather_service_event = RT_NULL;
static bool g_weather_home_entry_enabled = true;

#define WEATHER_SERVICE_STACK_SIZE 6144
#define WEATHER_SERVICE_PRIORITY   21
#define WEATHER_SERVICE_TICK       10
#define WEATHER_SERVICE_EVT_REFRESH (1UL << 0)
#define WEATHER_SERVICE_POLL_MS_SUCCESS (30 * 60 * 1000U)
#define WEATHER_SERVICE_POLL_MS_RETRY   (5 * 60 * 1000U)

extern const lv_image_dsc_t w0;   // 晴
extern const lv_image_dsc_t w1;   // 多云
extern const lv_image_dsc_t w2;   // 阴
extern const lv_image_dsc_t w3;   // 阵雨
extern const lv_image_dsc_t w4;   // 雷阵雨
extern const lv_image_dsc_t w5;   // 雷阵雨伴有冰雹
extern const lv_image_dsc_t w6;   // 雨夹雪
extern const lv_image_dsc_t w7;   // 小雨
extern const lv_image_dsc_t w8;   // 中雨
extern const lv_image_dsc_t w9;   // 大雨
extern const lv_image_dsc_t w10;  // 暴雨
extern const lv_image_dsc_t w11;  // 大暴雨
extern const lv_image_dsc_t w12;  // 特大暴雨
extern const lv_image_dsc_t w13;  // 阵雪
extern const lv_image_dsc_t w14;  // 小雪
extern const lv_image_dsc_t w15;  // 中雪
extern const lv_image_dsc_t w16;  // 大雪
extern const lv_image_dsc_t w17;  // 暴雪
extern const lv_image_dsc_t w18;  // 雾
extern const lv_image_dsc_t w19;  // 冻雨
extern const lv_image_dsc_t w20;  // 沙尘暴
extern const lv_image_dsc_t w21;  // 小到中雨
extern const lv_image_dsc_t w22;  // 中到大雨
extern const lv_image_dsc_t w23;  // 大到暴雨
extern const lv_image_dsc_t w24;  // 暴雨到大暴雨
extern const lv_image_dsc_t w25;  // 大暴雨到特大暴雨
extern const lv_image_dsc_t w26;  // 小到中雪
extern const lv_image_dsc_t w27;  // 中到大雪
extern const lv_image_dsc_t w28;  // 大到暴雪
extern const lv_image_dsc_t w29;  // 浮尘
extern const lv_image_dsc_t w30;  // 扬沙
extern const lv_image_dsc_t w31;  // 强沙尘暴
extern const lv_image_dsc_t w32;  // 浓雾
extern const lv_image_dsc_t w33;  // 龙卷风
extern const lv_image_dsc_t w34;  // 弱高吹雪
extern const lv_image_dsc_t w35;  // 轻雾
extern const lv_image_dsc_t w36;  // 霾
extern const lv_image_dsc_t w37;  // 小雨转中雨
extern const lv_image_dsc_t w38;  // 中雨转大雨
extern const lv_image_dsc_t w99;  // 未知天气


// 天气API配置 - 心知天气免费版
#define WEATHER_API_KEY "SO23_Gmly2oK3kMf4" // 请替换为你的API密钥
#define WEATHER_API_HOST "api.seniverse.com"
#define WEATHER_API_URI                                                        \
    "/v3/weather/now.json?key=%s&location=%s&language=%s&unit=c"
#define WEATHER_LOCATION "ip"      // 默认城市，可以是城市名或经纬度
#define WEATHER_LANGUAGE "zh-Hans" // 中文简体
#define WEATHER_HEADER_BUFSZ 1024
#define WEATHER_RESP_BUFSZ 1024
#define WEATHER_URL_LEN_MAX 512


rt_device_t g_rtc_device = RT_NULL;
date_time_t g_current_time = {0};
weather_info_t g_current_weather = {0};


// 周几的字符串数组
static const char *weekday_names[] = {"周日", "周一", "周二", "周三",
                                      "周四", "周五", "周六"};

// 新增：月份的中文字符串数组
static const char *month_names[] = {"",     "一月",   "二月",  "三月", "四月",
                                    "五月", "六月",   "七月",  "八月", "九月",
                                    "十月", "十一月", "十二月"};
// 添加NTP服务器列表
static const char *ntp_servers[] = {"ntp.aliyun.com", "time.windows.com",
                                    "pool.ntp.org", "cn.pool.ntp.org"};

void xiaozhi_time_use_china_timezone(void)
{
    struct timezone tz;

    tz.tz_minuteswest = XIAOZHI_CHINA_TIMEZONE_MINUTES;
    tz.tz_dsttime = 0;
    tz_set(&tz);
}

static const lv_image_dsc_t *weather_icon_from_code(const char *code)
{
    long value;
    char *endptr = RT_NULL;

    if (code == RT_NULL || code[0] == '\0')
    {
        return &w99;
    }

    value = strtol(code, &endptr, 10);
    if (endptr == code || *endptr != '\0')
    {
        return &w99;
    }

    switch (value)
    {
    case 0: return &w0;
    case 1: return &w1;
    case 2: return &w2;
    case 3: return &w3;
    case 4: return &w4;
    case 5: return &w5;
    case 6: return &w6;
    case 7: return &w7;
    case 8: return &w8;
    case 9: return &w9;
    case 10: return &w10;
    case 11: return &w11;
    case 12: return &w12;
    case 13: return &w13;
    case 14: return &w14;
    case 15: return &w15;
    case 16: return &w16;
    case 17: return &w17;
    case 18: return &w18;
    case 19: return &w19;
    case 20: return &w20;
    case 21: return &w21;
    case 22: return &w22;
    case 23: return &w23;
    case 24: return &w24;
    case 25: return &w25;
    case 26: return &w26;
    case 27: return &w27;
    case 28: return &w28;
    case 29: return &w29;
    case 30: return &w30;
    case 31: return &w31;
    case 32: return &w32;
    case 33: return &w33;
    case 34: return &w34;
    case 35: return &w35;
    case 36: return &w36;
    case 37: return &w37;
    case 38: return &w38;
    default: return &w99;
    }
}

const lv_image_dsc_t *xiaozhi_weather_get_icon(const char *code)
{
    return weather_icon_from_code(code);
}

int xiaozhi_weather_peek(weather_info_t *weather_info)
{
    if (weather_info == RT_NULL)
    {
        return -RT_EINVAL;
    }

    *weather_info = g_current_weather;
    return (g_current_weather.last_update > 0) ? RT_EOK : -RT_ERROR;
}

bool xiaozhi_weather_is_home_entry_enabled(void)
{
    return g_weather_home_entry_enabled;
}

void xiaozhi_weather_set_home_entry_enabled(bool enabled)
{
    g_weather_home_entry_enabled = enabled;
}

static void weather_service_thread_entry(void *parameter)
{
    rt_uint32_t events = 0U;
    rt_int32_t wait_ms = 1000;

    LV_UNUSED(parameter);
    xiaozhi_time_weather_init();

    while (1)
    {
        if (g_weather_service_event != RT_NULL &&
            rt_event_recv(g_weather_service_event,
                          WEATHER_SERVICE_EVT_REFRESH,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          rt_tick_from_millisecond((rt_uint32_t)wait_ms),
                          &events) == RT_EOK)
        {
            LV_UNUSED(events);
        }

        xiaozhi_time_weather();
        wait_ms = (g_current_weather.last_update > 0) ?
                  WEATHER_SERVICE_POLL_MS_SUCCESS :
                  WEATHER_SERVICE_POLL_MS_RETRY;
    }
}

int xiaozhi_weather_service_start(void)
{
    if (g_weather_service_started)
    {
        return RT_EOK;
    }

    if (g_weather_service_event == RT_NULL)
    {
        g_weather_service_event = rt_event_create("weather", RT_IPC_FLAG_FIFO);
        if (g_weather_service_event == RT_NULL)
        {
            return -RT_ENOMEM;
        }
    }

    g_weather_service_thread =
        rt_thread_create("weather_sync",
                         weather_service_thread_entry,
                         RT_NULL,
                         WEATHER_SERVICE_STACK_SIZE,
                         WEATHER_SERVICE_PRIORITY,
                         WEATHER_SERVICE_TICK);
    if (g_weather_service_thread == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    g_weather_service_started = 1;
    rt_thread_startup(g_weather_service_thread);
    return RT_EOK;
}

void xiaozhi_weather_request_refresh(void)
{
    if (xiaozhi_weather_service_start() != RT_EOK)
    {
        return;
    }

    if (g_weather_service_event != RT_NULL)
    {
        rt_event_send(g_weather_service_event, WEATHER_SERVICE_EVT_REFRESH);
    }
}

/**
 * @brief 获取周几的中文字符串
 */
const char *xiaozhi_time_get_weekday_str(int weekday)
{
    if (weekday >= 0 && weekday <= 6)
    {
        return weekday_names[weekday];
    }
    return "未知";
}

/**
 * @brief 格式化时间和日期字符串
 */
void xiaozhi_time_format_strings(date_time_t *time_info)
{
    if (!time_info)
        return;

    // 安全检查，确保值在合理范围内
    if (time_info->year < 1900 || time_info->year > 2100)
    {
        LOG_W("Invalid year: %d, using default", time_info->year);
        time_info->year = 2024;
    }
    if (time_info->month < 1 || time_info->month > 12)
    {
        time_info->month = 1;
    }
    if (time_info->day < 1 || time_info->day > 31)
    {
        time_info->day = 1;
    }
    if (time_info->hour < 0 || time_info->hour > 23)
    {
        time_info->hour = 0;
    }
    if (time_info->minute < 0 || time_info->minute > 59)
    {
        time_info->minute = 0;
    }
    if (time_info->second < 0 || time_info->second > 59)
    {
        time_info->second = 0;
    }
    if (time_info->weekday < 0 || time_info->weekday > 6)
    {
        time_info->weekday = 0;
    }

    // 格式化日期字符串: "2024年12月25日 周三"
    rt_snprintf(time_info->date_str, sizeof(time_info->date_str),
                "%04d年%02d月%02d日 %s", time_info->year, time_info->month,
                time_info->day,
                xiaozhi_time_get_weekday_str(time_info->weekday));

    // 格式化时间字符串: "14:30:25"
    rt_snprintf(time_info->time_str, sizeof(time_info->time_str),
                "%02d:%02d:%02d", time_info->hour, time_info->minute,
                time_info->second);


}





/**
 * @brief 获取当前时间信息
 */
int xiaozhi_time_get_current(date_time_t *time_info)
{
    if (!time_info)
        return -RT_ERROR;

    time_t now;
    struct tm tm_storage;
    struct tm *tm_info;

    now = time(RT_NULL);
    xiaozhi_time_use_china_timezone();

    tm_info = localtime_r(&now, &tm_storage);
    if (!tm_info)
    {
        tm_info = gmtime(&now);
        if (tm_info)
        {
            tm_info->tm_hour += 8;
            if (tm_info->tm_hour >= 24)
            {
                tm_info->tm_hour -= 24;
                tm_info->tm_mday += 1;
            }
        }
        else
        {
            rt_kprintf("Time conversion failed, using defaults\n");
            time_info->year = 2024;
            time_info->month = 1;
            time_info->day = 1;
            time_info->hour = 12;
            time_info->minute = 0;
            time_info->second = 0;
            time_info->weekday = 1;
            xiaozhi_time_format_strings(time_info);
            return RT_EOK;
        }
    }

    // 填充时间结构体
    time_info->year = tm_info->tm_year + 1900;
    time_info->month = tm_info->tm_mon + 1;
    time_info->day = tm_info->tm_mday;
    time_info->hour = tm_info->tm_hour;
    time_info->minute = tm_info->tm_min;
    time_info->second = tm_info->tm_sec;
    time_info->weekday = tm_info->tm_wday;

    // 格式化字符串
    xiaozhi_time_format_strings(time_info);

    return RT_EOK;
}


int xiaozhi_weather_get(weather_info_t *weather_info)
{
    if (!weather_info)
        return -RT_ERROR;



        // 检查是否有同步正在进行中，避免并发调用
    if (g_weather_sync_in_progress) {
        LOG_W("Weather sync already in progress, skipping...");
        return -RT_EBUSY;
    }

    // 设置同步进行标志
    g_weather_sync_in_progress = 1;


    if (check_internet_access() != 1)
    {
        LOG_W("network not ready, cannot get weather");
        g_network_available = 0;
            // 清除同步进行标志
        g_weather_sync_in_progress = 0;
        return -RT_ERROR;
    }



    int ret = -RT_ERROR;
    struct webclient_session *session = RT_NULL;
    char *weather_url = RT_NULL;
    char *buffer = RT_NULL;
    int resp_status;
    int content_length = -1, bytes_read = 0;
    int content_pos = 0;


    // 分配URL缓冲区
    weather_url = rt_calloc(1, WEATHER_URL_LEN_MAX);
    if (weather_url == RT_NULL)
    {
        LOG_E("No memory for weather_url!");
        goto __exit;
    }

    // 拼接GET网址
    rt_snprintf(weather_url, WEATHER_URL_LEN_MAX, "http://%s" WEATHER_API_URI,
                WEATHER_API_HOST, WEATHER_API_KEY, WEATHER_LOCATION,
                WEATHER_LANGUAGE);


    // 创建会话
    session = webclient_session_create(WEATHER_HEADER_BUFSZ);
    if (session == RT_NULL)
    {
        LOG_E("No memory for weather session!");
        goto __exit;
    }

    // 发送GET请求
    if ((resp_status = webclient_get(session, weather_url)) != 200)
    {
        LOG_E("Weather API request failed, response(%d) error.", resp_status);
        goto __exit;
    }

    // 分配接收缓冲区
    buffer = rt_calloc(1, WEATHER_RESP_BUFSZ);
    if (buffer == RT_NULL)
    {
        LOG_E("No memory for weather response buffer!");
        goto __exit;
    }

    // 读取响应内容
    content_length = webclient_content_length_get(session);
    if (content_length > 0)
    {
        do
        {
            bytes_read =
                webclient_read(session, buffer + content_pos,
                               content_length - content_pos >
                                       WEATHER_RESP_BUFSZ - content_pos - 1
                                   ? WEATHER_RESP_BUFSZ - content_pos - 1
                                   : content_length - content_pos);
            if (bytes_read <= 0)
            {
                break;
            }
            content_pos += bytes_read;
        } while (content_pos < content_length &&
                 content_pos < WEATHER_RESP_BUFSZ - 1);

        buffer[content_pos] = '\0'; // 确保字符串结束


        // 解析JSON响应
        cJSON *root = cJSON_Parse(buffer);
        if (!root)
        {
            LOG_E("Failed to parse weather JSON: %s", cJSON_GetErrorPtr());
            goto __exit;
        }

        // 解析results数组
        cJSON *results = cJSON_GetObjectItem(root, "results");
        if (!results || !cJSON_IsArray(results) ||
            cJSON_GetArraySize(results) == 0)
        {
            LOG_E("Invalid weather response: no results array");
            cJSON_Delete(root);
            goto __exit;
        }

        // 获取第一个结果
        cJSON *result = cJSON_GetArrayItem(results, 0);
        if (!result)
        {
            LOG_E("Invalid weather response: empty results");
            cJSON_Delete(root);
            goto __exit;
        }

        // 解析location信息
        cJSON *location = cJSON_GetObjectItem(result, "location");
        if (location)
        {
            cJSON *name = cJSON_GetObjectItem(location, "name");
            if (name && cJSON_IsString(name))
            {
                strncpy(weather_info->location, name->valuestring,
                        sizeof(weather_info->location) - 1);
                weather_info->location[sizeof(weather_info->location) - 1] =
                    '\0';
            }
        }

        // 解析now信息
        cJSON *now = cJSON_GetObjectItem(result, "now");
        if (!now)
        {
            LOG_E("Invalid weather response: no now object");
            cJSON_Delete(root);
            goto __exit;
        }

        // 解析天气现象文字
        cJSON *text = cJSON_GetObjectItem(now, "text");
        if (text && cJSON_IsString(text))
        {
            strncpy(weather_info->text, text->valuestring,
                    sizeof(weather_info->text) - 1);
            weather_info->text[sizeof(weather_info->text) - 1] = '\0';
        }

        // 解析天气现象代码
        cJSON *code = cJSON_GetObjectItem(now, "code");
        if (code && cJSON_IsString(code))
        {
            strncpy(weather_info->code, code->valuestring,
                    sizeof(weather_info->code) - 1);
            weather_info->code[sizeof(weather_info->code) - 1] = '\0';
        }

        // 解析温度
        cJSON *temperature = cJSON_GetObjectItem(now, "temperature");
        if (temperature && cJSON_IsString(temperature))
        {
            weather_info->temperature = atoi(temperature->valuestring);
        }

        {
            cJSON *feels_like = cJSON_GetObjectItem(now, "feels_like");
            if (feels_like && cJSON_IsString(feels_like))
            {
                weather_info->feels_like = atoi(feels_like->valuestring);
            }
        }

        {
            cJSON *humidity = cJSON_GetObjectItem(now, "humidity");
            if (humidity && cJSON_IsString(humidity))
            {
                weather_info->humidity = atoi(humidity->valuestring);
            }
        }

        {
            cJSON *wind_direction = cJSON_GetObjectItem(now, "wind_direction");
            if (wind_direction && cJSON_IsString(wind_direction))
            {
                strncpy(weather_info->wind_direction,
                        wind_direction->valuestring,
                        sizeof(weather_info->wind_direction) - 1);
                weather_info->wind_direction[sizeof(weather_info->wind_direction) - 1] = '\0';
            }
        }

        {
            cJSON *wind_scale = cJSON_GetObjectItem(now, "wind_scale");
            if (wind_scale && cJSON_IsString(wind_scale))
            {
                strncpy(weather_info->wind_scale,
                        wind_scale->valuestring,
                        sizeof(weather_info->wind_scale) - 1);
                weather_info->wind_scale[sizeof(weather_info->wind_scale) - 1] = '\0';
            }
        }

        // 记录更新时间
        weather_info->last_update = time(RT_NULL);

        cJSON_Delete(root);
        LOG_E("天气数据同步成功");
        g_network_available = 1;
        
        ret = RT_EOK;
    }
    else
    {
        LOG_E("No weather content received");
    }

__exit:
    if (weather_url != RT_NULL)
    {
        rt_free(weather_url);
    }

    if (session != RT_NULL)
    {
        LOCK_TCPIP_CORE();
        webclient_close(session);
        UNLOCK_TCPIP_CORE();
    }

    if (buffer != RT_NULL)
    {
        rt_free(buffer);
    }

    if (ret != RT_EOK)
    {
        LOG_E("天气同步失败\n");
        g_network_available = 0;
    }
    // 清除同步进行标志
    g_weather_sync_in_progress = 0;

    return ret;
}

void time_ui_update_callback(void)
{
    static int last_year = -1;
    static int last_month = -1;
    static int last_day = -1;
    static int last_bt_icon_state = -1;
    static int last_network_connected = -1;

    static int last_hour_tens = -1;
    static int last_hour_units = -1;
    static int last_minute_tens = -1;
    static int last_minute_units = -1;
    static int last_second = -1;//秒

    if (ui_status_panel_is_visible())
    {
        return;
    }

    ui_refresh_global_status_bar();

    // 获取最新时间
    if (xiaozhi_time_get_current(&g_current_time) != RT_EOK)
    {
        return;
    }

    const xiaozhi_home_screen_refs_t *refs = ui_home_screen_refs_get();
    lv_obj_t *hour_tens_img = refs != NULL ? refs->hour_tens_img : NULL;
    lv_obj_t *hour_units_img = refs != NULL ? refs->hour_units_img : NULL;
    lv_obj_t *minute_tens_img = refs != NULL ? refs->minute_tens_img : NULL;
    lv_obj_t *minute_units_img = refs != NULL ? refs->minute_units_img : NULL;
    lv_obj_t *ui_Label_second = refs != NULL ? refs->ui_Label_second : NULL;
    lv_obj_t *ui_Label_day = refs != NULL ? refs->ui_Label_day : NULL;
    lv_obj_t *ui_Label_year = refs != NULL ? refs->ui_Label_year : NULL;
    lv_obj_t *bluetooth_icon = refs != NULL ? refs->bluetooth_icon : NULL;
    lv_obj_t *network_icon = refs != NULL ? refs->network_icon : NULL;

    // 更新待机界面的时间显示
    // 根据小时和分钟更新数字图片
    // 更新小时显示
    int hour_tens = g_current_time.hour / 10;
    int hour_units = g_current_time.hour % 10;
    
    // 更新分钟显示
    int minute_tens = g_current_time.minute / 10;
    int minute_units = g_current_time.minute % 10;
    
    // 根据数字更新对应的图片资源
    extern const lv_image_dsc_t img_0, img_1, img_2, img_3, img_4, img_5, img_6, img_7, img_8, img_9;
    const lv_image_dsc_t* hour_tens_img_src[] = {&img_0, &img_1, &img_2, &img_3, &img_4, &img_5, &img_6, &img_7, &img_8, &img_9};
    const lv_image_dsc_t* hour_units_img_src[] = {&img_0, &img_1, &img_2, &img_3, &img_4, &img_5, &img_6, &img_7, &img_8, &img_9};
    const lv_image_dsc_t* minute_tens_img_src[] = {&img_0, &img_1, &img_2, &img_3, &img_4, &img_5, &img_6, &img_7, &img_8, &img_9};
    const lv_image_dsc_t* minute_units_img_src[] = {&img_0, &img_1, &img_2, &img_3, &img_4, &img_5, &img_6, &img_7, &img_8, &img_9};
    
        // 只在小时十位数变化时更新
    if (hour_tens != last_hour_tens) {
        if (hour_tens_img) lv_img_set_src(hour_tens_img, hour_tens_img_src[hour_tens]);
        last_hour_tens = hour_tens;
    }
    
    // 只在小时个位数变化时更新
    if (hour_units != last_hour_units) {
        if (hour_units_img) lv_img_set_src(hour_units_img, hour_units_img_src[hour_units]);
        last_hour_units = hour_units;
    }
    
    // 只在分钟十位数变化时更新
    if (minute_tens != last_minute_tens) {
        if (minute_tens_img) lv_img_set_src(minute_tens_img, minute_tens_img_src[minute_tens]);
        last_minute_tens = minute_tens;
    }
    
    // 只在分钟个位数变化时更新
    if (minute_units != last_minute_units) {
        if (minute_units_img) lv_img_set_src(minute_units_img, minute_units_img_src[minute_units]);
        last_minute_units = minute_units;
    }



    // 更新待机界面秒
    if (g_current_time.second != last_second) {
        if (ui_Label_second) {
            char second_text[8];
            snprintf(second_text, sizeof(second_text), "%02d", g_current_time.second);
            lv_label_set_text(ui_Label_second, second_text);
        }
        last_second = g_current_time.second;
    }

  // 更新年份显示
    if (g_current_time.year != last_year) {
        if (ui_Label_year) {
            char year_text[8];
            snprintf(year_text, sizeof(year_text), "%d", g_current_time.year);
            lv_label_set_text(ui_Label_year, year_text);
        }
    }
    // 更新月日显示
    if (g_current_time.month != last_month || g_current_time.day != last_day) {
        if (ui_Label_day) {
            char date_text[8];
            snprintf(date_text, sizeof(date_text), "%02d%02d", g_current_time.month, g_current_time.day);
            lv_label_set_text(ui_Label_day, date_text);
        }
    }

   // 更新蓝牙和网络图标（仅在状态变化时更新）
    extern const lv_image_dsc_t ble_icon_img_close;
    extern const lv_image_dsc_t network_icon_img;
    extern const lv_image_dsc_t network_icon_img_close;

    if (last_bt_icon_state != 0) {
        if (bluetooth_icon) {
            lv_img_set_src(bluetooth_icon, &ble_icon_img_close);
        }
        last_bt_icon_state = 0;
    }

    if (g_network_available != last_network_connected) {
        if (network_icon) {
            if (g_network_available) {
                lv_img_set_src(network_icon, &network_icon_img);
            } else {
                lv_img_set_src(network_icon, &network_icon_img_close);
            }
        }
        last_network_connected = g_network_available;
    }


}

void update_xiaozhi_ui_time(void *parameter)
{
    (void)parameter;
    ui_dispatch_request_time_refresh();
}


void weather_ui_update_callback(void)
{
    const xiaozhi_home_screen_refs_t *refs = ui_home_screen_refs_get();
    lv_obj_t *ui_Label_ip = refs != NULL ? refs->ui_Label_ip : NULL;
    lv_obj_t *last_time = refs != NULL ? refs->last_time : NULL;
    lv_obj_t *weather_icon = refs != NULL ? refs->weather_icon : NULL;

    if (ui_status_panel_is_visible())
    {
        return;
    }

    ui_refresh_global_status_bar();

    // 更新天气信息
    // 更新温度显示 (使用新UI中的ui_Label_ip对象)
    if (ui_Label_ip) {
        LOG_W("location%d\n",g_current_weather.location);
        char temp_text[32];
        // 根据天气结构体中的城市和温度信息显示
        snprintf(temp_text, sizeof(temp_text), "%.10s.%d°C", 
                 g_current_weather.location, g_current_weather.temperature);
        lv_label_set_text(ui_Label_ip, temp_text);
    }
    
    // 更新天气图标 (根据天气代码更新图标)
    if (weather_icon) {
        lv_img_set_src(weather_icon, weather_icon_from_code(g_current_weather.code));
    }
    
    // 更新上次更新时间显示 (使用新UI中的last_time对象)
    if (last_time && g_current_weather.last_update > 0) {
        struct tm *last_update_tm = localtime(&g_current_weather.last_update);
        if (last_update_tm) {
            char last_update_text[16];
            snprintf(last_update_text, sizeof(last_update_text), "%02d:%02d", 
                     last_update_tm->tm_hour, last_update_tm->tm_min);
            lv_label_set_text(last_time, last_update_text);
            LOG_I("last_update_text:%s",last_update_text);
        }
    }

    if (ui_Weather != NULL)
    {
        ui_Weather_screen_refresh();
    }
}

void update_xiaozhi_ui_weather(void *parameter)
{
    (void)parameter;
    ui_dispatch_request_weather_refresh();
}



/**
 * @brief NTP时间同步（蓝牙对时） 写入RTC
 */
int xiaozhi_ntp_sync(void)
{
    // 检查是否有同步正在进行中，避免并发调用
    if (g_ntp_sync_in_progress) {
        LOG_W("NTP sync already in progress, skipping...");
        return -RT_EBUSY;
    }

    // 设置同步进行标志
    g_ntp_sync_in_progress = 1;

    if (check_internet_access() != 1)
    {
        LOG_W("network not ready, cannot sync time");
        g_network_available = 0;
        // 清除同步进行标志
        g_ntp_sync_in_progress = 0;
        return -RT_ERROR;
    }


    time_t cur_time = 0;
    int sync_success = 0;

    // 尝试多个NTP服务器
    for (int i = 0; i < sizeof(ntp_servers) / sizeof(ntp_servers[0]); i++)
    {
        rt_kprintf("Trying NTP server: %s\n", ntp_servers[i]);

#ifdef PKG_USING_NETUTILS
        // 获取服务时间并设置rtc时间
        cur_time = ntp_sync_to_rtc(ntp_servers[i]);
        if (cur_time > 1000000000)// 基本的时间有效性检查（大约是2001年之后）
        { 
            sync_success = 1;
            
        }
#else
        // 如果没有NTP支持，使用系统时间
        LOG_W("NTP client not available, using system time");
        cur_time = time(RT_NULL);
        if (cur_time > 1000000000)
        { // 基本的时间有效性检查
            sync_success = 1;
        }
#endif

        if (sync_success)
        {
            // 验证RTC时间是否正确设置
            time_t verify_time = 0;
            rt_err_t result = rt_device_control(
                g_rtc_device, RT_DEVICE_CTRL_RTC_GET_TIME, &verify_time);
            if (result == RT_EOK)
            {
                LOG_I("RTC time verification successful: %ld", (long)verify_time);
                // 检查设置的时间和获取的时间是否一致
                if (verify_time == cur_time) {
                    LOG_I("RTC time matches NTP time");
                } else {
                    LOG_W("RTC time mismatch. NTP: %ld, RTC: %ld", (long)cur_time, (long)verify_time);
                }
            }
            else
            {
                LOG_E("Failed to verify RTC time: %d", result);
            }
            // 清除同步进行标志
            g_network_available = 1;
            g_ntp_sync_in_progress = 0;
            return RT_EOK;
        }

        rt_thread_mdelay(1000); // 等待1秒再尝试下一个服务器
    }
    // 清除同步进行标志
    g_network_available = 0;
    g_ntp_sync_in_progress = 0;
    return -RT_ERROR;
}

void xiaozhi_time_weather(void)//获取最新时间和天气
{


    int retry_count = 0;
    const int max_retries = 5;
    rt_err_t ntp_result = RT_ERROR;
    
    while (1) {
        if (check_internet_access() != 1) 
        {
            LOG_W("network disconnected during xiaozhi_time_weather");
            g_network_available = 0;
            return;

        }
        ntp_result = xiaozhi_ntp_sync();//同步网络服务时间
        if (ntp_result == RT_EOK) 
        {
            update_xiaozhi_ui_time(NULL);//更新界面时间显示
            LOG_I("Time synchronization successful, next sync in 30min");
            break;
        } 
        else 
        {
            retry_count++;
            LOG_W("Initial time synchronization failed, retrying... attempt %d", retry_count);
            if (retry_count < max_retries) 
            {
                rt_thread_mdelay(3000); // 等待3秒后重试
            } 
            else 
            {
                break;
            }
            
        }
    }

    if (ntp_result != RT_EOK) {
        LOG_W("Time synchronization failed after %d attempts, will retry in 5 minutes", max_retries);
    }

    // 获取天气信息带重试机制
    retry_count = 0;
    rt_err_t weather_result = RT_ERROR;
    
    while (1) {
        if (check_internet_access() != 1) 
        {
           LOG_W("network disconnected during time synchronization");
            g_network_available = 0;
            retry_count++;
            LOG_W("Failed to get weather information, attempt %d of %d", retry_count, max_retries);
            if (retry_count < max_retries) {
                rt_thread_mdelay(3000); // 等待3秒后重试
                continue;
            }
            else
            {
                LOG_W("Failed to get weather information after %d attempts", max_retries);
            }
        }
        
        weather_result = xiaozhi_weather_get(&g_current_weather);
        if (weather_result == RT_EOK) 
        {
            update_xiaozhi_ui_weather(NULL);//获取成功则更新一次
            LOG_W("xiaozhi_weather_get successful");
            break;
        } 
        else 
        {
            retry_count++;
            LOG_W("Failed to get weather information, attempt %d of %d", retry_count, max_retries);
            if (retry_count < max_retries) 
            {
                rt_thread_mdelay(3000); // 等待3秒后重试
            }
            else 
            {
                break;
            }
            
        }
    }
    
    if (weather_result != RT_EOK) {
        LOG_W("Failed to get weather information after %d attempts, will retry in 5 minutes", max_retries);
    }

}
      

//初始化rtc设备
int xiaozhi_time_weather_init(void)
{
        xiaozhi_time_use_china_timezone();

        // 查找RTC设备
        g_rtc_device = rt_device_find("rtc");
        if (g_rtc_device == RT_NULL)
        {
            return -RT_ENOMEM;
            LOG_W("RTC device not found, using system time only");
        }
        else
        {
            rt_device_open(g_rtc_device, RT_DEVICE_OFLAG_RDWR);
        }

    

    return RT_EOK;
}
