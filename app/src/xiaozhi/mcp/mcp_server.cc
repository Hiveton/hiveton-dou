/*
 * MCP Server Implementation
 * Reference: https://modelcontextprotocol.io/specification/2024-11-05
 */

#include "mcp_server.h"
#include <algorithm>
#include <cstring>
#include "../iot/thing_manager.h"
#include <webclient.h>
#include "audio_mem.h"
#include "rgbled_mcp.h" 
// #include "lwip/apps/websocket_client.h"   // 提供 wsock_write 和 OPCODE_TEXT 定义
#include "../xiaozhi_websocket.h"        // 提供 g_xz_ws 定义
#include "../xiaozhi_mqtt.h" 
extern xiaozhi_ws_t g_xz_ws; 
extern xiaozhi_context_t g_xz_context;     
extern "C" {
extern void xiaozhi_ui_update_volume(int volume);
extern void xiaozhi_ui_update_brightness(int brightness);
extern void my_mqtt_request_cb2(void *arg, err_t err);
extern uint8_t aec_is_enable(void);
extern void aec_set_enable(uint8_t enable);
extern uint8_t vad_is_enable(void);
extern void vad_set_enable(uint8_t enable);
}



#define TAG "MCP"
#define BOARD_NAME "XiaoZhi-SF32"
#define DEFAULT_TOOLCALL_STACK_SIZE 6144
#ifndef MCP_LOG_MESSAGE_CONTENT
#define MCP_LOG_MESSAGE_CONTENT 0
#endif
#define MCP_LOG_CONTENT_PREVIEW_BYTES 256
#define MCP_MAX_REPLY_RESULT_BYTES 8192
#define MCP_MAX_ERROR_MESSAGE_BYTES 256
#define MCP_MAX_OUTBOUND_MESSAGE_BYTES 16384

static std::string McpExtractStringField(const std::string& json, const char* field)
{
    std::string pattern = "\"";
    pattern += field;
    pattern += "\":\"";

    auto start = json.find(pattern);
    if (start == std::string::npos) {
        return "unknown";
    }

    start += pattern.length();
    auto end = json.find('"', start);
    if (end == std::string::npos || end == start) {
        return "unknown";
    }

    return json.substr(start, end - start);
}

static std::string McpTruncateString(const std::string& value, size_t max_len)
{
    if (value.length() <= max_len) {
        return value;
    }

    return value.substr(0, max_len);
}

static void McpLogContentPreview(const char* label, const std::string& text)
{
#if MCP_LOG_MESSAGE_CONTENT
    std::string preview = McpTruncateString(text, MCP_LOG_CONTENT_PREVIEW_BYTES);
    rt_kprintf("[MCP] %s preview(%u/%u): %s%s\n",
               label,
               (unsigned int)preview.length(),
               (unsigned int)text.length(),
               preview.c_str(),
               preview.length() < text.length() ? "..." : "");
#else
    (void)label;
    (void)text;
#endif
}

McpServer::McpServer() {
}

McpServer::~McpServer() {
    for (auto tool : tools_) { 
        delete tool;
    }
    tools_.clear();
}

