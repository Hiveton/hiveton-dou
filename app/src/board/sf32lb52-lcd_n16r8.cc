#include "board_hardware.h"
#include "rtthread.h"
#include "rtdevice.h"
#include "drv_io.h"
#include "bf0_hal.h"
#include "bf0_pm.h"

#define LCD_BACKLIGHT_PWM_DEV_NAME "pwm2"
#define LCD_BACKLIGHT_PWM_CHANNEL 4
#define LCD_BACKLIGHT_PWM_PERIOD_NS (50 * 1000U)

extern "C"
{

static struct rt_device_pwm *s_backlight_pwm = RT_NULL;
static rt_bool_t s_backlight_pwm_ready = RT_FALSE;
static int s_backlight_last_level = -1;

static rt_err_t board_backlight_pwm_init(void)
{
    rt_err_t result;

    HAL_PIN_Set(PAD_PA01, GPTIM1_CH4, PIN_NOPULL, 1);

    if (s_backlight_pwm == RT_NULL)
    {
        s_backlight_pwm = (struct rt_device_pwm *)rt_device_find(LCD_BACKLIGHT_PWM_DEV_NAME);
        if (s_backlight_pwm == RT_NULL)
        {
            rt_kprintf("backlight: pwm device %s not found\n", LCD_BACKLIGHT_PWM_DEV_NAME);
            return -RT_ENOSYS;
        }
    }

    if (s_backlight_pwm_ready)
    {
        return RT_EOK;
    }

    result = rt_device_open((rt_device_t)s_backlight_pwm, RT_DEVICE_OFLAG_RDWR);
    if ((result != RT_EOK) && (result != -RT_EBUSY))
    {
        rt_kprintf("backlight: pwm open failed=%d\n", result);
        return result;
    }

    s_backlight_pwm_ready = RT_TRUE;
    return RT_EOK;
}

void set_pinmux(void)
{
    HAL_PIN_Set(PAD_PA01, GPTIM1_CH4, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA00, GPIO_A0, PIN_NOPULL, 1);
    // AW32001 I2C2 pins (PA31: SCL, PA32: SDA)
    HAL_PIN_Set(PAD_PA31, I2C2_SCL, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA32, I2C2_SDA, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA42, GPIO_A42, PIN_NOPULL, 1);
    BSP_GPIO_Set(1, 1, 1);
    BSP_GPIO_Set(0, 1, 1);
}

void board_backlight_set(uint8_t enabled)
{
    board_backlight_set_level(enabled ? 100U : 0U);
}

void board_backlight_set_level(uint8_t brightness)
{
    rt_uint32_t pulse;

    if (brightness > 100U)
    {
        brightness = 100U;
    }

    if (s_backlight_last_level != (int)brightness)
    {
        rt_kprintf("bl_trace: board_backlight_set_level=%u\n", brightness);
        s_backlight_last_level = (int)brightness;
    }

    if (board_backlight_pwm_init() != RT_EOK)
    {
        HAL_PIN_Set(PAD_PA01, GPIO_A1, PIN_NOPULL, 1);
        BSP_GPIO_Set(1, brightness > 0U ? 1 : 0, 1);
        return;
    }

    pulse = (LCD_BACKLIGHT_PWM_PERIOD_NS * brightness) / 100U;
    rt_pwm_set(s_backlight_pwm,
               LCD_BACKLIGHT_PWM_CHANNEL,
               LCD_BACKLIGHT_PWM_PERIOD_NS,
               pulse);

    if (brightness == 0U)
    {
        rt_pwm_disable(s_backlight_pwm, LCD_BACKLIGHT_PWM_CHANNEL);
    }
    else
    {
        rt_pwm_enable(s_backlight_pwm, LCD_BACKLIGHT_PWM_CHANNEL);
    }
}

HAL_RAM_RET_CODE_SECT(PowerDownCustom, void PowerDownCustom(void))
{
    HAL_PMU_SelectWakeupPin(0, HAL_HPAON_QueryWakeupPin(hwp_gpio1, BSP_KEY1_PIN));
    HAL_PMU_EnablePinWakeup(0, AON_PIN_MODE_HIGH);
    hwp_pmuc->WKUP_CNT = 0x000F000F;

    HAL_PMU_ConfigPeriLdo(PMU_PERI_LDO2_3V3, false, false);
    HAL_PMU_ConfigPeriLdo(PMU_PERI_LDO_1V8, false, false);
    HAL_PMU_EnterHibernate();

    while (1)
    {
    }
}

void check_poweron_reason(void)
{
    if (SystemPowerOnModeGet() == PM_HIBERNATE_BOOT)
    {
        NVIC_EnableIRQ(RTC_IRQn);
    }
}

}
