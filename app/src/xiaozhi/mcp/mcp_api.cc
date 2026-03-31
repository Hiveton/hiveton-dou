#include "mcp_api.h"
#include "mcp_server.h"
#include "cJSON.h"
#include <rtthread.h>
#include <string.h>
#include "rgbled_mcp.h"

extern "C" 
{
    //注册IoT设备(可多个设备)
    void McpServer_ParseMessage(const char* message)
    {
        McpServer::GetInstance().ParseMessage(message);                
    }

    void MCP_RGBLED_CLOSE()
    {
        // LED控制 - 占位符实现
        // TODO: 根据实际硬件实现
    }

} // extern "C"