void McpServer::AddCommonTools() {
    // To speed up the response time, we add the common tools to the beginning of
    // the tools list to utilize the prompt cache.
    // Backup the original tools list and restore it after adding the common tools.
   auto original_tools = std::move(tools_);
#if 1   
   auto speaker = iot::ThingManager::GetInstance().GetThing("Speaker");
    if (speaker) {
        //设置音量工具
        AddTool("self.audio_speaker.set_volume",
        "Set the volume of the audio speaker,Must not exceed 15.",
        PropertyList({
            Property("volume", kPropertyTypeInteger, 0, 15)
        }),
        [=](const PropertyList& properties) -> ReturnValue {
            int volume = properties["volume"].value<int>();
            auto json_str = R"({"method":"SetVolume","parameters":{"volume":)" + std::to_string(volume) + "}}";
            auto command = cJSON_Parse(json_str.c_str());
            if (command) {
                speaker->Invoke(command);
                cJSON_Delete(command); // 使用完记得释放内存
            }
            xiaozhi_ui_update_volume(volume);
            return true;
        });

         //获取当前音量工具
        AddTool("self.audio_speaker.get_volume",
        "Get the current volume of the audio speaker.",
        PropertyList(),
        [=](const PropertyList&) -> ReturnValue {
            const char* json_str = R"({"method":"GetVolume","parameters":{}})";
            cJSON* cmd = cJSON_Parse(json_str); // 直接使用 const char*
            speaker->Invoke(cmd);
            cJSON_Delete(cmd);
            return audio_server_get_private_volume(AUDIO_TYPE_LOCAL_MUSIC);
        });
    }
    
    auto screen = iot::ThingManager::GetInstance().GetThing("Screen");
    if (screen) {
        //设置屏幕亮度工具
        AddTool("self.screen.set_brightness",
        "Set the brightness of the screen.",
        PropertyList({
            Property("brightness", kPropertyTypeInteger, 0, 100)
        }),
        [=](const PropertyList& properties) -> ReturnValue {
            int brightness = properties["brightness"].value<int>();
            auto json_str = R"({"method":"SetBrightness","parameters":{"Brightness":)" + std::to_string(brightness) + "}}";
            auto command = cJSON_Parse(json_str.c_str());
            if (command) {
                screen->Invoke(command);
                cJSON_Delete(command);
            }
            xiaozhi_ui_update_brightness(brightness);
            return true;
        });

        //获取屏幕亮度工具
        AddTool("self.screen.get_bbrightness",
        "Get the current brightness of the screen.",
        PropertyList(),
        [=](const PropertyList&) -> ReturnValue {
            const char* json_str = R"({"method":"GetBrightness","parameters":{}})";
            cJSON* cmd = cJSON_Parse(json_str);
            screen->Invoke(cmd);
            cJSON_Delete(cmd);
            
            for (const auto& prop : screen->GetProperties()) {
                if (prop.name() == "Brightness" && prop.type() == iot::kValueTypeNumber) {
                    return prop.number();
                }
            }
            return 50; // 默认值
        });
    }
    
        // 添加RGB LED工具
        RGBLEDTool::RegisterRGBLEDTool(this);

    //添加唤醒工具
    AddTool("self.wakeup.enable",
        "Enable the wakeup function.",
        PropertyList(),
        [=](const PropertyList&) -> ReturnValue 
        {
            aec_set_enable(1);
            return true;
        });

    AddTool("self.wakeup.disable",
        "Disable the wakeup function.",
        PropertyList(),
        [=](const PropertyList&) -> ReturnValue 
        {
            aec_set_enable(0);
            return true;
        });

    AddTool("self.wakeup.get_status",
        "Get the current status of the wakeup function.",
        PropertyList(),
        [=](const PropertyList&) -> ReturnValue 
        {
            return (bool)aec_is_enable();
        });

    // 添加打断功能控制工具
    AddTool("self.interrupt.enable",
        "Enable the interrupt function.",
        PropertyList(),
        [=](const PropertyList&) -> ReturnValue 
        {
            vad_set_enable(0);
            return true;
        });

    AddTool("self.interrupt.disable",
        "Disable the interrupt function.",
        PropertyList(),
        [=](const PropertyList&) -> ReturnValue 
        {
            vad_set_enable(1);
            return true;
        });

    AddTool("self.interrupt.get_status",
        "Get the current status of the interrupt function.",
        PropertyList(),
        [=](const PropertyList&) -> ReturnValue 
        {
            // 注意：vad_enable为1表示不打断，为0表示可打断
            return (bool)(!vad_is_enable());
        });

#endif 
    // Restore the original tools list to the end of the tools list
    tools_.insert(tools_.end(), original_tools.begin(), original_tools.end());
}

