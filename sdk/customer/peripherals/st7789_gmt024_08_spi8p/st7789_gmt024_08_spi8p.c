#include <rtthread.h>
#include "string.h"
#include "board.h"
#include "drv_io.h"
#include "drv_lcd.h"
#include "bf0_hal.h"
#include "drivers/rt_drv_pwm.h"
#include <finsh.h>
#include "waveinit.h"
#include "epd_pin_defs.h"
#include "mem_section.h"

#define DBG_TAG "epd.spi"
#define DBG_LVL DBG_INFO
#include "log.h"

#define EPD_SRC_PIXELS 792
#define EPD_GATE_PIXELS 528
#define EPD_WIDTH EPD_SRC_PIXELS
#define EPD_HEIGHT EPD_GATE_PIXELS
#define EPD_LOGICAL_WIDTH LCD_HOR_RES_MAX
#define EPD_LOGICAL_HEIGHT LCD_VER_RES_MAX
#define EPD_BACKLIGHT_PWM_DEV_NAME "pwm2"
#define EPD_BACKLIGHT_PWM_CHANNEL 4
#define EPD_BACKLIGHT_PWM_PERIOD_NS (10 * 1000)
#define EPD_GRAY2_FRAME_SIZE (EPD_WIDTH * EPD_HEIGHT / 4) /* 2bpp */
#define EPD_MONO_FRAME_SIZE (EPD_WIDTH * EPD_HEIGHT / 8)  /* 1bpp */
#define EPD_SELF_TEST_PATTERN 0
#define EPD_USE_SCAN_MODE3 1
#define EPD_ENABLE_WINDOW_CMDS 1
/* Scan total and visible window can differ (per UC8179 reference code). */
#define EPD_SCAN_SRC_PIXELS EPD_SRC_PIXELS
/* Vendor sample uses total gate scan 600 with visible window starting at Y=72 (0x48). */
#define EPD_SCAN_GATE_PIXELS 600
#define EPD_WIN_X_START 0
#define EPD_WIN_Y_START 0
#define EPD_WIN_WIDTH EPD_WIDTH
#define EPD_WIN_HEIGHT EPD_HEIGHT

static const unsigned char EPD_lut_full_update[] = {
    0x02, 0x02, 0x01, 0x11, 0x12, 0x12, 0x22, 0x22, 0x66, 0x69,
    0x69, 0x59, 0x58, 0x99, 0x99, 0x88, 0x00, 0x00, 0x00, 0x00,
    0xF8, 0xB4, 0x13, 0x51, 0x35, 0x51, 0x51, 0x19, 0x01, 0x00};

