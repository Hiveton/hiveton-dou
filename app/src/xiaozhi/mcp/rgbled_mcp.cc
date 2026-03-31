#include "rtthread.h"
#include "rgbled_mcp.h"
#include "mcp_server.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include "drivers/rt_drv_pwm.h"
#include "bf0_hal.h"

// LED控制占位符实现
// TODO: 根据实际硬件实现LED控制

RGBLEDController& GetRGBLEDController() {
    static RGBLEDController instance;
    return instance;
}

bool RGBLEDTool::is_color_cycling_ = false;

void RGBLEDTool::ColorCycleThreadEntry(void* param) 
{
    // LED控制 - 占位符实现
    (void)param;
}

bool RGBLEDTool::IsLightOn() {
    return is_color_cycling_;
}

void RGBLEDTool::RegisterRGBLEDTool(McpServer* server) {
    // 循环变色工具
    server->AddTool(
        "self.led.turn_on_the_light",
        "turn on the light.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            if (is_color_cycling_) return true;
            is_color_cycling_ = true;
            // LED控制 - 占位符实现
            return true;
        }
    );

    server->AddTool(
        "self.led.turn_off_the_light",
        "turn off the light.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            is_color_cycling_ = false;
            return true;
        }
    );

    server->AddTool(
        "self.led.get_light_status",
        "Get the current status of the LED (on or off).",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            return RGBLEDTool::IsLightOn();
        }
    );
}