void McpServer::SendText(const std::string& text)
{
#ifdef XIAOZHI_USING_MQTT
    const char* transport = "MQTT";
#else
    const char* transport = "WebSocket";
#endif
    std::string message_type = McpExtractStringField(text, "type");
    rt_kprintf("[MCP] Sending text via %s len=%u type=%s tools=%d\n",
               transport,
               (unsigned int)text.length(),
               message_type.c_str(),
               (int)tools_.size());
    McpLogContentPreview("Outgoing message", text);

    if (text.length() > MCP_MAX_OUTBOUND_MESSAGE_BYTES) {
        rt_kprintf("[MCP] Outgoing message too large, dropped len=%u limit=%u type=%s\n",
                   (unsigned int)text.length(),
                   (unsigned int)MCP_MAX_OUTBOUND_MESSAGE_BYTES,
                   message_type.c_str());
        return;
    }

#ifdef XIAOZHI_USING_MQTT
    //  MQTT 发送逻辑
    if (mqtt_client_is_connected(&g_xz_context.clnt)) {
        mqtt_publish(&g_xz_context.clnt, g_xz_context.publish_topic, text.c_str(), 
                     text.length(), 0, 0, NULL, NULL);
    } else {
        rt_kprintf("[MCP] MQTT client not connected\n");
    }
#else
    // WebSocket 发送逻辑
    wsock_write(&g_xz_ws.clnt, text.c_str(), text.length(), OPCODE_TEXT);
#endif
}
void print_long_string(const char* str, int max_len_per_line = 100) {
    if (str == nullptr) {
        return;
    }

    (void)max_len_per_line;
    McpLogContentPreview("Message content", std::string(str));
}
void McpServer::SendmcpMessage(const std::string& payload) {
    const char *session_id;
    const char prefix[] = "{\"session_id\":\"";
    const char middle[] = "\",\"type\":\"mcp\",\"payload\":";
    const char suffix[] = "}";
    size_t session_id_len;
    size_t total_len;
    uint32_t alloc_len;
    char *message;
    char *p;
#ifdef XIAOZHI_USING_MQTT
    session_id = reinterpret_cast<const char*>(g_xz_context.session_id);
    if (session_id == nullptr || session_id[0] == '\0') {
        session_id = "unknown-session";
    }
    rt_kprintf("[MCP] MQTT session_id len=%u\n", (unsigned int)strlen(session_id));
#else
    // WebSocket 逻辑
    if (g_xz_ws.session_id[0]) {
        session_id = reinterpret_cast<const char*>(g_xz_ws.session_id);
    } else {
        session_id = "unknown-session";
    }
    rt_kprintf("[MCP] WebSocket session_id len=%u\n", (unsigned int)strlen(session_id));
#endif

    session_id_len = strlen(session_id);
    total_len = (sizeof(prefix) - 1U) + session_id_len +
                (sizeof(middle) - 1U) + payload.length() +
                (sizeof(suffix) - 1U);
    if (total_len > MCP_MAX_OUTBOUND_MESSAGE_BYTES) {
        rt_kprintf("[MCP] MCP envelope too large, dropped payload_len=%u message_len=%u limit=%u\n",
                   (unsigned int)payload.length(),
                   (unsigned int)total_len,
                   (unsigned int)MCP_MAX_OUTBOUND_MESSAGE_BYTES);
        return;
    }

    alloc_len = (uint32_t)(total_len + 1U);
    if (alloc_len < 256U) {
        /* Force audio_mem_malloc() onto its PSRAM heap instead of the exhausted system heap. */
        alloc_len = 256U;
    }

    message = static_cast<char *>(audio_mem_malloc(alloc_len));
    if (message == nullptr) {
        rt_kprintf("[MCP] MCP envelope alloc failed message_len=%u\n",
                   (unsigned int)total_len);
        return;
    }

    p = message;
    memcpy(p, prefix, sizeof(prefix) - 1U);
    p += sizeof(prefix) - 1U;
    memcpy(p, session_id, session_id_len);
    p += session_id_len;
    memcpy(p, middle, sizeof(middle) - 1U);
    p += sizeof(middle) - 1U;
    memcpy(p, payload.data(), payload.length());
    p += payload.length();
    memcpy(p, suffix, sizeof(suffix) - 1U);
    p += sizeof(suffix) - 1U;
    *p = '\0';

    rt_kprintf("[MCP] MCP envelope payload_len=%u message_len=%u\n",
               (unsigned int)payload.length(),
               (unsigned int)total_len);
#if MCP_LOG_MESSAGE_CONTENT
    McpLogContentPreview("MCP envelope", std::string(message, total_len));
#endif

#ifdef XIAOZHI_USING_MQTT
    if (mqtt_client_is_connected(&g_xz_context.clnt)) {
        mqtt_publish(&g_xz_context.clnt, g_xz_context.publish_topic, message,
                     total_len, 0, 0, NULL, NULL);
    } else {
        rt_kprintf("[MCP] MQTT client not connected\n");
    }
#else
    wsock_write(&g_xz_ws.clnt, message, total_len, OPCODE_TEXT);
#endif
    audio_mem_free(message);
}