static const unsigned char EPD_lut_partial_update[] = {
    0x10, 0x18, 0x18, 0x08, 0x18, 0x18, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x13, 0x14, 0x44, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned char LUT_GC[282]={
/*Vcom*/
0x01, 0x18, 0x04, 0x0E, 0x0A, 0x01, 0x01,
0x01, 0x0A, 0x00, 0x00, 0x00, 0x01, 0x01,
0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/*WW*/
0x01, 0x58, 0x04, 0x8E, 0x8A, 0x01, 0x01,
0x01, 0x0A, 0x00, 0x00, 0x00, 0x01, 0x01,
0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/*BW*/
0x01, 0x18, 0x04, 0x8E, 0x8A, 0x01, 0x01,
0x01, 0x0A, 0x00, 0x00, 0x00, 0x01, 0x01,
0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/*WB*/
0x01, 0x18, 0x04, 0x4E, 0x0A, 0x01, 0x01,
0x01, 0x4A, 0x00, 0x00, 0x00, 0x01, 0x01,
0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/*BB*/
0x01, 0x98, 0x04, 0x4E, 0x0A, 0x01, 0x01,
0x01, 0x4A, 0x00, 0x00, 0x00, 0x01, 0x01,
0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

const unsigned char LUT_DU[282]={
/*Vcom*/
0x01, 0x06, 0x01, 0x06, 0x06, 0x01, 0x01,
0x01, 0x04, 0x01, 0x01, 0x00, 0x01, 0x01,
0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/*WW*/
0x01, 0x06, 0x81, 0x06, 0x06, 0x01, 0x01,
0x01, 0x04, 0x01, 0x01, 0x00, 0x01, 0x01,
0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/*BW*/
0x01, 0x86, 0x81, 0x86, 0x86, 0x01, 0x01,
0x01, 0x84, 0x81, 0x01, 0x00, 0x01, 0x01,
0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/*WB*/
0x01, 0x46, 0x41, 0x46, 0x46, 0x01, 0x01,
0x01, 0x44, 0x41, 0x01, 0x00, 0x01, 0x01,
0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/*BB*/
0x01, 0x06, 0x01, 0x06, 0x06, 0x01, 0x01,
0x01, 0x04, 0x01, 0x41, 0x00, 0x01, 0x01,
0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static struct rt_device_pwm *g_epd_backlight_pwm = RT_NULL;
static rt_bool_t g_epd_backlight_pwm_ready = RT_FALSE;

static void EPD_SetBacklightFallback(uint8_t br)
{
    HAL_PIN_Set(PAD_PA01, GPIO_A1, PIN_NOPULL, 1);
    BSP_GPIO_Set(1, br > 0 ? 1 : 0, 1);
    rt_kprintf("EPD backlight fallback gpio=%u\n", br > 0 ? 1U : 0U);
}

static void EPD_SetBacklight(uint8_t br)
{
    rt_err_t result;
    rt_uint32_t pulse;

    rt_kprintf("bl_trace: st7789 EPD_SetBacklight=%u\n", br);

    if (br > 100)
    {
        br = 100;
    }

    MODIFY_REG(hwp_hpsys_cfg->GPTIM1_PINR,
               HPSYS_CFG_GPTIM1_PINR_CH4_PIN_Msk,
               MAKE_REG_VAL(PAD_PA01 - PAD_PA00,
                            HPSYS_CFG_GPTIM1_PINR_CH4_PIN_Msk,
                            HPSYS_CFG_GPTIM1_PINR_CH4_PIN_Pos));
    HAL_PIN_Set(PAD_PA01, PA01_TIM, PIN_NOPULL, 1);

    if (g_epd_backlight_pwm == RT_NULL)
    {
        g_epd_backlight_pwm = (struct rt_device_pwm *)rt_device_find(EPD_BACKLIGHT_PWM_DEV_NAME);
        if (g_epd_backlight_pwm == RT_NULL)
        {
            LOG_W("EPD backlight pwm device not found: %s", EPD_BACKLIGHT_PWM_DEV_NAME);
            EPD_SetBacklightFallback(br);
            return;
        }
    }

    if (!g_epd_backlight_pwm_ready)
    {
        result = rt_device_open((rt_device_t)g_epd_backlight_pwm, RT_DEVICE_OFLAG_RDWR);
        if ((result != RT_EOK) && (result != -RT_EBUSY))
        {
            LOG_W("EPD backlight pwm open failed: %d", result);
            EPD_SetBacklightFallback(br);
            return;
        }
        g_epd_backlight_pwm_ready = RT_TRUE;
    }

    pulse = (EPD_BACKLIGHT_PWM_PERIOD_NS * br) / 100;
    result = rt_pwm_set(g_epd_backlight_pwm,
                        EPD_BACKLIGHT_PWM_CHANNEL,
                        EPD_BACKLIGHT_PWM_PERIOD_NS,
                        pulse);
    if (result != RT_EOK)
    {
        LOG_W("EPD backlight pwm set failed: %d br=%d pulse=%d", result, br, pulse);
        EPD_SetBacklightFallback(br);
        return;
    }

    if (br == 0)
    {
        result = rt_pwm_disable(g_epd_backlight_pwm, EPD_BACKLIGHT_PWM_CHANNEL);
        if (result != RT_EOK)
        {
            LOG_W("EPD backlight pwm disable failed: %d", result);
            EPD_SetBacklightFallback(0);
        }
    }
    else
    {
        result = rt_pwm_enable(g_epd_backlight_pwm, EPD_BACKLIGHT_PWM_CHANNEL);
        if (result != RT_EOK)
        {
            LOG_W("EPD backlight pwm enable failed: %d", result);
            EPD_SetBacklightFallback(br);
            return;
        }
    }

    LOG_I("EPD backlight brightness=%d pulse=%d", br, pulse);
}

#define EPD_FULL 0
#define EPD_PART 1

#define EPD_LCD_ID 0x09ff
#define LCD_PIXEL_WIDTH EPD_WIDTH
#define LCD_PIXEL_HEIGHT EPD_HEIGHT
#define LCD_HOR_RES_MAX_8 LCD_HOR_RES_MAX / 8

#define PICTURE_LENGTH (LCD_HOR_RES_MAX / 8 * LCD_VER_RES_MAX)
#define PIC_WHITE 255                  // 全白
#define PIC_BLACK 254                  // 全黑
#define PIC_LEFT_BLACK_RIGHT_WHITE 253 // 左黑右白
#define PIC_UP_BLACK_DOWN_WHITE 252    // 上黑下白
#define PART_DISP_TIMES 10
#define EPD_REFRESH_INTERVAL_MS 180
#define EPD_PARTIAL_REFRESH_FALLBACK_MS 700
#define EPD_FAST_FULL_REFRESH_FALLBACK_MS 820
#define EPD_GC_FULL_REFRESH_FALLBACK_MS 950
/*
 * Test switches for intermittent artifact diagnosis.
 * Set these to 0 to restore the previous production behavior.
 */
#define EPD_TEST_FORCE_FULL_REFRESH 0
#define EPD_TEST_STRICT_BUSY_WAIT 1
/*
 * Mono conversion quality switch:
 * 0 = hard threshold (sharper text, less edge noise)
 * 1 = Bayer dithering (smoother gradients, may look grainy on text)
 */
#define EPD_MONO_THRESHOLD_LEVEL 2U
#define EPD_USE_GRAY2_REFRESH 0

#define REG_LUT_VCOM 0x20
#define REG_LUT_W2W 0x21
#define REG_LUT_K2W 0x22
#define REG_LUT_W2K 0x23
#define REG_LUT_K2K 0x24
#define REG_LUT_OPT 0x2A
#define REG_WRITE_OLD_DATA 0x10
#define REG_WRITE_NEW_DATA 0x13
#define REG_AUTO_REFRESH 0x17
#define REG_PWR_ON_MEASURE 0x05
#define REG_TEMP_CALIB 0x40
#define REG_TEMP_SEL 0x41
#define REG_TEMP_READ 0x43
#define REG_PANEL_SETTING 0x00
#define REG_POWER_SETTING 0x01
#define REG_BOOSTER_SOFTSTART 0x06
#define REG_PLL_CTRL 0x30
#define REG_VCOM_DATA_INTERV 0x50
#define REG_TCON_SETTING 0x60
#define REG_RESOLUTION 0x61
#define REG_REV 0x70
#define REG_VDCS 0x82
#define REG_WRITE_NEW_DATA 0x13

static int reflesh_times;
static uint8_t current_refresh_mode;
static unsigned char LUT_Flag = 0; // LUT切换标志
static unsigned char Var_Temp = 0; // 温度值
static const char *s_epd_busy_stage = "boot";
static uint32_t s_epd_refresh_fallback_ms = 1000;
static rt_tick_t s_last_epd_refresh_tick = 0;
static uint8_t s_frame_collecting = 0;
static uint8_t s_dirty_area_valid = 0;
static uint16_t s_dirty_x0 = 0;
static uint16_t s_dirty_y0 = 0;
static uint16_t s_dirty_x1 = 0;
static uint16_t s_dirty_y1 = 0;
static uint8_t s_partial_refresh_count = 0;
static uint8_t s_epd_stats_dump_count = 0;
static rt_mutex_t s_epd_flush_mutex = RT_NULL;

static LCDC_InitTypeDef lcdc_int_cfg = {
    .lcd_itf = LCDC_INTF_SPI_DCX_1DATA,
    .freq = 5000000,
    .color_mode = LCDC_PIXEL_FORMAT_RGB332,
    .cfg =
        {
            .spi =
                {
                    .dummy_clock = 0,
                    .syn_mode = HAL_LCDC_SYNC_DISABLE,
                    .vsyn_polarity = 1,
                    .vsyn_delay_us = 0,
                    .hsyn_num = 0,
                },
        },
};

LCDC_HandleTypeDef hlcdc;
static uint16_t g_input_color_mode = RTGRAPHIC_PIXEL_FORMAT_RGB565;
static uint32_t LCD_ReadID(LCDC_HandleTypeDef *hlcdc);
static void LCD_WriteReg(LCDC_HandleTypeDef *hlcdc, uint16_t LCD_Reg,
                         uint8_t *Parameters, uint32_t NbParameters);
static uint32_t LCD_ReadData(LCDC_HandleTypeDef *hlcdc, uint16_t RegValue,
                             uint8_t ReadSize);
static void EPD_TemperatureMeasure(LCDC_HandleTypeDef *hlcdc);
static void EPD_EnterDeepSleep(LCDC_HandleTypeDef *hlcdc);
static void EPD_DisplayImage(LCDC_HandleTypeDef *hlcdc, uint8_t img_flag);
static void EPD_EnterDeepSleep(LCDC_HandleTypeDef *hlcdc);
static void EPD_LoadLUT(LCDC_HandleTypeDef *hlcdc, uint8_t lut_mode);
static void LUTGC(LCDC_HandleTypeDef *hlcdc);
static void EPD_Refresh(LCDC_HandleTypeDef *hlcdc);
static void EPD_ReadBusy(void);
static void EPD_SendCommandDataBuf(LCDC_HandleTypeDef *hlcdc, uint8_t cmd,
                                   const uint8_t *data, uint32_t len);
static void EPD_SetFullWindow(LCDC_HandleTypeDef *hlcdc);
static void EPD_SetPartialWindow(LCDC_HandleTypeDef *hlcdc, uint16_t x0,
                                 uint16_t y0, uint16_t x1, uint16_t y1);
static void EPD_FrameBuffer_FlushRegion(LCDC_HandleTypeDef *hlcdc, uint16_t x0,
                                        uint16_t y0, uint16_t x1, uint16_t y1);
static void EPD_FrameBuffer_FlushFast(LCDC_HandleTypeDef *hlcdc);
static void EPD_FrameBuffer_FlushGray2(LCDC_HandleTypeDef *hlcdc);
static void EPD_MarkDirtyPhysicalRegion(uint16_t x0, uint16_t y0, uint16_t x1,
                                        uint16_t y1);
static void EPD_MarkDirtyLogicalRegion(uint16_t x0, uint16_t y0, uint16_t x1,
                                       uint16_t y1);
static void EPD_ClearDirtyRegion(void);
static uint8_t EPD_ShouldUsePartialWindow(uint16_t x0, uint16_t y0,
                                          uint16_t x1, uint16_t y1);
static uint8_t EPD_ShouldUseFullRefresh(uint16_t x0, uint16_t y0, uint16_t x1,
                                        uint16_t y1);
static uint8_t *EPD_AllocMonoRegionBuffer(const uint8_t *src, uint16_t x0,
                                          uint16_t y0, uint16_t x1,
                                          uint16_t y1, uint32_t *out_len);

L2_NON_RET_BSS_SECT_BEGIN(frambuf)
L2_NON_RET_BSS_SECT(frambuf,
                    ALIGN(64) static uint8_t mixed_framebuffer[EPD_GRAY2_FRAME_SIZE]);
L2_NON_RET_BSS_SECT(frambuf,
                    ALIGN(64) static uint8_t mixed_framebuffer_mono[EPD_MONO_FRAME_SIZE]);
L2_NON_RET_BSS_SECT(frambuf,
                    ALIGN(64) static uint8_t mixed_framebuffer_prev_mono[EPD_MONO_FRAME_SIZE]);
L2_NON_RET_BSS_SECT(frambuf,
                    ALIGN(64) static uint8_t s_partial_region_buffer[EPD_MONO_FRAME_SIZE]);
L2_NON_RET_BSS_SECT_END

static uint8_t framebuffer_initialized = 0;
static rt_bool_t s_epd_mono_dither_enabled = RT_FALSE;
static rt_bool_t s_epd_mono_dither_region_valid = RT_FALSE;
static uint16_t s_epd_mono_dither_x0 = 0;
static uint16_t s_epd_mono_dither_y0 = 0;
static uint16_t s_epd_mono_dither_x1 = 0;
static uint16_t s_epd_mono_dither_y1 = 0;

static rt_sem_t epd_busy_sem = RT_NULL;

static void epd_flush_mutex_init(void)
{
    if (s_epd_flush_mutex != RT_NULL)
    {
        return;
    }

    s_epd_flush_mutex = rt_mutex_create("epd_flush", RT_IPC_FLAG_PRIO);
    if (s_epd_flush_mutex == RT_NULL)
    {
        rt_kprintf("EPD flush mutex create failed\n");
    }
}

static void epd_flush_lock(void)
{
    if (s_epd_flush_mutex != RT_NULL)
    {
        rt_mutex_take(s_epd_flush_mutex, RT_WAITING_FOREVER);
    }
}

static void epd_flush_unlock(void)
{
    if (s_epd_flush_mutex != RT_NULL)
    {
        rt_mutex_release(s_epd_flush_mutex);
    }
}

static unsigned int epd_ticks_to_ms(rt_tick_t ticks)
{
    return (unsigned int)(((uint64_t)ticks * 1000U) / RT_TICK_PER_SECOND);
}

static inline uint8_t epd_clamp_gray2(uint8_t lv)
{
    return (lv > 3) ? 3 : lv;
}

static inline void epd_set_gray2_pixel(uint16_t x, uint16_t y, uint8_t lv)
{
    uint32_t px = (uint32_t)y * EPD_WIDTH + x;
    uint32_t byte_idx = px >> 2;              /* 4 pixels per byte */
    uint8_t shift = (uint8_t)((3 - (px & 0x3)) << 1); /* MSB first */
    uint8_t mask = (uint8_t)(0x3u << shift);
    lv = epd_clamp_gray2(lv);
    mixed_framebuffer[byte_idx] = (uint8_t)((mixed_framebuffer[byte_idx] & ~mask) | (lv << shift));
}

static inline uint8_t epd_get_gray2_pixel(uint16_t x, uint16_t y)
{
    uint32_t px = (uint32_t)y * EPD_WIDTH + x;
    uint32_t byte_idx = px >> 2;
    uint8_t shift = (uint8_t)((3 - (px & 0x3)) << 1);
    return (uint8_t)((mixed_framebuffer[byte_idx] >> shift) & 0x3u);
}

static inline void epd_map_logical_to_physical(uint16_t lx, uint16_t ly,
                                               uint16_t *px, uint16_t *py)
{
    /* Logical (portrait): 528x792 -> Physical (landscape): 792x528 */
    *px = ly;
    *py = lx;
}

static void epd_debug_dump_stats(const char *tag)
{
    uint32_t gray_count[4] = {0, 0, 0, 0};
    uint32_t mono_black = 0;
    uint32_t mono_white = 0;
    uint32_t total_pixels = (uint32_t)EPD_WIDTH * (uint32_t)EPD_HEIGHT;
    uint32_t i;

    for (i = 0; i < total_pixels; i++)
    {
        uint8_t lv = (mixed_framebuffer[i >> 2] >> ((3U - (i & 0x3U)) << 1)) & 0x3U;
        gray_count[lv]++;
    }

    for (i = 0; i < EPD_MONO_FRAME_SIZE; i++)
    {
        uint8_t value = mixed_framebuffer_mono[i];
        for (uint8_t bit = 0; bit < 8U; bit++)
        {
            if (value & (uint8_t)(1U << bit))
            {
                mono_white++;
            }
            else
            {
                mono_black++;
            }
        }
    }

    rt_kprintf("epd_stat[%s]: gray2 b=%lu dg=%lu lg=%lu w=%lu mono_black=%lu mono_white=%lu total=%lu\n",
               tag != RT_NULL ? tag : "na",
               (unsigned long)gray_count[0],
               (unsigned long)gray_count[1],
               (unsigned long)gray_count[2],
               (unsigned long)gray_count[3],
               (unsigned long)mono_black,
               (unsigned long)mono_white,
               (unsigned long)total_pixels);
}

static void EPD_Gray2ToMonoDither(void)
{
    static const uint8_t bayer2x2[2][2] = {
        {0, 2},
        {3, 1},
    };
    memset(mixed_framebuffer_mono, 0xFF, sizeof(mixed_framebuffer_mono));

    for (uint16_t y = 0; y < EPD_HEIGHT; y++)
    {
        for (uint16_t x = 0; x < EPD_WIDTH; x++)
        {
            uint8_t lv = epd_get_gray2_pixel(x, y); /* 0=black ... 3=white */
            uint8_t threshold = bayer2x2[y & 1][x & 1];
            /* level density: 0->0/4, 1->1/4, 2->2/4, 3->4/4 */
            uint8_t is_white = (lv == 3) ? 1 : ((lv > threshold) ? 1 : 0);

            uint32_t byte_idx = (uint32_t)y * (EPD_WIDTH / 8) + (x / 8);
            uint8_t bit_pos = (uint8_t)(7 - (x % 8));
            if (is_white)
            {
                mixed_framebuffer_mono[byte_idx] |= (uint8_t)(1u << bit_pos);
            }
            else
            {
                mixed_framebuffer_mono[byte_idx] &= (uint8_t)~(1u << bit_pos);
            }
        }
    }
}

static inline rt_bool_t epd_gray2_use_dither(uint16_t x, uint16_t y)
{
    if (!s_epd_mono_dither_enabled)
    {
        return RT_FALSE;
    }

    if (!s_epd_mono_dither_region_valid)
    {
        return RT_TRUE;
    }

    return (x >= s_epd_mono_dither_x0 && x <= s_epd_mono_dither_x1 &&
            y >= s_epd_mono_dither_y0 && y <= s_epd_mono_dither_y1) ? RT_TRUE : RT_FALSE;
}

static void EPD_Gray2ToMonoRegionDither(void)
{
    static const uint8_t bayer2x2[2][2] = {
        {0, 2},
        {3, 1},
    };

    memset(mixed_framebuffer_mono, 0xFF, sizeof(mixed_framebuffer_mono));

    for (uint16_t y = 0; y < EPD_HEIGHT; y++)
    {
        for (uint16_t x = 0; x < EPD_WIDTH; x++)
        {
            uint8_t lv = epd_get_gray2_pixel(x, y); /* 0=black ... 3=white */
            uint8_t is_white;
            uint32_t byte_idx = (uint32_t)y * (EPD_WIDTH / 8) + (x / 8);
            uint8_t bit_pos = (uint8_t)(7 - (x % 8));

            if (epd_gray2_use_dither(x, y))
            {
                uint8_t threshold = bayer2x2[y & 1][x & 1];
                is_white = (lv == 3U) ? 1U : ((lv > threshold) ? 1U : 0U);
            }
            else
            {
                is_white = (lv >= EPD_MONO_THRESHOLD_LEVEL) ? 1U : 0U;
            }

            if (is_white)
            {
                mixed_framebuffer_mono[byte_idx] |= (uint8_t)(1u << bit_pos);
            }
            else
            {
                mixed_framebuffer_mono[byte_idx] &= (uint8_t)~(1u << bit_pos);
            }
        }
    }
}

static void EPD_Gray2ToMonoThreshold(void)
{
    memset(mixed_framebuffer_mono, 0xFF, sizeof(mixed_framebuffer_mono));

    for (uint16_t y = 0; y < EPD_HEIGHT; y++)
    {
        for (uint16_t x = 0; x < EPD_WIDTH; x++)
        {
            uint8_t lv = epd_get_gray2_pixel(x, y); /* 0=black ... 3=white */
            uint8_t is_white = (lv >= EPD_MONO_THRESHOLD_LEVEL) ? 1U : 0U;
            uint32_t byte_idx = (uint32_t)y * (EPD_WIDTH / 8) + (x / 8);
            uint8_t bit_pos = (uint8_t)(7 - (x % 8));

            if (is_white)
            {
                mixed_framebuffer_mono[byte_idx] |= (uint8_t)(1u << bit_pos);
            }
            else
            {
                mixed_framebuffer_mono[byte_idx] &= (uint8_t)~(1u << bit_pos);
            }
        }
    }
}

static void EPD_Gray2ToMono(void)
{
    if (!s_epd_mono_dither_enabled)
    {
        EPD_Gray2ToMonoThreshold();
    }
    else if (s_epd_mono_dither_region_valid)
    {
        EPD_Gray2ToMonoRegionDither();
    }
    else
    {
        EPD_Gray2ToMonoDither();
    }
}

void lcd_set_epd_mono_dither_enabled(rt_bool_t enabled)
{
    s_epd_mono_dither_enabled = enabled ? RT_TRUE : RT_FALSE;
    s_epd_mono_dither_region_valid = RT_FALSE;
}

void lcd_set_epd_mono_dither_region(rt_bool_t enabled,
                                    uint16_t x0,
                                    uint16_t y0,
                                    uint16_t x1,
                                    uint16_t y1)
{
    if (!enabled)
    {
        s_epd_mono_dither_enabled = RT_FALSE;
        s_epd_mono_dither_region_valid = RT_FALSE;
        return;
    }

    if (x0 > x1)
    {
        uint16_t tmp = x0;
        x0 = x1;
        x1 = tmp;
    }
    if (y0 > y1)
    {
        uint16_t tmp = y0;
        y0 = y1;
        y1 = tmp;
    }

    if (x0 >= EPD_WIDTH) x0 = (uint16_t)(EPD_WIDTH - 1U);
    if (x1 >= EPD_WIDTH) x1 = (uint16_t)(EPD_WIDTH - 1U);
    if (y0 >= EPD_HEIGHT) y0 = (uint16_t)(EPD_HEIGHT - 1U);
    if (y1 >= EPD_HEIGHT) y1 = (uint16_t)(EPD_HEIGHT - 1U);

    s_epd_mono_dither_enabled = RT_TRUE;
    s_epd_mono_dither_region_valid = RT_TRUE;
    s_epd_mono_dither_x0 = x0;
    s_epd_mono_dither_y0 = y0;
    s_epd_mono_dither_x1 = x1;
    s_epd_mono_dither_y1 = y1;
}

static void EPD_MarkDirtyPhysicalRegion(uint16_t x0, uint16_t y0, uint16_t x1,
                                        uint16_t y1)
{
    if (x0 > x1 || y0 > y1)
    {
        return;
    }

    if (x1 >= EPD_WIDTH) x1 = EPD_WIDTH - 1;
    if (y1 >= EPD_HEIGHT) y1 = EPD_HEIGHT - 1;

    if (!s_dirty_area_valid)
    {
        s_dirty_x0 = x0;
        s_dirty_y0 = y0;
        s_dirty_x1 = x1;
        s_dirty_y1 = y1;
        s_dirty_area_valid = 1;
        return;
    }

    if (x0 < s_dirty_x0) s_dirty_x0 = x0;
    if (y0 < s_dirty_y0) s_dirty_y0 = y0;
    if (x1 > s_dirty_x1) s_dirty_x1 = x1;
    if (y1 > s_dirty_y1) s_dirty_y1 = y1;
}

static void EPD_MarkDirtyLogicalRegion(uint16_t x0, uint16_t y0, uint16_t x1,
                                       uint16_t y1)
{
    if (x0 > x1 || y0 > y1)
    {
        return;
    }

    if (x1 >= EPD_LOGICAL_WIDTH) x1 = EPD_LOGICAL_WIDTH - 1;
    if (y1 >= EPD_LOGICAL_HEIGHT) y1 = EPD_LOGICAL_HEIGHT - 1;

    EPD_MarkDirtyPhysicalRegion(y0, x0, y1, x1);
}

static void EPD_ClearDirtyRegion(void)
{
    s_dirty_area_valid = 0;
    s_dirty_x0 = 0;
    s_dirty_y0 = 0;
    s_dirty_x1 = 0;
    s_dirty_y1 = 0;
}

static uint8_t EPD_ShouldUsePartialWindow(uint16_t x0, uint16_t y0,
                                          uint16_t x1, uint16_t y1)
{
    uint32_t dirty_pixels = (uint32_t)(x1 - x0 + 1U) * (uint32_t)(y1 - y0 + 1U);
    uint32_t full_pixels = (uint32_t)EPD_WIDTH * (uint32_t)EPD_HEIGHT;

    return (dirty_pixels * 2U) < full_pixels;
}

static uint8_t EPD_ShouldUseFullRefresh(uint16_t x0, uint16_t y0, uint16_t x1,
                                        uint16_t y1)
{
#if EPD_TEST_FORCE_FULL_REFRESH
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    return 1;
#else
    if (x0 > x1 || y0 > y1)
    {
        return 1;
    }

    if (s_partial_refresh_count >= PART_DISP_TIMES)
    {
        return 1;
    }

    return 0;
#endif
}

static void EPD_GetEffectiveUpdateRegion(LCDC_HandleTypeDef *hlcdc,
                                         uint16_t src_x0, uint16_t src_y0,
                                         uint16_t src_x1, uint16_t src_y1,
                                         uint16_t *upd_x0, uint16_t *upd_y0,
                                         uint16_t *upd_x1, uint16_t *upd_y1)
{
    if (upd_x0 == NULL || upd_y0 == NULL || upd_x1 == NULL || upd_y1 == NULL)
    {
        return;
    }

    *upd_x0 = src_x0;
    *upd_y0 = src_y0;
    *upd_x1 = src_x1;
    *upd_y1 = src_y1;

    if (hlcdc == NULL)
    {
        return;
    }

    if ((hlcdc->roi.x0 <= hlcdc->roi.x1) && (hlcdc->roi.y0 <= hlcdc->roi.y1))
    {
        uint16_t roi_x0 = (uint16_t)hlcdc->roi.x0;
        uint16_t roi_y0 = (uint16_t)hlcdc->roi.y0;
        uint16_t roi_x1 = (uint16_t)hlcdc->roi.x1;
        uint16_t roi_y1 = (uint16_t)hlcdc->roi.y1;

        if (roi_x0 < *upd_x0) roi_x0 = *upd_x0;
        if (roi_y0 < *upd_y0) roi_y0 = *upd_y0;
        if (roi_x1 > *upd_x1) roi_x1 = *upd_x1;
        if (roi_y1 > *upd_y1) roi_y1 = *upd_y1;

        if (roi_x0 <= roi_x1 && roi_y0 <= roi_y1)
        {
            *upd_x0 = roi_x0;
            *upd_y0 = roi_y0;
            *upd_x1 = roi_x1;
            *upd_y1 = roi_y1;
        }
    }
}

static uint8_t EPD_ShouldFlushCurrentROI(LCDC_HandleTypeDef *hlcdc,
                                         uint16_t src_x1, uint16_t src_y1)
{
    if (hlcdc == NULL)
    {
        return 1;
    }

    if ((hlcdc->roi.x0 <= hlcdc->roi.x1) && (hlcdc->roi.y0 <= hlcdc->roi.y1))
    {
        return (src_x1 >= (uint16_t)hlcdc->roi.x1) &&
               (src_y1 >= (uint16_t)hlcdc->roi.y1);
    }

    return 1;
}

static uint8_t *EPD_AllocMonoRegionBuffer(const uint8_t *src, uint16_t x0,
                                          uint16_t y0, uint16_t x1,
                                          uint16_t y1, uint32_t *out_len)
{
    uint16_t src_stride = (uint16_t)(EPD_WIDTH / 8U);
    uint16_t x0_byte = (uint16_t)(x0 / 8U);
    uint16_t x1_byte = (uint16_t)(x1 / 8U);
    uint16_t row_bytes = (uint16_t)(x1_byte - x0_byte + 1U);
    uint16_t height = (uint16_t)(y1 - y0 + 1U);
    uint32_t buf_len = (uint32_t)row_bytes * (uint32_t)height;
    if (out_len != NULL)
    {
        *out_len = 0;
    }

    if (src == NULL || row_bytes == 0U || height == 0U)
    {
        return RT_NULL;
    }

    if (buf_len > sizeof(s_partial_region_buffer))
    {
        rt_kprintf("EPD partial region buffer overflow, len=%lu max=%lu\n",
                   (unsigned long)buf_len,
                   (unsigned long)sizeof(s_partial_region_buffer));
        return RT_NULL;
    }

    for (uint16_t row = 0; row < height; row++)
    {
        const uint8_t *src_row = src + ((uint32_t)(y0 + row) * src_stride) + x0_byte;
        memcpy(s_partial_region_buffer + ((uint32_t)row * row_bytes), src_row, row_bytes);
    }

    if (out_len != NULL)
    {
        *out_len = buf_len;
    }

    return s_partial_region_buffer;
}

/**
 * @brief 初始化帧缓冲区为全白
 */
static void EPD_FrameBuffer_Init(void)
{
    memset(mixed_framebuffer, 0xFF, sizeof(mixed_framebuffer)); /* default white (3) by packed bytes */
    memset(mixed_framebuffer_mono, 0xFF, sizeof(mixed_framebuffer_mono));
    memset(mixed_framebuffer_prev_mono, 0xFF, sizeof(mixed_framebuffer_prev_mono));
    framebuffer_initialized = 1;
    EPD_ClearDirtyRegion();
    s_partial_refresh_count = 0;
}

/**
 * @brief 更新帧缓冲区指定区域的数据
 * @param data 源数据指针(RGB332格式，每字节一个像素)
 * @param x0 起始X坐标
 * @param y0 起始Y坐标
 * @param x1 结束X坐标
 * @param y1 结束Y坐标
 * @note 将RGB332数据转换为2bpp（4色阶）格式存入缓冲区
 */
static void EPD_FrameBuffer_UpdateRegion(const uint8_t *data, uint16_t x0, uint16_t y0, 
                                          uint16_t x1, uint16_t y1)
{
    if (data == NULL || x0 > x1 || y0 > y1)
    {
        return;
    }
    
    // 边界检查
    if (x1 >= EPD_LOGICAL_WIDTH) x1 = EPD_LOGICAL_WIDTH - 1;
    if (y1 >= EPD_LOGICAL_HEIGHT) y1 = EPD_LOGICAL_HEIGHT - 1;
    
    if (!framebuffer_initialized)
    {
        EPD_FrameBuffer_Init();
    }
    
    uint16_t src_width = x1 - x0 + 1;
    
    for (uint16_t y = y0; y <= y1; y++)
    {
        for (uint16_t x = x0; x <= x1; x++)
        {
            // 计算源数据中的位置
            uint32_t src_idx = (y - y0) * src_width + (x - x0);
            uint8_t pixel = data[src_idx];
            
            // 将RGB332转换为4级灰度（0=黑,1=深灰,2=浅灰,3=白）
            // RGB332: RRRGGGBB, 计算灰度值
            uint8_t r = (pixel >> 5) & 0x07;  // 3 bits
            uint8_t g = (pixel >> 2) & 0x07;  // 3 bits
            uint8_t b = pixel & 0x03;         // 2 bits
            uint16_t gray = (uint16_t)r * 36 + (uint16_t)g * 36 + (uint16_t)b * 64;  // 0-252
            uint8_t gray2 = (gray < 64) ? 0 : (gray < 128) ? 1 : (gray < 192) ? 2 : 3;
            uint16_t px, py;
            epd_map_logical_to_physical(x, y, &px, &py);
            epd_set_gray2_pixel(px, py, gray2);
        }
    }
}

static void EPD_FrameBuffer_UpdateRegionFromSrcArea(const uint8_t *data,
                                                    uint16_t src_x0, uint16_t src_y0,
                                                    uint16_t src_x1, uint16_t src_y1,
                                                    uint16_t upd_x0, uint16_t upd_y0,
                                                    uint16_t upd_x1, uint16_t upd_y1)
{
    if (data == NULL || src_x0 > src_x1 || src_y0 > src_y1 ||
        upd_x0 > upd_x1 || upd_y0 > upd_y1)
    {
        return;
    }

    if (upd_x0 < src_x0 || upd_y0 < src_y0 || upd_x1 > src_x1 || upd_y1 > src_y1)
    {
        return;
    }

    if (!framebuffer_initialized)
    {
        EPD_FrameBuffer_Init();
    }

    uint16_t src_width = (uint16_t)(src_x1 - src_x0 + 1U);

    for (uint16_t y = upd_y0; y <= upd_y1; y++)
    {
        for (uint16_t x = upd_x0; x <= upd_x1; x++)
        {
            uint32_t src_idx = (uint32_t)(y - src_y0) * src_width + (uint32_t)(x - src_x0);
            uint8_t pixel = data[src_idx];
            uint8_t r = (pixel >> 5) & 0x07;
            uint8_t g = (pixel >> 2) & 0x07;
            uint8_t b = pixel & 0x03;
            uint16_t gray = (uint16_t)r * 36 + (uint16_t)g * 36 + (uint16_t)b * 64;
            uint8_t gray2 = (gray < 64) ? 0 : (gray < 128) ? 1 : (gray < 192) ? 2 : 3;
            uint16_t px, py;
            epd_map_logical_to_physical(x, y, &px, &py);
            epd_set_gray2_pixel(px, py, gray2);
        }
    }
}

static void EPD_FrameBuffer_UpdateRegionRGB565(const uint8_t *data, uint16_t x0, uint16_t y0,
                                                uint16_t x1, uint16_t y1)
{
    if (data == NULL || x0 > x1 || y0 > y1)
    {
        return;
    }

    if (x1 >= EPD_LOGICAL_WIDTH) x1 = EPD_LOGICAL_WIDTH - 1;
    if (y1 >= EPD_LOGICAL_HEIGHT) y1 = EPD_LOGICAL_HEIGHT - 1;

    if (!framebuffer_initialized)
    {
        EPD_FrameBuffer_Init();
    }

    uint16_t src_width = x1 - x0 + 1;

    for (uint16_t y = y0; y <= y1; y++)
    {
        for (uint16_t x = x0; x <= x1; x++)
        {
            uint32_t src_idx = (uint32_t)((y - y0) * src_width + (x - x0));
            uint16_t pixel565 = (uint16_t)data[src_idx * 2] | ((uint16_t)data[src_idx * 2 + 1] << 8);

            uint8_t r5 = (uint8_t)((pixel565 >> 11) & 0x1F);
            uint8_t g6 = (uint8_t)((pixel565 >> 5) & 0x3F);
            uint8_t b5 = (uint8_t)(pixel565 & 0x1F);
            uint8_t r8 = (uint8_t)((r5 << 3) | (r5 >> 2));
            uint8_t g8 = (uint8_t)((g6 << 2) | (g6 >> 4));
            uint8_t b8 = (uint8_t)((b5 << 3) | (b5 >> 2));

            uint16_t gray = (uint16_t)(r8 * 30 + g8 * 59 + b8 * 11) / 100;
            uint8_t gray2 = (gray < 64) ? 0 : (gray < 128) ? 1 : (gray < 192) ? 2 : 3;
            uint16_t px, py;
            epd_map_logical_to_physical(x, y, &px, &py);
            epd_set_gray2_pixel(px, py, gray2);
        }
    }
}

static void EPD_FrameBuffer_UpdateRegionRGB565FromSrcArea(const uint8_t *data,
                                                          uint16_t src_x0, uint16_t src_y0,
                                                          uint16_t src_x1, uint16_t src_y1,
                                                          uint16_t upd_x0, uint16_t upd_y0,
                                                          uint16_t upd_x1, uint16_t upd_y1)
{
    if (data == NULL || src_x0 > src_x1 || src_y0 > src_y1 ||
        upd_x0 > upd_x1 || upd_y0 > upd_y1)
    {
        return;
    }

    if (upd_x0 < src_x0 || upd_y0 < src_y0 || upd_x1 > src_x1 || upd_y1 > src_y1)
    {
        return;
    }

    if (!framebuffer_initialized)
    {
        EPD_FrameBuffer_Init();
    }

    uint16_t src_width = (uint16_t)(src_x1 - src_x0 + 1U);

    for (uint16_t y = upd_y0; y <= upd_y1; y++)
    {
        for (uint16_t x = upd_x0; x <= upd_x1; x++)
        {
            uint32_t src_idx = (uint32_t)(y - src_y0) * src_width + (uint32_t)(x - src_x0);
            uint16_t pixel565 = (uint16_t)data[src_idx * 2U] |
                                ((uint16_t)data[src_idx * 2U + 1U] << 8);

            uint8_t r5 = (uint8_t)((pixel565 >> 11) & 0x1F);
            uint8_t g6 = (uint8_t)((pixel565 >> 5) & 0x3F);
            uint8_t b5 = (uint8_t)(pixel565 & 0x1F);
            uint8_t r8 = (uint8_t)((r5 << 3) | (r5 >> 2));
            uint8_t g8 = (uint8_t)((g6 << 2) | (g6 >> 4));
            uint8_t b8 = (uint8_t)((b5 << 3) | (b5 >> 2));

            uint16_t gray = (uint16_t)(r8 * 30 + g8 * 59 + b8 * 11) / 100;
            uint8_t gray2 = (gray < 64) ? 0 : (gray < 128) ? 1 : (gray < 192) ? 2 : 3;
            uint16_t px, py;
            epd_map_logical_to_physical(x, y, &px, &py);
            epd_set_gray2_pixel(px, py, gray2);
        }
    }
}

/**
 * @brief 更新帧缓冲区指定区域的数据（1bpp格式直接拷贝）
 * @param data 源数据指针(1bpp格式)
 * @param x0 起始X坐标（必须是8的倍数）
 * @param y0 起始Y坐标
 * @param x1 结束X坐标
 * @param y1 结束Y坐标
 */
static void EPD_FrameBuffer_UpdateRegion1bpp(const uint8_t *data, uint16_t x0, uint16_t y0, 
                                              uint16_t x1, uint16_t y1)
{
    if (data == NULL || x0 > x1 || y0 > y1)
    {
        return;
    }
    
    // 边界检查
    if (x1 >= EPD_LOGICAL_WIDTH) x1 = EPD_LOGICAL_WIDTH - 1;
    if (y1 >= EPD_LOGICAL_HEIGHT) y1 = EPD_LOGICAL_HEIGHT - 1;
    
    if (!framebuffer_initialized)
    {
        EPD_FrameBuffer_Init();
    }
    
    // 将1bpp源数据映射到2bpp灰度缓存（0->黑，1->白）
    uint16_t x0_byte = x0 / 8;
    uint16_t x1_byte = x1 / 8;
    uint16_t src_bytes_per_row = x1_byte - x0_byte + 1;
    
    for (uint16_t y = y0; y <= y1; y++)
    {
        uint32_t src_offset = (y - y0) * src_bytes_per_row;
        for (uint16_t x = x0; x <= x1; x++)
        {
            uint32_t src_byte_idx = src_offset + ((x - x0) / 8);
            uint8_t src_bit = (uint8_t)(7 - ((x - x0) % 8));
            uint8_t mono = (uint8_t)((data[src_byte_idx] >> src_bit) & 0x1u);
            uint16_t px, py;
            epd_map_logical_to_physical(x, y, &px, &py);
            epd_set_gray2_pixel(px, py, mono ? 3 : 0);
        }
    }
}

/**
 * @brief 将帧缓冲区全屏刷新到墨水屏
 * @param hlcdc LCDC句柄
 */
static void EPD_FrameBuffer_Flush(LCDC_HandleTypeDef *hlcdc)
{
    rt_tick_t refresh_start;
    rt_tick_t refresh_end;

    if (!framebuffer_initialized)
    {
        EPD_FrameBuffer_Init();
    }

    EPD_ReadBusy();
    EPD_LoadLUT(hlcdc, 0);

#if EPD_USE_SCAN_MODE3
    EPD_SetFullWindow(hlcdc);
#endif

#if EPD_USE_GRAY2_REFRESH
    if (s_epd_stats_dump_count < 3U)
    {
        epd_debug_dump_stats("flush_gc_gray2");
        s_epd_stats_dump_count++;
    }
    EPD_SendCommandDataBuf(hlcdc, REG_WRITE_NEW_DATA, mixed_framebuffer,
                           EPD_GRAY2_FRAME_SIZE);
#else
    // 使用Layer方式发送帧缓冲区数据
    EPD_Gray2ToMono();
    if (s_epd_stats_dump_count < 3U)
    {
        epd_debug_dump_stats("flush_gc");
        s_epd_stats_dump_count++;
    }

    /* The new panel sample updates visible content through 0x13 only. */
    EPD_SendCommandDataBuf(hlcdc, REG_WRITE_NEW_DATA, mixed_framebuffer_mono,
                           EPD_MONO_FRAME_SIZE);
#endif

    // 刷新显示
    s_epd_refresh_fallback_ms = EPD_GC_FULL_REFRESH_FALLBACK_MS;
    refresh_start = rt_tick_get();
    EPD_Refresh(hlcdc);
    refresh_end = rt_tick_get();
    rt_kprintf("[standby_dbg] epd full refresh done elapsed=%u ms\n",
               epd_ticks_to_ms(refresh_end - refresh_start));
#if !EPD_USE_GRAY2_REFRESH
    memcpy(mixed_framebuffer_prev_mono, mixed_framebuffer_mono,
           sizeof(mixed_framebuffer_prev_mono));
#endif
    s_partial_refresh_count = 0;
    EPD_ClearDirtyRegion();
}

static void EPD_FrameBuffer_FlushFast(LCDC_HandleTypeDef *hlcdc)
{
    rt_tick_t refresh_start;
    rt_tick_t refresh_end;

    if (!framebuffer_initialized)
    {
        EPD_FrameBuffer_Init();
    }

    if (s_epd_mono_dither_enabled)
    {
        EPD_FrameBuffer_Flush(hlcdc);
        return;
    }

    EPD_ReadBusy();
    EPD_LoadLUT(hlcdc, 2);
#if EPD_USE_SCAN_MODE3
    EPD_SetFullWindow(hlcdc);
#endif

#if EPD_USE_GRAY2_REFRESH
    /* DU is mono-only on this panel path; keep the grayscale experiment on GC refresh. */
    EPD_FrameBuffer_Flush(hlcdc);
    return;
#else
    EPD_Gray2ToMono();
    if (s_epd_stats_dump_count < 3U)
    {
        epd_debug_dump_stats("flush_fast");
        s_epd_stats_dump_count++;
    }
    EPD_SendCommandDataBuf(hlcdc, REG_WRITE_NEW_DATA, mixed_framebuffer_mono,
                           EPD_MONO_FRAME_SIZE);

    refresh_start = rt_tick_get();
    EPD_Refresh(hlcdc);
    refresh_end = rt_tick_get();
    rt_kprintf("[standby_dbg] epd fast full refresh done elapsed=%u ms partial_count=%u\n",
               epd_ticks_to_ms(refresh_end - refresh_start),
               (unsigned int)s_partial_refresh_count);

    memcpy(mixed_framebuffer_prev_mono, mixed_framebuffer_mono,
           sizeof(mixed_framebuffer_prev_mono));
    if (s_partial_refresh_count < 0xFF)
    {
        s_partial_refresh_count++;
    }
    EPD_ClearDirtyRegion();
#endif
}

static void EPD_FrameBuffer_FlushGray2(LCDC_HandleTypeDef *hlcdc)
{
    EPD_FrameBuffer_Flush(hlcdc);
}

/**
 * @brief 清空帧缓冲区（填充白色）
 */
static void EPD_FrameBuffer_Clear(void)
{
    memset(mixed_framebuffer, 0xFF, sizeof(mixed_framebuffer));
    memset(mixed_framebuffer_mono, 0xFF, sizeof(mixed_framebuffer_mono));
    memset(mixed_framebuffer_prev_mono, 0xFF, sizeof(mixed_framebuffer_prev_mono));
    framebuffer_initialized = 1;
    EPD_ClearDirtyRegion();
    s_partial_refresh_count = 0;
}

static void EPD_DrawCenteredCircleTest(void)
{
    int32_t cx = EPD_WIDTH / 2;
    int32_t cy = EPD_HEIGHT / 2;
    int32_t r = (EPD_WIDTH < EPD_HEIGHT ? EPD_WIDTH : EPD_HEIGHT) / 4;
    int32_t r2 = r * r;

    EPD_FrameBuffer_Clear();

    for (int32_t y = 0; y < EPD_HEIGHT; y++)
    {
        int32_t dy = y - cy;
        for (int32_t x = 0; x < EPD_WIDTH; x++)
        {
            int32_t dx = x - cx;
            if ((dx * dx + dy * dy) <= r2)
            {
                epd_set_gray2_pixel((uint16_t)x, (uint16_t)y, 0);
            }
        }
    }
}

/**
 * @brief 填充帧缓冲区（填充黑色）
 */
static void EPD_FrameBuffer_Fill(void)
{
    memset(mixed_framebuffer, 0x00, sizeof(mixed_framebuffer));
    memset(mixed_framebuffer_mono, 0x00, sizeof(mixed_framebuffer_mono));
    memset(mixed_framebuffer_prev_mono, 0x00, sizeof(mixed_framebuffer_prev_mono));
    framebuffer_initialized = 1;
}

/**
 * @brief 获取帧缓冲区指针
 * @return 帧缓冲区指针
 */
static uint8_t* EPD_FrameBuffer_GetPtr(void)
{
    if (!framebuffer_initialized)
    {
        EPD_FrameBuffer_Init();
    }
    return mixed_framebuffer;
}
// 发送命令（不带数据）
static void EPD_SendCommand(LCDC_HandleTypeDef *hlcdc, uint8_t cmd)
{
    HAL_LCDC_WriteU8Reg(hlcdc, cmd, NULL, 0);
}

// 发送命令+单字节数据
static void EPD_SendCommandData(LCDC_HandleTypeDef *hlcdc, uint8_t cmd,
                                uint8_t data)
{
    HAL_LCDC_WriteU8Reg(hlcdc, cmd, &data, 1);
}

// 发送命令+多字节数据
static void EPD_SendCommandDataBuf(LCDC_HandleTypeDef *hlcdc, uint8_t cmd,
                                   const uint8_t *data, uint32_t len)
{
    HAL_LCDC_WriteU8Reg(hlcdc, cmd, (uint8_t *)data, len);
}

static void EPD_SetFullWindow(LCDC_HandleTypeDef *hlcdc)
{
#if !EPD_ENABLE_WINDOW_CMDS
    (void)hlcdc;
    return;
#else
    uint16_t x_start = EPD_WIN_X_START;
    uint16_t y_start = EPD_WIN_Y_START;
    uint16_t x_end = (uint16_t)(EPD_WIN_X_START + EPD_WIN_WIDTH - 1);
    uint16_t y_end = (uint16_t)(EPD_WIN_Y_START + EPD_WIN_HEIGHT - 1);

    if (x_end >= EPD_SCAN_SRC_PIXELS) x_end = EPD_SCAN_SRC_PIXELS - 1;
    if (y_end >= EPD_SCAN_GATE_PIXELS) y_end = EPD_SCAN_GATE_PIXELS - 1;

    uint8_t win[9] = {
        (uint8_t)(x_start >> 8), (uint8_t)(x_start & 0xFF),
        (uint8_t)(x_end >> 8), (uint8_t)(x_end & 0xFF),
        (uint8_t)(y_start >> 8), (uint8_t)(y_start & 0xFF),
        (uint8_t)(y_end >> 8), (uint8_t)(y_end & 0xFF),
        0x01
    };
    EPD_SendCommand(hlcdc, 0x91);
    EPD_SendCommandDataBuf(hlcdc, 0x90, win, sizeof(win));
#endif
}

static void EPD_SetPartialWindow(LCDC_HandleTypeDef *hlcdc, uint16_t x0,
                                 uint16_t y0, uint16_t x1, uint16_t y1)
{
#if !EPD_ENABLE_WINDOW_CMDS
    (void)hlcdc;
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    return;
#else
    uint16_t x_start = (uint16_t)(x0 & ~0x7u);
    uint16_t x_end = (uint16_t)(x1 | 0x7u);
    uint16_t y_start = (uint16_t)(EPD_WIN_Y_START + y0);
    uint16_t y_end = (uint16_t)(EPD_WIN_Y_START + y1);
    uint8_t win[9] = {
        (uint8_t)(x_start >> 8), (uint8_t)(x_start & 0xFF),
        (uint8_t)(x_end >> 8), (uint8_t)(x_end & 0xFF),
        (uint8_t)(y_start >> 8), (uint8_t)(y_start & 0xFF),
        (uint8_t)(y_end >> 8), (uint8_t)(y_end & 0xFF),
        0x01
    };

    if (x_end >= EPD_SCAN_SRC_PIXELS) x_end = EPD_SCAN_SRC_PIXELS - 1;
    if (y_end >= EPD_SCAN_GATE_PIXELS) y_end = EPD_SCAN_GATE_PIXELS - 1;
    win[2] = (uint8_t)(x_end >> 8);
    win[3] = (uint8_t)(x_end & 0xFF);
    win[6] = (uint8_t)(y_end >> 8);
    win[7] = (uint8_t)(y_end & 0xFF);

    EPD_SendCommand(hlcdc, 0x91);
    EPD_SendCommandDataBuf(hlcdc, 0x90, win, sizeof(win));
#endif
}

static void EPD_FrameBuffer_FlushRegion(LCDC_HandleTypeDef *hlcdc, uint16_t x0,
                                        uint16_t y0, uint16_t x1, uint16_t y1)
{
#if EPD_USE_GRAY2_REFRESH
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    EPD_FrameBuffer_FlushGray2(hlcdc);
    return;
#else
    uint8_t *old_region;
    uint8_t *new_region;
    uint32_t old_len;
    uint32_t new_len;
    rt_tick_t refresh_start;
    rt_tick_t refresh_end;
    uint16_t src_stride;
    uint16_t x0_byte;
    uint16_t x1_byte;
    uint16_t row_bytes;

    if (!framebuffer_initialized)
    {
        EPD_FrameBuffer_Init();
    }

    if (s_epd_mono_dither_enabled)
    {
        EPD_FrameBuffer_Flush(hlcdc);
        return;
    }

    x0 = (uint16_t)(x0 & ~0x7u);
    x1 = (uint16_t)(x1 | 0x7u);
    if (x1 >= EPD_WIDTH) x1 = EPD_WIDTH - 1;
    if (y1 >= EPD_HEIGHT) y1 = EPD_HEIGHT - 1;

    EPD_ReadBusy();
    EPD_LoadLUT(hlcdc, 2);
    EPD_Gray2ToMono();
    EPD_SetPartialWindow(hlcdc, x0, y0, x1, y1);

    old_region = EPD_AllocMonoRegionBuffer(mixed_framebuffer_prev_mono, x0, y0, x1,
                                           y1, &old_len);
    if (old_region == RT_NULL)
    {
        rt_kprintf("EPD partial old region alloc failed, fallback to full refresh\n");
        EPD_FrameBuffer_Flush(hlcdc);
        return;
    }

    /*
     * EPD_AllocMonoRegionBuffer reuses one static workspace.
     * Send old data first, then refill workspace with new data.
     */
    s_epd_refresh_fallback_ms = EPD_PARTIAL_REFRESH_FALLBACK_MS;
    EPD_SendCommandDataBuf(hlcdc, REG_WRITE_OLD_DATA, old_region, old_len);

    new_region = EPD_AllocMonoRegionBuffer(mixed_framebuffer_mono, x0, y0, x1,
                                           y1, &new_len);
    if (new_region == RT_NULL)
    {
        rt_kprintf("EPD partial new region alloc failed, fallback to full refresh\n");
        EPD_FrameBuffer_Flush(hlcdc);
        return;
    }
    if (old_len != new_len)
    {
        rt_kprintf("EPD partial region size mismatch old=%lu new=%lu, fallback to full refresh\n",
                   (unsigned long)old_len,
                   (unsigned long)new_len);
        EPD_FrameBuffer_Flush(hlcdc);
        return;
    }
    EPD_SendCommandDataBuf(hlcdc, REG_WRITE_NEW_DATA, new_region, new_len);
    refresh_start = rt_tick_get();
    EPD_Refresh(hlcdc);
    refresh_end = rt_tick_get();
    rt_kprintf("[standby_dbg] epd partial refresh done area=(%d,%d)-(%d,%d) elapsed=%u ms partial_count=%u\n",
               x0, y0, x1, y1,
               epd_ticks_to_ms(refresh_end - refresh_start),
               (unsigned int)s_partial_refresh_count);

    src_stride = (uint16_t)(EPD_WIDTH / 8U);
    x0_byte = (uint16_t)(x0 / 8U);
    x1_byte = (uint16_t)(x1 / 8U);
    row_bytes = (uint16_t)(x1_byte - x0_byte + 1U);

    for (uint16_t row = y0; row <= y1; row++)
    {
        uint8_t *dst = mixed_framebuffer_prev_mono + ((uint32_t)row * src_stride) + x0_byte;
        const uint8_t *src = mixed_framebuffer_mono + ((uint32_t)row * src_stride) + x0_byte;
        memcpy(dst, src, row_bytes);
    }

    if (s_partial_refresh_count < 0xFF)
    {
        s_partial_refresh_count++;
    }
    EPD_ClearDirtyRegion();
#endif
}
static void epd_busy_callback(void *args)
{
    rt_sem_release(epd_busy_sem);
    rt_pin_irq_enable(2, PIN_IRQ_DISABLE);
}

static void epd_sem_init(void)
{
    if (epd_busy_sem != RT_NULL)
    {
        return;
    }
    epd_busy_sem = rt_sem_create("epd_busy", 0, RT_IPC_FLAG_FIFO);
    if (epd_busy_sem == RT_NULL)
    {
        rt_kprintf("EPD busy semaphore create failed!\n");
        return;
    }
    rt_pin_mode(2, PIN_MODE_INPUT);
    rt_err_t irq_ret = rt_pin_attach_irq(2, PIN_IRQ_MODE_HIGH_LEVEL,
                                         epd_busy_callback, RT_NULL);
    if (irq_ret != RT_EOK)
    {
        rt_kprintf("EPD BUSY IRQ attach failed!\n");
        rt_sem_delete(epd_busy_sem);
        epd_busy_sem = RT_NULL;
        return;
    }
    rt_pin_irq_enable(2, PIN_IRQ_ENABLE);
}

static void EPD_ReadBusy(void)
{
    /* Follow vendor sample: busy=0 means busy, wait until BUSY goes high. */
    rt_tick_t start = rt_tick_get();
    rt_tick_t timeout = rt_tick_from_millisecond(10000);
    rt_bool_t is_refresh_stage = RT_FALSE;
    rt_bool_t is_idle_stage = RT_FALSE;

    if ((s_epd_busy_stage != RT_NULL) &&
        (strncmp(s_epd_busy_stage, "refresh:", 8) == 0))
    {
        is_refresh_stage = RT_TRUE;
    }
    else if ((s_epd_busy_stage != RT_NULL) &&
             (strcmp(s_epd_busy_stage, "idle") == 0))
    {
        is_idle_stage = RT_TRUE;
        timeout = rt_tick_from_millisecond(30);
    }

    while (rt_pin_read(2) == 0)
    {
#if !EPD_TEST_STRICT_BUSY_WAIT
        if (is_refresh_stage)
        {
            rt_kprintf("EPD busy fallback delay stage=%s wait=%lu ms pin=%d\n",
                       s_epd_busy_stage,
                       (unsigned long)s_epd_refresh_fallback_ms,
                       rt_pin_read(2));
            rt_thread_mdelay((rt_int32_t)s_epd_refresh_fallback_ms);
            break;
        }
#endif
        if ((rt_tick_get() - start) > timeout)
        {
            if (is_idle_stage)
            {
                rt_kprintf("EPD busy idle skip stage=%s pin=%d\n",
                           s_epd_busy_stage != RT_NULL ? s_epd_busy_stage : "?",
                           rt_pin_read(2));
            }
            else if (is_refresh_stage)
            {
                rt_kprintf("EPD busy refresh timeout! stage=%s pin=%d\n",
                           s_epd_busy_stage != RT_NULL ? s_epd_busy_stage : "?",
                           rt_pin_read(2));
            }
            else
            {
                rt_kprintf("EPD busy wait timeout! stage=%s pin=%d\n",
                           s_epd_busy_stage != RT_NULL ? s_epd_busy_stage : "?",
                           rt_pin_read(2));
            }
            break;
        }
        rt_thread_mdelay(5);
    }
    rt_thread_mdelay(2);
}

static void EPD_Reset(LCDC_HandleTypeDef *hlcdc)
{
    rt_pin_write(0, 0);
    rt_thread_mdelay(10);
    rt_pin_write(0, 1);
    rt_thread_mdelay(100);
    s_epd_busy_stage = "reset";
    EPD_ReadBusy();

    LUT_Flag = 0;
}
static void LCD_ReadMode(LCDC_HandleTypeDef *hlcdc, bool enable)
{
    if (HAL_LCDC_IS_SPI_IF(lcdc_int_cfg.lcd_itf))
    {
        if (enable)
        {
            HAL_LCDC_SetFreq(hlcdc, 2800000);
        }
        else
        {
            HAL_LCDC_SetFreq(hlcdc, lcdc_int_cfg.freq);
        }
    }
}

static void LCD_Drv_Init(LCDC_HandleTypeDef *hlcdc, uint8_t Mode)
{
    (void)Mode;
    memcpy(&hlcdc->Init, &lcdc_int_cfg, sizeof(LCDC_InitTypeDef));
    HAL_LCDC_Init(hlcdc);
    rt_pin_mode(0, PIN_MODE_OUTPUT);
    rt_pin_mode(2, PIN_MODE_INPUT);
    epd_sem_init();
    epd_flush_mutex_init();
    EPD_Reset(hlcdc);


    /* Start from the vendor sequence for the new UC8179 panel. */
    uint8_t scan_data[2] = {0x3F, 0x4A};
    EPD_SendCommandDataBuf(hlcdc, 0x00, scan_data, 2);

    EPD_SendCommandData(hlcdc, 0x03, 0x10);

    uint8_t power_data[5] = {0x03, 0x00, 0x78, 0x78, 0x17};
    EPD_SendCommandDataBuf(hlcdc, 0x01, power_data, 5);

    {
        uint8_t boost_data[3] = {0x25, 0x25, 0x3C};
        EPD_SendCommandDataBuf(hlcdc, 0x06, boost_data, 3);
    }

    EPD_SendCommandData(hlcdc, 0x82, 0x24);
    EPD_SendCommandData(hlcdc, 0x30, 0x0F);

    {
        uint8_t resolution_data[4] = {0x03, 0x18, 0x02, 0x58};
        EPD_SendCommandDataBuf(hlcdc, 0x61, resolution_data, 4);
    }
    s_epd_busy_stage = "init:0x61";
    EPD_ReadBusy();

    {
        uint8_t scan_addr_data[4] = {0x00, 0x00, 0x00, 0x00};
        EPD_SendCommandDataBuf(hlcdc, 0x65, scan_addr_data, 4);
    }

    EPD_SendCommandData(hlcdc, 0xE1, 0x02);

    EPD_FrameBuffer_Init();
#if EPD_USE_GRAY2_REFRESH
    EPD_SendCommandDataBuf(hlcdc, 0x10, mixed_framebuffer,
                           EPD_GRAY2_FRAME_SIZE);
#else
    EPD_SendCommandDataBuf(hlcdc, 0x10, mixed_framebuffer_mono,
                           EPD_MONO_FRAME_SIZE);
#endif

    EPD_SendCommand(hlcdc, 0x04);
    s_epd_busy_stage = "init:0x04";
    EPD_ReadBusy();
}



// EPD 刷新显示
static void EPD_Refresh(LCDC_HandleTypeDef *hlcdc)
{
    EPD_SendCommand(hlcdc, 0x12);
    s_epd_busy_stage = "refresh:0x12";
    EPD_ReadBusy();
    s_epd_busy_stage = "idle";
#if EPD_USE_SCAN_MODE3
    EPD_SendCommand(hlcdc, 0x92);
#endif
}

// 加载 GC 模式的 LUT 表
// 注意: 需要定义 LUT_GC 数组(282字节)
static void LUTGC(LCDC_HandleTypeDef *hlcdc)
{
    EPD_SendCommandData(hlcdc, 0x50, 0x97);
    EPD_SendCommandDataBuf(hlcdc, 0x20, &LUT_GC[0], 49);
    EPD_SendCommandDataBuf(hlcdc, 0x21, &LUT_GC[49], 49);
    EPD_SendCommandDataBuf(hlcdc, 0x22, &LUT_GC[98], 49);
    EPD_SendCommandDataBuf(hlcdc, 0x23, &LUT_GC[147], 49);
    EPD_SendCommandDataBuf(hlcdc, 0x24, &LUT_GC[196], 49);
}

void EPD_Clear(LCDC_HandleTypeDef *hlcdc)
{
    LUTGC(hlcdc);
    EPD_FrameBuffer_Clear();
#if EPD_USE_GRAY2_REFRESH
    EPD_SendCommandDataBuf(hlcdc, 0x13, mixed_framebuffer, EPD_GRAY2_FRAME_SIZE);
#else
    EPD_SendCommandDataBuf(hlcdc, 0x13, mixed_framebuffer_mono, EPD_MONO_FRAME_SIZE);
#endif
    EPD_Refresh(hlcdc);
    EPD_Refresh(hlcdc);
}


void EPD_Sleep(LCDC_HandleTypeDef *hlcdc)
{
    EPD_SendCommandData(hlcdc, 0x10, 0x01); // DEEP_SLEEP_MODE
}

static void LCD_Init(LCDC_HandleTypeDef *hlcdc)
{
    LCD_Drv_Init(hlcdc, EPD_FULL);
    rt_kprintf("EPD initialized\n");
    // rt_thread_mdelay(1000);
    rt_kprintf("EPD initialized\n");
    EPD_Clear(hlcdc);
    rt_kprintf("EPD initialized\n");
#if EPD_SELF_TEST_PATTERN
    EPD_DrawCenteredCircleTest();
    EPD_FrameBuffer_Flush(hlcdc);
#endif
    // rt_thread_mdelay(10000);
}

static uint32_t LCD_ReadID(LCDC_HandleTypeDef *hlcdc)
{
    (void)hlcdc;
    return EPD_LCD_ID;
}
static void LCD_DisplayOn(LCDC_HandleTypeDef *hlcdc)
{
    (void)hlcdc;
}

static void LCD_DisplayOff(LCDC_HandleTypeDef *hlcdc)
{
}
static void LCD_SetRegion(LCDC_HandleTypeDef *hlcdc, uint16_t Xpos0,
                          uint16_t Ypos0, uint16_t Xpos1, uint16_t Ypos1)
{
    if (Xpos1 >= EPD_LOGICAL_WIDTH) Xpos1 = EPD_LOGICAL_WIDTH - 1;
    if (Ypos1 >= EPD_LOGICAL_HEIGHT) Ypos1 = EPD_LOGICAL_HEIGHT - 1;
    HAL_LCDC_SetROIArea(hlcdc, Xpos0, Ypos0, Xpos1, Ypos1);
}

static void LCD_WritePixel(LCDC_HandleTypeDef *hlcdc, uint16_t Xpos,
                           uint16_t Ypos, const uint8_t *RGBCode)
{
    if (RGBCode == NULL || Xpos >= EPD_LOGICAL_WIDTH || Ypos >= EPD_LOGICAL_HEIGHT)
    {
        return;
    }
    rt_kprintf("EPD single pixel: (%d,%d)\n", Xpos, Ypos);
    epd_flush_lock();
    // 更新帧缓冲区单个像素
    if (g_input_color_mode == RTGRAPHIC_PIXEL_FORMAT_RGB565 ||
        g_input_color_mode == LCDC_PIXEL_FORMAT_RGB565)
    {
        EPD_FrameBuffer_UpdateRegionRGB565(RGBCode, Xpos, Ypos, Xpos, Ypos);
    }
    else
    {
        EPD_FrameBuffer_UpdateRegion(RGBCode, Xpos, Ypos, Xpos, Ypos);
    }
    EPD_MarkDirtyLogicalRegion(Xpos, Ypos, Xpos, Ypos);
    epd_flush_unlock();
    
    // 注意: 单像素写入不会立即刷新屏幕，需要调用者手动触发刷新
    // 如果需要立即刷新，取消下面的注释
    // EPD_FrameBuffer_Flush(hlcdc);
}

static void LCD_WriteMultiplePixels(LCDC_HandleTypeDef *hlcdc,
                                    const uint8_t *RGBCode, uint16_t Xpos0,
                                    uint16_t Ypos0, uint16_t Xpos1,
                                    uint16_t Ypos1)
{
    uint8_t should_flush_now;
    uint16_t upd_x0;
    uint16_t upd_y0;
    uint16_t upd_x1;
    uint16_t upd_y1;
    uint8_t prefer_full_refresh;

    if (RGBCode == NULL || Xpos0 > Xpos1 || Ypos0 > Ypos1)
    {
        rt_kprintf("EPD multiple pixels param error\n");
        return;
    }
    epd_flush_lock();
    EPD_GetEffectiveUpdateRegion(hlcdc, Xpos0, Ypos0, Xpos1, Ypos1,
                                 &upd_x0, &upd_y0, &upd_x1, &upd_y1);
    rt_kprintf("EPD multiple pixels src:(%d,%d)-(%d,%d) roi:(%d,%d)-(%d,%d)\n",
               Xpos0, Ypos0, Xpos1, Ypos1, upd_x0, upd_y0, upd_x1, upd_y1);

    if (!s_frame_collecting)
    {
        s_frame_collecting = 1;
    }

    if (g_input_color_mode == RTGRAPHIC_PIXEL_FORMAT_RGB565 ||
        g_input_color_mode == LCDC_PIXEL_FORMAT_RGB565)
    {
        EPD_FrameBuffer_UpdateRegionRGB565FromSrcArea(RGBCode, Xpos0, Ypos0,
                                                      Xpos1, Ypos1, upd_x0,
                                                      upd_y0, upd_x1, upd_y1);
    }
    else
    {
        EPD_FrameBuffer_UpdateRegionFromSrcArea(RGBCode, Xpos0, Ypos0, Xpos1,
                                                Ypos1, upd_x0, upd_y0,
                                                upd_x1, upd_y1);
    }
    EPD_MarkDirtyLogicalRegion(upd_x0, upd_y0, upd_x1, upd_y1);
    should_flush_now = EPD_ShouldFlushCurrentROI(hlcdc, Xpos1, Ypos1);

    if (!should_flush_now)
    {
        rt_kprintf("[standby_dbg] epd defer flush src=(%d,%d)-(%d,%d) roi=(%d,%d)-(%d,%d) dirty=(%d,%d)-(%d,%d)\n",
                   Xpos0, Ypos0, Xpos1, Ypos1,
                   hlcdc->roi.x0, hlcdc->roi.y0, hlcdc->roi.x1, hlcdc->roi.y1,
                   s_dirty_x0, s_dirty_y0, s_dirty_x1, s_dirty_y1);
    }
    else
    {
        rt_tick_t min_interval =
            rt_tick_from_millisecond(EPD_REFRESH_INTERVAL_MS);
        rt_tick_t now = rt_tick_get();
        rt_tick_t elapsed = now - s_last_epd_refresh_tick;

        if ((s_last_epd_refresh_tick != 0) && (elapsed < min_interval))
        {
            rt_thread_delay(min_interval - elapsed);
        }

        prefer_full_refresh = (!s_dirty_area_valid) ||
                              EPD_ShouldUseFullRefresh(s_dirty_x0,
                                                       s_dirty_y0,
                                                       s_dirty_x1,
                                                       s_dirty_y1);
#if EPD_USE_GRAY2_REFRESH
        prefer_full_refresh = 1;
#endif

        if (!prefer_full_refresh)
        {
            rt_kprintf("[standby_dbg] epd partial du flush src=(%d,%d)-(%d,%d) dirty=(%d,%d)-(%d,%d) partial_count=%u\n",
                       Xpos0, Ypos0, Xpos1, Ypos1,
                       s_dirty_x0, s_dirty_y0, s_dirty_x1, s_dirty_y1,
                       (unsigned int)s_partial_refresh_count);
            EPD_FrameBuffer_FlushRegion(hlcdc,
                                        s_dirty_x0,
                                        s_dirty_y0,
                                        s_dirty_x1,
                                        s_dirty_y1);
        }
        else
        {
            rt_kprintf("[standby_dbg] epd gc full flush src=(%d,%d)-(%d,%d) dirty_valid=%d dirty=(%d,%d)-(%d,%d) partial_count=%u\n",
                       Xpos0, Ypos0, Xpos1, Ypos1,
                       s_dirty_area_valid,
                       s_dirty_x0, s_dirty_y0, s_dirty_x1, s_dirty_y1,
                       (unsigned int)s_partial_refresh_count);
            EPD_FrameBuffer_Flush(hlcdc);
        }
        s_last_epd_refresh_tick = rt_tick_get();
        s_frame_collecting = 0;
    }

    reflesh_times++;

    /*
     * EPD path uses synchronous command writes (not LCDC layer DMA), so there is
     * no hardware transfer-complete IRQ to release drv_lcd draw_sem.
     * Notify completion explicitly to avoid draw_core timeout in lcd_task.
     */
    if (hlcdc->XferCpltCallback != NULL)
    {
        hlcdc->XferCpltCallback(hlcdc);
    }
    epd_flush_unlock();
}
static void LCD_WriteReg(LCDC_HandleTypeDef *hlcdc, uint16_t LCD_Reg,
                         uint8_t *Parameters, uint32_t NbParameters)
{
    EPD_ReadBusy();
    HAL_LCDC_WriteU8Reg(hlcdc, LCD_Reg, Parameters, NbParameters);
}
static uint32_t LCD_ReadData(LCDC_HandleTypeDef *hlcdc, uint16_t RegValue,
                             uint8_t ReadSize)
{
    uint32_t rd_data = 0;
    EPD_ReadBusy();
    LCD_ReadMode(hlcdc, true);
    HAL_LCDC_ReadU8Reg(hlcdc, RegValue, (uint8_t *)&rd_data, ReadSize);
    LCD_ReadMode(hlcdc, false);

    return rd_data;
}

static uint32_t LCD_ReadPixel(LCDC_HandleTypeDef *hlcdc, uint16_t Xpos,
                              uint16_t Ypos)
{
    if (Xpos >= LCD_PIXEL_WIDTH || Ypos >= LCD_PIXEL_HEIGHT)
    {
        LOG_W("EPD read pixel out of range");
        return 0;
    }

    LCD_SetRegion(hlcdc, Xpos, Ypos, Xpos, Ypos);
    uint8_t read_data = (uint8_t)LCD_ReadData(hlcdc, 0x2E, 1);
    uint32_t color = (read_data & 0x80) ? 0xFFFFFF : 0x000000;
    LOG_D("EPD read pixel: (%d,%d), color: 0x%x", Xpos, Ypos, color);
    return color;
}

static void LCD_SetColorMode(LCDC_HandleTypeDef *hlcdc, uint16_t color_mode)
{
    if (color_mode != RTGRAPHIC_PIXEL_FORMAT_RGB332 &&
        color_mode != LCDC_PIXEL_FORMAT_RGB332 &&
        color_mode != RTGRAPHIC_PIXEL_FORMAT_RGB565 &&
        color_mode != LCDC_PIXEL_FORMAT_RGB565)
    {
        rt_kprintf("EPD unsupported input mode: %d\n",
                   color_mode);
        return;
    }
    g_input_color_mode = color_mode;
    lcdc_int_cfg.color_mode = LCDC_PIXEL_FORMAT_RGB332;
    HAL_LCDC_SetOutFormat(hlcdc, lcdc_int_cfg.color_mode);
    // rt_kprintf("EPD set color mode: mono-color (1bit)\n");
}

static void LCD_SetBrightness(LCDC_HandleTypeDef *hlcdc, uint8_t br)
{
    rt_kprintf("bl_trace: st7789 LCD_SetBrightness=%u\n", br);
    EPD_SetBacklight(br);
}

static void epd_stat(void)
{
    EPD_Gray2ToMono();
    epd_debug_dump_stats("shell");
}
MSH_CMD_EXPORT(epd_stat, dump epd framebuffer statistics);

static void epd_test(int argc, char **argv)
{
    const char *mode = (argc > 1) ? argv[1] : "split";

    if (!framebuffer_initialized)
    {
        EPD_FrameBuffer_Init();
    }

    if (strcmp(mode, "white") == 0)
    {
        EPD_FrameBuffer_Clear();
    }
    else if (strcmp(mode, "black") == 0)
    {
        EPD_FrameBuffer_Fill();
    }
    else if (strcmp(mode, "circle") == 0)
    {
        EPD_DrawCenteredCircleTest();
    }
    else
    {
        EPD_DisplayImage(&hlcdc, PIC_LEFT_BLACK_RIGHT_WHITE);
        rt_kprintf("epd_test: mode=%s direct image command sent\n", mode);
        return;
    }

    EPD_FrameBuffer_FlushFast(&hlcdc);
    rt_kprintf("epd_test: mode=%s flushed\n", mode);
}
MSH_CMD_EXPORT(epd_test, epd_test [white|black|split|circle]);

static void LCD_IdleModeOn(LCDC_HandleTypeDef *hlcdc)
{
    EPD_EnterDeepSleep(hlcdc);
    BSP_LCD_PowerDown();
    BSP_LCD_Reset(0);
}

static void LCD_IdleModeOff(LCDC_HandleTypeDef *hlcdc)
{
    BSP_LCD_PowerUp();
    BSP_LCD_Reset(1);
    HAL_Delay(1);
    LCD_Drv_Init(hlcdc, EPD_FULL);
}
static void EPD_LoadLUT(LCDC_HandleTypeDef *hlcdc, uint8_t lut_mode)
{
    const unsigned char *lut = LUT_DU;
    uint8_t border = 0xD7;

    switch (lut_mode)
    {
    case 0:
    case 1:
        lut = LUT_GC;
        border = 0x97;
        break;
    case 2:
        lut = LUT_DU;
        border = 0xD7;
        break;
    default:
        lut = LUT_DU;
        border = 0xD7;
        break;
    }

    EPD_SendCommandData(hlcdc, 0x50, border);
    EPD_SendCommandDataBuf(hlcdc, REG_LUT_VCOM, &lut[0], 49);
    EPD_SendCommandDataBuf(hlcdc, REG_LUT_W2W, &lut[49], 49);
    EPD_SendCommandDataBuf(hlcdc, REG_LUT_K2W, &lut[98], 49);
    EPD_SendCommandDataBuf(hlcdc, REG_LUT_W2K, &lut[147], 49);
    EPD_SendCommandDataBuf(hlcdc, REG_LUT_K2K, &lut[196], 49);
}

static void EPD_DisplayImage(LCDC_HandleTypeDef *hlcdc, uint8_t img_flag)
{
    uint16_t row, col;
    uint16_t pcnt = 0;
    uint8_t *temp_buf = rt_malloc(PICTURE_LENGTH);

    if (temp_buf == RT_NULL)
    {
        rt_kprintf("EPD image buf malloc failed\n");
        return;
    }

    for (col = 0; col < LCD_VER_RES_MAX; col++)
    {
        for (row = 0; row < LCD_HOR_RES_MAX / 8; row++)
        {
            switch (img_flag)
            {
            case PIC_BLACK:
                temp_buf[pcnt] = 0x00;
                break;
            case PIC_WHITE:
                temp_buf[pcnt] = 0xFF;
                break;
            case PIC_LEFT_BLACK_RIGHT_WHITE:
                temp_buf[pcnt] = (col >= LCD_VER_RES_MAX / 2) ? 0xFF : 0x00;
                break;
            case PIC_UP_BLACK_DOWN_WHITE:
                temp_buf[pcnt] = (row > LCD_HOR_RES_MAX / 16)    ? 0xFF
                                 : (row == LCD_HOR_RES_MAX / 16) ? 0x0F
                                                                 : 0x00;
                break;
            default:
                temp_buf[pcnt] = 0x00;
                break;
            }
            pcnt++;
        }
    }

    LCD_WriteReg(hlcdc, REG_WRITE_NEW_DATA, temp_buf, PICTURE_LENGTH);
    rt_free(temp_buf);

    EPD_Refresh(hlcdc);
}
static void EPD_EnterDeepSleep(LCDC_HandleTypeDef *hlcdc)
{
    uint8_t parameter[5];
    parameter[0] = 0xA5;
    LCD_WriteReg(hlcdc, 0x07, parameter, 1);
}
static void EPD_TemperatureMeasure(LCDC_HandleTypeDef *hlcdc)
{
    uint8_t parameter[5];
    LCD_WriteReg(hlcdc, REG_PWR_ON_MEASURE, RT_NULL, 0);
    parameter[0] = 0xA5;
    LCD_WriteReg(hlcdc, REG_TEMP_SEL, parameter, 1);
    LCD_WriteReg(hlcdc, REG_TEMP_CALIB, RT_NULL, 0);
    EPD_ReadBusy();

    HAL_LCDC_ReadDatas(hlcdc, REG_TEMP_READ, 0, &Var_Temp, 1);
    // rt_kprintf("EPD internal temp: %d °C", (int8_t)Var_Temp);
}

static const LCD_DrvOpsDef epd_spi_drv = {
    .Init = LCD_Init,
    .ReadID = LCD_ReadID,
    .DisplayOn = LCD_DisplayOn,
    .DisplayOff = LCD_DisplayOff,
    .SetRegion = LCD_SetRegion,
    .WritePixel = LCD_WritePixel,
    .WriteMultiplePixels = LCD_WriteMultiplePixels,
    .ReadPixel = LCD_ReadPixel,
    .SetColorMode = LCD_SetColorMode,
    .SetBrightness = RT_NULL,
    .IdleModeOn = RT_NULL,
    .IdleModeOff = RT_NULL,
};

LCD_DRIVER_EXPORT(epd_spi, EPD_LCD_ID, &lcdc_int_cfg, &epd_spi_drv,
                  LCD_PIXEL_WIDTH, LCD_PIXEL_HEIGHT, 16);
