// xiaozhi_public.c
#include "xiaozhi_client_public.h"
#include <rtthread.h>
#include "bf0_hal.h"
#include "bts2_global.h"
#include "bt_connection_manager.h"
#include "bt_env.h"
#include <webclient.h>
#include <cJSON.h>
#include "stdio.h"
#include "string.h"
#include <lwip/dns.h>
#include <lwip/sys.h>
#include "xiaozhi_websocket.h"
#include "lwip/api.h"
#include "lwip/dns.h"
#include "lwip/apps/websocket_client.h"
#include "lwip/apps/mqtt_priv.h"
#include "lwip/apps/mqtt.h"
#include "lwip/tcpip.h"
#include "lv_timer.h"
#include "lv_display.h"
#include "lv_obj_pos.h"
#include "lv_tiny_ttf.h"
#include "lv_obj.h"
#include "lv_label.h"
#include "bf0_sys_cfg.h"
#include "drv_flash.h"
#include "audio_mem.h"
#include "mbedtls/platform.h"
#include "gui_app_pm.h"
#include "network/net_manager.h"
#include "../board/board_hardware.h"
static const char *ota_version =
    "{\r\n "
    "\"version\": 2,\r\n"
    "\"flash_size\": 4194304,\r\n"
    "\"psram_size\": 8388608,\r\n"
    "\"minimum_free_heap_size\": 123456,\r\n"
    "\"mac_address\": \"%s\",\r\n"
    "\"uuid\": \"%s\",\r\n"
    "\"chip_model_name\": \"sf32lb563\",\r\n"
    "\"chip_info\": {\r\n"
    "    \"model\": 1,\r\n"
    "    \"cores\": 2,\r\n"
    "    \"revision\": 0,\r\n"
    "    \"features\": 0\r\n"
    "},\r\n"
    "\"application\": {\r\n"
    "    \"name\": \"my-app\",\r\n"
    "    \"version\": \"1.0.0\",\r\n"
    "    \"compile_time\": \"2021-01-01T00:00:00Z\",\r\n"
    "    \"idf_version\": \"4.2-dev\",\r\n"
    "    \"elf_sha256\": \"\"\r\n"
    "},\r\n"
    "\"partition_table\": [\r\n"
    "    {\r\n"
    "        \"label\": \"app\",\r\n"
    "        \"type\": 1,\r\n"
    "        \"subtype\": 2,\r\n"
    "        \"address\": 10000,\r\n"
    "        \"size\": 100000\r\n"
    "    }\r\n"
    "],\r\n"
    "\"ota\": {\r\n"
    "    \"label\": \"ota_0\"\r\n"
    "},\r\n"
    "\"board\": {\r\n"
    "    \"type\":\"hdk563\",\r\n"
    "    \"mac\": \"%s\"\r\n"
    "}\r\n"
    "}\r\n";

// 公共变量定义
extern uint8_t aec_enabled;
extern BOOL first_pan_connected;

static uint8_t g_en_vad = 1;
static uint8_t g_en_aec = 1;
static uint8_t g_config_change = 0;

volatile int g_kws_force_exit = 0;
volatile int g_kws_running = 0;
volatile uint8_t she_bei_ma = 1;
char mac_address_string[20];
char client_id_string[40];
ALIGN(4) uint8_t g_sha256_result[32] = {0};
static char s_xiaozhi_last_error[160];
static rt_bool_t s_xiaozhi_tls_allocator_ready = RT_FALSE;
static int s_last_internet_access_result = -1;
static int s_last_internet_access_log_state = -1;
static rt_tick_t s_last_internet_access_tick = 0;

#define XZ_INTERNET_ACCESS_CACHE_OK_MS   30000U
#define XZ_INTERNET_ACCESS_CACHE_FAIL_MS 5000U

#if defined(__CC_ARM) || defined(__CLANG_ARM)
L2_RET_BSS_SECT_BEGIN(xiaozhi_http_resp_buf)
static char s_xiaozhi_http_resp_buf[GET_RESP_BUFSZ];
L2_RET_BSS_SECT_END
#else
static char s_xiaozhi_http_resp_buf[GET_RESP_BUFSZ]
    L2_RET_BSS_SECT(xiaozhi_http_resp_buf);
#endif