void McpServer::AddTool(McpTool* tool) {
    // Prevent adding duplicate tools
    if (std::find_if(tools_.begin(), tools_.end(), [tool](const McpTool* t) { return t->name() == tool->name(); }) != tools_.end()) {
        rt_kprintf(TAG, "Tool %s already added", tool->name().c_str());
        return;
    }

    rt_kprintf("[MCP] Add tool: %s\n", tool->name().c_str());
    tools_.push_back(tool);
}

void McpServer::AddTool(const std::string& name, const std::string& description, const PropertyList& properties, std::function<ReturnValue(const PropertyList&)> callback) {
    AddTool(new McpTool(name, description, properties, callback));
}

void McpServer::ParseMessage(const std::string& message) {
    cJSON* json = cJSON_Parse(message.c_str());
    if (json == nullptr) {
        rt_kprintf(TAG, "Failed to parse MCP message: %s", message.c_str());
        return;
    }
    ParseMessage(json);
    cJSON_Delete(json);
}

void McpServer::ParseCapabilities(const cJSON* capabilities) {
 
}

void McpServer::ParseMessage(const cJSON* json) 
{
    // Check JSONRPC version
    auto version = cJSON_GetObjectItem(json, "jsonrpc");
    if (version == nullptr || !cJSON_IsString(version) || strcmp(version->valuestring, "2.0") != 0) {
        const char* version_text = (cJSON_IsString(version) && version->valuestring) ? version->valuestring : "non-string/null";
        rt_kprintf("[MCP] Invalid JSONRPC version: %s\n", version_text);
        return;
    }
    
    // Check method
    auto method = cJSON_GetObjectItem(json, "method");
    if (method == nullptr || !cJSON_IsString(method)) {
        rt_kprintf(TAG, "Missing method");
        return;
    }
    
    auto method_str = std::string(method->valuestring);
    rt_kprintf(TAG, "Received method: %s", method_str.c_str());
    if (method_str.find("notifications") == 0) {
        return;
    }
    
    // Check params
    auto params = cJSON_GetObjectItem(json, "params");
    if (params != nullptr && !cJSON_IsObject(params)) {
        rt_kprintf(TAG, "Invalid params for method: %s", method_str.c_str());
        return;
    }

    auto id = cJSON_GetObjectItem(json, "id");
    if (id == nullptr || !cJSON_IsNumber(id)) {
        rt_kprintf(TAG, "Invalid id for method: %s", method_str.c_str());
        return;
    }
    auto id_int = id->valueint;
    
    if (method_str == "initialize") {
        
        
        //auto app_desc = esp_app_get_description();
        std::string message = "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"" BOARD_NAME "\",\"version\":\"";
        message += "1.0.0";
        message += "\"}}";
        ReplyResult(id_int, message);
    } else if (method_str == "tools/list") {
        std::string cursor_str = "";
        if (params != nullptr) {
            auto cursor = cJSON_GetObjectItem(params, "cursor");
            if (cJSON_IsString(cursor)) {
                cursor_str = std::string(cursor->valuestring);
            }
        }
        GetToolsList(id_int, cursor_str);
    } else if (method_str == "tools/call") {
        if (!cJSON_IsObject(params)) {
            rt_kprintf(TAG, "tools/call: Missing params");
            ReplyError(id_int, "Missing params");
            return;
        }
        auto tool_name = cJSON_GetObjectItem(params, "name");
        if (!cJSON_IsString(tool_name)) {
            rt_kprintf(TAG, "tools/call: Missing name");
            ReplyError(id_int, "Missing name");
            return;
        }
        auto tool_arguments = cJSON_GetObjectItem(params, "arguments");
        if (tool_arguments != nullptr && !cJSON_IsObject(tool_arguments)) {
            rt_kprintf(TAG, "tools/call: Invalid arguments");
            ReplyError(id_int, "Invalid arguments");
            return;
        }
        auto stack_size = cJSON_GetObjectItem(params, "stackSize");
        if (stack_size != nullptr && !cJSON_IsNumber(stack_size)) {
            rt_kprintf(TAG, "tools/call: Invalid stackSize");
            ReplyError(id_int, "Invalid stackSize");
            return;
        }
        DoToolCall(id_int, std::string(tool_name->valuestring), tool_arguments, stack_size ? stack_size->valueint : DEFAULT_TOOLCALL_STACK_SIZE);
    } else {
        rt_kprintf(TAG, "Method not implemented: %s", method_str.c_str());
        ReplyError(id_int, "Method not implemented: " + method_str);
    }
}

