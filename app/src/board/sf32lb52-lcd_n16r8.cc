#include "board_hardware.h"
#include "rtthread.h"
#include "drv_io.h"
#include "bf0_hal.h"
#include "bf0_pm.h"

extern "C"
{

void set_pinmux(void)
{
    // AW32001 I2C2 pins (PA31: SCL, PA32: SDA)
    HAL_PIN_Set(PAD_PA31, I2C2_SCL, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA32, I2C2_SDA, PIN_PULLUP, 1);
    HAL_PIN_SetMode(PAD_PA34, 1, PIN_DIGITAL_IO_NORMAL);
    HAL_PIN_SetMode(PAD_PA43, 1, PIN_DIGITAL_IO_NORMAL);
    HAL_PIN_SetMode(PAD_PA44, 1, PIN_DIGITAL_IO_NORMAL);
    HAL_PIN_Set(PAD_PA34, GPIO_A34, PIN_PULLDOWN, 1);
    HAL_PIN_Set(PAD_PA43, GPIO_A43, PIN_PULLDOWN, 1);
    HAL_PIN_Set(PAD_PA44, GPIO_A44, PIN_PULLDOWN, 1);
    HAL_PIN_Set(PAD_PA42, GPIO_A42, PIN_NOPULL, 1);
}

void board_backlight_set(uint8_t enabled)
{
    (void)enabled;
}

void board_backlight_set_level(uint8_t brightness)
{
    (void)brightness;
}

void board_backlight_restore(void)
{
}

HAL_RAM_RET_CODE_SECT(PowerDownCustom, void PowerDownCustom(void))
{
    HAL_PMU_SelectWakeupPin(0, HAL_HPAON_QueryWakeupPin(hwp_gpio1, 34));
    HAL_PMU_EnablePinWakeup(0, AON_PIN_MODE_HIGH);
    HAL_PMU_SelectWakeupPin(1, HAL_HPAON_QueryWakeupPin(hwp_gpio1, 43));
    HAL_PMU_EnablePinWakeup(1, AON_PIN_MODE_HIGH);
    HAL_PMU_SelectWakeupPin(2, HAL_HPAON_QueryWakeupPin(hwp_gpio1, 44));
    HAL_PMU_EnablePinWakeup(2, AON_PIN_MODE_HIGH);
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