static void *xz_mbedtls_calloc(size_t count, size_t size)
{
    if (count == 0 || size == 0)
    {
        return audio_mem_calloc(0, 0);
    }

    if (count > (SIZE_MAX / size) ||
        count > UINT32_MAX || size > UINT32_MAX)
    {
        return RT_NULL;
    }

    return audio_mem_calloc((uint32_t)count, (uint32_t)size);
}

static void xz_mbedtls_free(void *ptr)
{
    audio_mem_free(ptr);
}

void xz_prepare_tls_allocator(void)
{
    if (s_xiaozhi_tls_allocator_ready)
    {
        return;
    }

    if (mbedtls_platform_set_calloc_free(xz_mbedtls_calloc,
                                         xz_mbedtls_free) == 0)
    {
        s_xiaozhi_tls_allocator_ready = RT_TRUE;
        rt_kprintf("xiaozhi tls allocator ready (psram enabled)\n");
    }
    else
    {
        rt_kprintf("xiaozhi tls allocator setup failed\n");
    }
}

static void xz_fill_device_address(bd_addr_t *addr)
{
    if (addr == RT_NULL)
    {
        return;
    }

    memset(addr, 0, sizeof(*addr));
    (void)ble_request_public_address(addr);
}

ble_common_update_type_t ble_request_public_address(bd_addr_t *addr)
{
    uint8_t mac[6] = {0};
    int ret = 0;
    int read_len = rt_flash_config_read(FACTORY_CFG_ID_MAC, mac, 6);
    // OTP没有内容，用UID生成MAC
    if (read_len == 0)
    {    
        ret = bt_mac_addr_generate_via_uid_v2(addr);
        if (ret != 0)
        {   
            //uid生成失败
            rt_kprintf("uid get mac fail: %d", ret);
            return BLE_UPDATE_NO_UPDATE;
        }
        else
        {
            // 抹掉最后一个字节的bit0和bit1，避免组播地址
            addr->addr[5] &= ~0x03; 
            rt_kprintf("uid get mac ok\n");
            rt_kprintf("UID mac: %02x:%02x:%02x:%02x:%02x:%02x\n",
            addr->addr[5], addr->addr[4], addr->addr[3],
            addr->addr[2], addr->addr[1], addr->addr[0]);
        }
    }
    else
    {
        // OTP有内容，直接用
        memcpy(addr->addr, mac, 6);
        rt_kprintf("MAC read from OTP: %02x:%02x:%02x:%02x:%02x:%02x\n",
            mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
    }

    return BLE_UPDATE_ONCE;
}

char *get_mac_address()
{
    if (mac_address_string[0] == '\0')
    {
        bd_addr_t addr;

        xz_fill_device_address(&addr);

        rt_snprintf((char *)mac_address_string, 20,
                    "%02x:%02x:%02x:%02x:%02x:%02x",
                    addr.addr[5], addr.addr[4], addr.addr[3],
                    addr.addr[2], addr.addr[1], addr.addr[0]);
    }
    return (&(mac_address_string[0]));
}
void hash_run(uint8_t algo, uint8_t *raw_data, uint32_t raw_data_len,
              uint8_t *result, uint32_t result_len)
{
    /* Rest hash block. */
    HAL_HASH_reset();
    /* Initialize AES Hash hardware block. */
    HAL_HASH_init(NULL, algo, 0);
    /* Do hash. HAL_HASH_run will block until hash finish. */
    HAL_HASH_run(raw_data, raw_data_len, 1);
    /* Get hash result. */
    HAL_HASH_result(result);
}
void hex_2_asc(uint8_t n, char *str)
{
    uint8_t i = (n >> 4);
    if (i >= 10)
        *str = i + 'a' - 10;
    else
        *str = i + '0';
    str++, i = n & 0xf;
    if (i >= 10)
        *str = i + 'a' - 10;
    else
        *str = i + '0';
}
char *get_client_id()
{
    if (client_id_string[0] == '\0')
    {
        int i, j = 0;
        bd_addr_t addr;

        xz_fill_device_address(&addr);
        hash_run(HASH_ALGO_SHA256, (uint8_t *)&addr, sizeof(addr),
                 g_sha256_result, sizeof(g_sha256_result));
        for (i = 0; i < 16; i++, j += 2)
        {
            // 12345678-1234-1234-1234-123456789012
            if (i == 4 || i == 6 || i == 8 || i == 10)
            {
                client_id_string[j++] = '-';
            }
            hex_2_asc(g_sha256_result[i], &client_id_string[j]);
        }
        rt_kprintf(client_id_string);
    }
    return (&(client_id_string[0]));
}

const char *get_xiaozhi_last_error(void)
{
    return s_xiaozhi_last_error;
}

static void svr_found_callback(const char *name, const ip_addr_t *ipaddr,
                               void *callback_arg)
{
    if (ipaddr != NULL)
    {
        rt_kprintf("DNS lookup succeeded, IP: %s\n", ipaddr_ntoa(ipaddr));
    }
}

static rt_bool_t xz_internet_access_cache_hit(int expected_result,
                                              rt_tick_t now_tick)
{
    rt_tick_t ttl;

    if (s_last_internet_access_result != expected_result)
    {
        return RT_FALSE;
    }

    if (s_last_internet_access_tick == 0U)
    {
        return RT_FALSE;
    }

    ttl = rt_tick_from_millisecond((expected_result == 1) ?
                                   XZ_INTERNET_ACCESS_CACHE_OK_MS :
                                   XZ_INTERNET_ACCESS_CACHE_FAIL_MS);
    return (rt_tick_t)(now_tick - s_last_internet_access_tick) < ttl;
}

static void xz_report_internet_access_failure(int reason, const char *hostname)
{
    if (s_last_internet_access_log_state == reason)
    {
        return;
    }

    if (reason == 0)
    {
        rt_kprintf("network link inactive, skip dns check for %s\n",
                   hostname);
    }
    else
    {
        rt_kprintf("Coud not find %s, please check PAN connection\n",
                   hostname);
    }

    s_last_internet_access_log_state = reason;
}

int check_internet_access()
{
    int r = 0;
    const char *hostname = XIAOZHI_HOST;
    ip_addr_t addr = {0};
    rt_tick_t now_tick = rt_tick_get();

    if (!net_manager_network_ready())
    {
        xz_report_internet_access_failure(0, hostname);
        s_last_internet_access_result = 0;
        s_last_internet_access_tick = now_tick;
        return 0;
    }

    if (xz_internet_access_cache_hit(1, now_tick))
    {
        return 1;
    }

    if (xz_internet_access_cache_hit(0, now_tick))
    {
        return 0;
    }

    {
        err_t err =
            dns_gethostbyname(hostname, &addr, svr_found_callback, NULL);
        if (err != ERR_OK && err != ERR_INPROGRESS)
        {
            xz_report_internet_access_failure(1, hostname);
            s_last_internet_access_result = 0;
            s_last_internet_access_tick = now_tick;
        }
        else
        {
            r = 1;
            s_last_internet_access_result = 1;
            s_last_internet_access_tick = now_tick;
            s_last_internet_access_log_state = -1;
        }
    }

    return r;
}

int xiaozhi_network_service_ready(void)
{
    return check_internet_access() ? 1 : 0;
}

char *get_xiaozhi()
{
    rt_kprintf("gett_xiaozhi\n");
    int resp_status;
    struct webclient_session *session = RT_NULL;
    char *ota_formatted = RT_NULL;
    char xiaozhi_url[GET_URL_LEN_MAX];
    int content_length = -1;
    int bytes_read = 0;
    int content_pos = 0;
    int ota_len;
    rt_size_t ota_capacity;

    s_xiaozhi_last_error[0] = '\0';
    xz_prepare_tls_allocator();

    if (xiaozhi_network_service_ready() == 0)
    {
        rt_snprintf(s_xiaozhi_last_error, sizeof(s_xiaozhi_last_error),
                    "网络未连接或服务未就绪");
        return RT_NULL;
    }

    first_pan_connected = TRUE;

    ota_capacity = strlen(ota_version) + strlen(get_mac_address()) * 2U +
                   strlen(get_client_id()) + 32U;
    ota_formatted = (char *)audio_mem_malloc((uint32_t)ota_capacity);
    if (ota_formatted == RT_NULL)
    {
        rt_snprintf(s_xiaozhi_last_error, sizeof(s_xiaozhi_last_error),
                    "创建 OTA 请求失败");
        goto __exit;
    }

    memset(ota_formatted, 0, ota_capacity);
    ota_len = rt_snprintf(ota_formatted, ota_capacity, ota_version,
                          get_mac_address(), get_client_id(),
                          get_mac_address());
    if (ota_len <= 0 || ota_len >= (int)ota_capacity)
    {
        rt_kprintf("xiaozhi request body too long\n");
        rt_snprintf(s_xiaozhi_last_error, sizeof(s_xiaozhi_last_error),
                    "设备请求参数过长");
        goto __exit;
    }

    memset(xiaozhi_url, 0, sizeof(xiaozhi_url));
    rt_snprintf(xiaozhi_url, sizeof(xiaozhi_url), GET_URI, XIAOZHI_HOST);

    /* 创建会话并且设置响应的大小 */
    session = webclient_session_create(GET_HEADER_BUFSZ);
    if (session == RT_NULL)
    {
        rt_kprintf("No memory for get header!\n");
        rt_snprintf(s_xiaozhi_last_error, sizeof(s_xiaozhi_last_error),
                    "创建网络会话失败");
        goto __exit;
    }

    webclient_header_fields_add(session, "Device-Id: %s \r\n",
                                get_mac_address());
    webclient_header_fields_add(session, "Client-Id: %s \r\n", get_client_id());
    webclient_header_fields_add(session, "Content-Type: application/json \r\n");
    webclient_header_fields_add(session, "Content-length: %d \r\n",
                                ota_len);
    // webclient_header_fields_add(session, "X-language:");

    /* 发送 GET 请求使用默认的头部 */
    if ((resp_status = webclient_post(session, xiaozhi_url, ota_formatted,
                                      ota_len)) != 200)
    {
        rt_kprintf("webclient Post request failed, response(%d) error.\n",
                   resp_status);
        rt_snprintf(s_xiaozhi_last_error, sizeof(s_xiaozhi_last_error),
                    "小智 OTA 接口返回异常(%d)", resp_status);
        goto __exit;
    }

    memset(s_xiaozhi_http_resp_buf, 0, sizeof(s_xiaozhi_http_resp_buf));

    content_length = webclient_content_length_get(session);
    while (content_pos < (GET_RESP_BUFSZ - 1))
    {
        int read_size = GET_RESP_BUFSZ - 1 - content_pos;

        if (content_length > 0 && read_size > (content_length - content_pos))
        {
            read_size = content_length - content_pos;
        }

        if (read_size <= 0)
        {
            break;
        }

        bytes_read = webclient_read(session, s_xiaozhi_http_resp_buf + content_pos,
                                    read_size);
        if (bytes_read <= 0)
        {
            break;
        }

        content_pos += bytes_read;

        if (content_length > 0 && content_pos >= content_length)
        {
            break;
        }
    }
__exit:
    /* 关闭会话 */
    if (session != RT_NULL)
    {
        LOCK_TCPIP_CORE();
        webclient_close(session);
        UNLOCK_TCPIP_CORE();
    }

    if (ota_formatted != RT_NULL)
    {
        audio_mem_free(ota_formatted);
    }

    if (content_pos <= 0)
    {
        if (s_xiaozhi_last_error[0] == '\0')
        {
            rt_snprintf(s_xiaozhi_last_error, sizeof(s_xiaozhi_last_error),
                        "小智 OTA 接口未返回有效内容");
        }
        return RT_NULL;
    }

    s_xiaozhi_http_resp_buf[content_pos] = '\0';
    return s_xiaozhi_http_resp_buf;
}
char *my_json_string(cJSON *json, char *key)
{
    cJSON *item = cJSON_GetObjectItem(json, key);
    char *r = cJSON_Print(item);

    if (r && ((*r) == '\"'))
    {
        r++;
        r[strlen(r) - 1] = '\0';
    }
    return r;
}

uint8_t vad_is_enable(void)
{
    return g_en_vad;
}

void vad_set_enable(uint8_t enable)
{
    if(enable != g_en_vad)
    {
        g_en_vad = enable;
        xz_set_config_update(true);
        rt_kprintf("vad_set_enable VAD %d \r\n", g_en_vad);
    }
}

uint8_t aec_is_enable(void)
{
    return g_en_aec;
}

void aec_set_enable(uint8_t enable)
{
    if(enable != g_en_aec)
    {
        g_en_aec = enable;
        xz_set_config_update(true);
        rt_kprintf("vad_set_enable AEC %d \r\n", g_en_aec);
    }
}

uint8_t xz_get_config_update(void)
{
    return g_config_change;
}

void xz_set_config_update(uint8_t en)
{
    g_config_change = en;
}



// 设备注册函数
int register_device_with_server(void)
{
    device_register_params_t reg_params = {0};
    
    // 填充注册参数
    reg_params.mac = get_mac_address();

#ifdef BSP_USING_BOARD_SF32LB52_LCD_N16R8
    reg_params.model = "sf32lb52-lcd-n16r8";
    reg_params.solution = "SF32LB52_LCD_N16R8_TFT_CO5300";
#elif defined(BSP_USING_BOARD_SF32LB52_LCHSPI_ULP)
    reg_params.model = "sf32lb52-lchspi-ulp";
    reg_params.solution = "SF32LB52_ULP_NOR_TFT_CO5300";
#elif defined(BSP_USING_BOARD_SF32LB52_NANO_52J)
    reg_params.model = "sf32lb52-nano-52j";
    reg_params.solution = "SF32LB52_NANO_52J_TFT_CO5300";
#elif defined(BSP_USING_BOARD_SF32LB52_XTY_AI)
    reg_params.model = "sf32lb52-xty-ai";
    reg_params.solution = "SF32LB52_ULP_XTY_AI_SPI_ST7789";
#elif defined(BSP_USING_BOARD_SF32LB52_XTY_AI_THT)
    reg_params.model = "sf32lb52-xty-ai-tht";
    reg_params.solution = "SF32LB52_XTY_AI_THT_SPI_ST7789";
#endif

    reg_params.version = VERSION; // 当前固件版本
    reg_params.ota_version = VERSION;
    reg_params.chip_id = get_client_id();
    
    // 服务器注册设备URL
    const char* ota_server_url = "https://ota.sifli.com";
    
    // 执行设备注册
    int result = dfu_pan_register_device(ota_server_url, &reg_params);
    
    if (result == 0) {
        rt_kprintf("Device registered successfully with chip_id: %s\n", get_client_id());
    } else {
        rt_kprintf("Device registration failed\n");
    }
    
    return result;
}

// 构建OTA查询URL
char* build_ota_query_url(const char* chip_id)
{
    static char query_url[512] = {0};
    
#ifdef BSP_USING_BOARD_SF32LB52_LCD_N16R8
    snprintf(query_url, sizeof(query_url), 
             "https://ota.sifli.com/v2/xiaozhi/SF32LB52_LCD_N16R8_TFT_CO5300/sf32lb52-lcd-n16r8?chip_id=%s&version=latest",
             chip_id);
#elif defined(BSP_USING_BOARD_SF32LB52_LCHSPI_ULP)
    snprintf(query_url, sizeof(query_url), 
             "https://ota.sifli.com/v2/xiaozhi/SF32LB52_ULP_NOR_TFT_CO5300/sf32lb52-lchspi-ulp?chip_id=%s&version=latest",
             chip_id);
#elif defined(BSP_USING_BOARD_SF32LB52_NANO_52J)
    snprintf(query_url, sizeof(query_url), 
             "https://ota.sifli.com/v2/xiaozhi/SF32LB52_NANO_52J_TFT_CO5300/sf32lb52-nano-52j?chip_id=%s&version=latest",
             chip_id);
#elif defined(BSP_USING_BOARD_SF32LB52_XTY_AI)
    snprintf(query_url, sizeof(query_url), 
             "https://ota.sifli.com/v2/xiaozhi/SF32LB52_XTY_AI_SPI_ST7789/sf32lb52-xty-ai?chip_id=%s&version=latest",
             chip_id);
#elif defined(BSP_USING_BOARD_SF32LB52_XTY_AI_THT)
    snprintf(query_url, sizeof(query_url), 
             "https://ota.sifli.com/v2/xiaozhi/SF32LB52_XTY_AI_THT_SPI_ST7789/sf32lb52-xty-ai-tht?chip_id=%s&version=latest",
             chip_id);
#endif
    
    return query_url;
}