void McpServer::ReplyResult(int id, const std::string& result) {
    if (result.length() > MCP_MAX_REPLY_RESULT_BYTES) {
        rt_kprintf("[MCP] Reply result too large, returning error id=%d result_len=%u limit=%u\n",
                   id,
                   (unsigned int)result.length(),
                   (unsigned int)MCP_MAX_REPLY_RESULT_BYTES);
        ReplyError(id, "Result too large");
        return;
    }

    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id) + ",\"result\":";
    payload += result;
    payload += "}";
    McpServer::SendmcpMessage(payload);
    //wsock_write(&g_xz_ws.clnt, payload, strlen(payload), OPCODE_TEXT)
}
void McpServer::ReplyError(int id, const std::string& message) {
    std::string safe_message = McpTruncateString(message, MCP_MAX_ERROR_MESSAGE_BYTES);
    if (safe_message.length() < message.length()) {
        rt_kprintf("[MCP] Reply error message truncated id=%d message_len=%u limit=%u\n",
                   id,
                   (unsigned int)message.length(),
                   (unsigned int)MCP_MAX_ERROR_MESSAGE_BYTES);
    }

    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id);
    payload += ",\"error\":{\"message\":\"";
    payload += safe_message;
    payload += "\"}}";
    McpServer::SendmcpMessage(payload);
    //wsock_write(&g_xz_ws.clnt, payload, strlen(payload), OPCODE_TEXT)
}

void McpServer::GetToolsList(int id, const std::string& cursor) {
    const int max_payload_size = 8000;
    std::string json = "{\"tools\":[";
    
    bool found_cursor = cursor.empty();
    auto it = tools_.begin();
    std::string next_cursor = "";
    
    while (it != tools_.end()) {
        // 如果我们还没有找到起始位置，继续搜索
        if (!found_cursor) {
            if ((*it)->name() == cursor) {
                found_cursor = true;
            } else {
                ++it;
                continue;
            }
        }
        
        // 添加tool前检查大小
        std::string tool_json = (*it)->to_json() + ",";
        if (json.length() + tool_json.length() + 30 > max_payload_size) {
            // 如果添加这个tool会超出大小限制，设置next_cursor并退出循环
            next_cursor = (*it)->name();
            break;
        }
        
        json += tool_json;
        ++it;
    }
    
    if (json.back() == ',') {
        json.pop_back();
    }
    
    if (json.back() == '[' && !tools_.empty()) {
        // 如果没有添加任何tool，返回错误
        rt_kprintf(TAG, "tools/list: Failed to add tool %s because of payload size limit", next_cursor.c_str());
        ReplyError(id, "Failed to add tool " + next_cursor + " because of payload size limit");
        return;
    }

    if (next_cursor.empty()) {
        json += "]}";
    } else {
        json += "],\"nextCursor\":\"" + next_cursor + "\"}";
    }
    
    ReplyResult(id, json);
}

