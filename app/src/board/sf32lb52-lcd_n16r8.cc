#include "board_hardware.h"
#include "rtthread.h"
#include "drv_io.h"
#include "bf0_hal.h"
#include "bf0_pm.h"

extern "C"
{

void set_pinmux(void)
{
    HAL_PIN_Set(PAD_PA01, GPIO_A1, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA00, GPIO_A0, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA10, GPIO_A10, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA07, I2C1_SCL, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA08, I2C1_SDA, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA42, GPIO_A42, PIN_NOPULL, 1);
    BSP_GPIO_Set(10, 1, 1);
    BSP_GPIO_Set(1, 1, 1);
    BSP_GPIO_Set(0, 1, 1);
}

void board_backlight_set(uint8_t enabled)
{
    HAL_PIN_Set(PAD_PA01, GPIO_A1, PIN_NOPULL, 1);
    BSP_GPIO_Set(1, enabled ? 1 : 0, 1);
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