void McpServer::DoToolCall(int id, const std::string& tool_name, const cJSON* tool_arguments, int stack_size) {
    auto tool_iter = std::find_if(tools_.begin(), tools_.end(), 
                                 [&tool_name](const McpTool* tool) { 
                                     return tool->name() == tool_name; 
                                 });
    
    if (tool_iter == tools_.end()) {
        rt_kprintf(TAG, "tools/call: Unknown tool: %s", tool_name.c_str());
        ReplyError(id, "Unknown tool: " + tool_name);
        return;
    }

    PropertyList arguments = (*tool_iter)->properties();
    //try {
        for (auto& argument : arguments) {
            bool found = false;
            if (cJSON_IsObject(tool_arguments)) {
                auto value = cJSON_GetObjectItem(tool_arguments, argument.name().c_str());
                if (value != nullptr) {
                    if (argument.type() == kPropertyTypeBoolean) {
                        if (!cJSON_IsBool(value)) {
                            argument.SetError("Invalid argument type: " + argument.name());
                            ReplyResult(id, (*tool_iter)->Call(arguments));
                            return;
                        }
                        if (!argument.TrySetValue<bool>(value->valueint == 1)) {
                            ReplyResult(id, (*tool_iter)->Call(arguments));
                            return;
                        }
                        found = true;
                    } else if (argument.type() == kPropertyTypeInteger) {
                        if (!cJSON_IsNumber(value)) {
                            argument.SetError("Invalid argument type: " + argument.name());
                            ReplyResult(id, (*tool_iter)->Call(arguments));
                            return;
                        }
                        int value_int = value->valueint;
                        rt_kprintf("value_int: %d\n", value_int);
                        if (!argument.TrySetValue<int>(value_int)) {
                            ReplyResult(id, (*tool_iter)->Call(arguments));
                            return;
                        }
                        found = true;
                    } else if (argument.type() == kPropertyTypeString) {
                        if (!cJSON_IsString(value)) {
                            argument.SetError("Invalid argument type: " + argument.name());
                            ReplyResult(id, (*tool_iter)->Call(arguments));
                            return;
                        }
                        if (!argument.TrySetValue<std::string>(value->valuestring)) {
                            ReplyResult(id, (*tool_iter)->Call(arguments));
                            return;
                        }
                        found = true;
                    }
                }
            }

            if (!argument.has_default_value() && !found) {
                rt_kprintf(TAG, "tools/call: Missing valid argument: %s", argument.name().c_str());
                ReplyError(id, "Missing valid argument: " + argument.name());
                return;
            }
        }
    // } catch (const std::runtime_error& e) {
    //     rt_kprintf(TAG, "tools/call: %s", e.what());
    //     ReplyError(id, e.what());
    //     return;
    // }

    // Start a task to receive data with stack size
    // esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    // cfg.thread_name = "tool_call";
    // cfg.stack_size = stack_size;
    // cfg.prio = 1;
    // esp_pthread_set_cfg(&cfg);

    // Use a thread to call the tool to avoid blocking the main thread
    // tool_call_thread_ = std::thread([this, id, tool_iter, args = arguments]() {
    //     try {
           
    //     } catch (const std::runtime_error& e) {
    //         rt_kprintf(TAG, "tools/call: %s", e.what());
    //         ReplyError(id, e.what());
    //     }
    // });
    // tool_call_thread_.detach();
     ReplyResult(id, (*tool_iter)->Call(arguments));
}
