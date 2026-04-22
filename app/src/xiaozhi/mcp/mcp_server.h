#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include <rtthread.h>
#include <string>
#include <string.h>
#include <vector>
#include <map>
#include <functional>
#include <variant>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <cJSON.h>

// 添加类型别名
using ReturnValue = std::variant<bool, int, std::string>;

inline std::string& McpCallErrorMessage() {
    static std::string error;
    return error;
}

inline void McpClearCallError() {
    McpCallErrorMessage().clear();
}

inline void McpSetCallError(const std::string& error) {
    if (!error.empty()) {
        McpCallErrorMessage() = error;
    }
}

inline std::string McpTakeCallError() {
    std::string error = McpCallErrorMessage();
    McpClearCallError();
    return error;
}

enum PropertyType {
    kPropertyTypeBoolean,
    kPropertyTypeInteger,
    kPropertyTypeString
};

class Property {
private:
    std::string name_;
    PropertyType type_;
    std::variant<bool, int, std::string> value_;
    bool has_default_value_;
    std::optional<int> min_value_;  // 新增：整数最小值
    std::optional<int> max_value_;  // 新增：整数最大值

    static const char* ErrorPrefix() {
        return "__mcp_internal_error__:";
    }

    static bool StartsWith(const std::string& value, const char* prefix) {
        const std::string prefix_str(prefix);
        return value.compare(0, prefix_str.size(), prefix_str) == 0;
    }

    bool SetTypeError(const char* expected_type) {
        std::string message = "Property " + name_ + " does not accept " + expected_type;
        rt_kprintf("%s", message.c_str());
        SetError(message);
        return false;
    }

public:
    // Required field constructor
    Property(const std::string& name, PropertyType type)
        : name_(name), type_(type), has_default_value_(false) {}

    // Optional field constructor with default value
    template<typename T>
    Property(const std::string& name, PropertyType type, const T& default_value)
        : name_(name), type_(type), has_default_value_(true) {
        value_ = default_value;
    }

    Property(const std::string& name, PropertyType type, int min_value, int max_value)
        : name_(name), type_(type), has_default_value_(false), min_value_(min_value), max_value_(max_value) {
        if (type != kPropertyTypeInteger) {
            rt_kprintf("Error: Range limits only apply to integer properties");
            min_value_.reset();
            max_value_.reset();
        } else if (min_value > max_value) {
            rt_kprintf("Error: Invalid integer property range");
            min_value_ = max_value;
            max_value_ = min_value;
        }
    }

    Property(const std::string& name, PropertyType type, int default_value, int min_value, int max_value)
        : name_(name), type_(type), has_default_value_(true), min_value_(min_value), max_value_(max_value) {
        if (type != kPropertyTypeInteger) {
            rt_kprintf("Range limits only apply to integer properties");
            has_default_value_ = false;
            min_value_.reset();
            max_value_.reset();
            return;
        }
        if (min_value > max_value) {
            rt_kprintf("Invalid integer property range");
            min_value_ = max_value;
            max_value_ = min_value;
        }
        if (default_value < min_value_.value()) {
            rt_kprintf("Default value must be within the specified range");
            value_ = min_value_.value();
            return;
        }
        if (default_value > max_value_.value()) {
            rt_kprintf("Default value must be within the specified range");
            value_ = max_value_.value();
            return;
        }
        value_ = default_value;
    }
    

    inline const std::string& name() const { return name_; }
    inline PropertyType type() const { return type_; }
    inline bool has_default_value() const { return has_default_value_; }
    inline bool has_range() const { return min_value_.has_value() && max_value_.has_value(); }
    inline int min_value() const { return min_value_.value_or(0); }
    inline int max_value() const { return max_value_.value_or(0); }

    inline void SetError(const std::string& message) {
        value_ = std::string(ErrorPrefix()) + message;
    }

    inline bool has_error() const {
        if (!std::holds_alternative<std::string>(value_)) {
            return false;
        }
        const std::string& value = std::get<std::string>(value_);
        return StartsWith(value, ErrorPrefix());
    }

    inline std::string error_message() const {
        if (!has_error()) {
            return "";
        }
        const std::string& value = std::get<std::string>(value_);
        return value.substr(std::string(ErrorPrefix()).size());
    }

    template<typename T>
    inline T value() const {
        if (has_error()) {
            McpSetCallError(error_message());
            return T();
        }
        if (const auto* typed_value = std::get_if<T>(&value_)) {
            return *typed_value;
        }
        std::string message = "Property " + name_ + " has invalid value type";
        rt_kprintf("%s", message.c_str());
        McpSetCallError(message);
        return T();
    }

    template<typename T>
    inline bool TrySetValue(const T& value) {
        if constexpr (std::is_same_v<T, bool>) {
            if (type_ != kPropertyTypeBoolean) {
                return SetTypeError("boolean");
            }
        } else if constexpr (std::is_same_v<T, int>) {
            if (type_ != kPropertyTypeInteger) {
                return SetTypeError("integer");
            }
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (type_ != kPropertyTypeString) {
                return SetTypeError("string");
            }
        }
        if constexpr (std::is_same_v<T, int>) {
            if (min_value_.has_value() && value < min_value_.value()) {
                std::string message = "Property " + name_ + " is below minimum allowed: " + std::to_string(min_value_.value());
                rt_kprintf("%s", message.c_str());
                SetError(message);
                return false;
            }
            if (max_value_.has_value() && value > max_value_.value()) {
                std::string message = "Property " + name_ + " exceeds maximum allowed: " + std::to_string(max_value_.value());
                rt_kprintf("%s", message.c_str());
                SetError(message);
                return false;
            }
        }
        value_ = value;
        return true;
    }

    template<typename T>
    inline void set_value(const T& value) {
        (void)TrySetValue<T>(value);
    }

    std::string to_json() const {
        cJSON *json = cJSON_CreateObject();
        
        if (type_ == kPropertyTypeBoolean) {
            cJSON_AddStringToObject(json, "type", "boolean");
            if (has_default_value_) {
                cJSON_AddBoolToObject(json, "default", value<bool>());
            }
        } else if (type_ == kPropertyTypeInteger) {
            cJSON_AddStringToObject(json, "type", "integer");
            if (has_default_value_) {
                cJSON_AddNumberToObject(json, "default", value<int>());
            }
            if (min_value_.has_value()) {
                cJSON_AddNumberToObject(json, "minimum", min_value_.value());
            }
            if (max_value_.has_value()) {
                cJSON_AddNumberToObject(json, "maximum", max_value_.value());
            }
        } else if (type_ == kPropertyTypeString) {
            cJSON_AddStringToObject(json, "type", "string");
            if (has_default_value_) {
                cJSON_AddStringToObject(json, "default", value<std::string>().c_str());
            }
        }
        
        char *json_str = cJSON_PrintUnformatted(json);
        std::string result(json_str);
        cJSON_free(json_str);
        cJSON_Delete(json);
        
        return result;
    }
};

class PropertyList {
private:
    std::vector<Property> properties_;

public:
    PropertyList() = default;
    PropertyList(const std::vector<Property>& properties) : properties_(properties) {}
    void AddProperty(const Property& property) {
        properties_.push_back(property);
    }

    const Property& operator[](const std::string& name) const {
        for (const auto& property : properties_) {
            if (property.name() == name) {
                return property;
            }
        }
        std::string message = "Property not found: " + name;
        rt_kprintf("%s", message.c_str());
        McpSetCallError(message);
        static Property empty("__mcp_error__", kPropertyTypeString);
        empty.SetError(message);
        return empty;
    }

    auto begin() { return properties_.begin(); }
    auto end() { return properties_.end(); }

    std::string GetErrorMessage() const {
        for (const auto& property : properties_) {
            if (property.has_error()) {
                return property.error_message();
            }
        }
        return "";
    }

    std::vector<std::string> GetRequired() const {
        std::vector<std::string> required;
        for (auto& property : properties_) {
            if (!property.has_default_value()) {
                required.push_back(property.name());
            }
        }
        return required;
    }

    std::string to_json() const {
        cJSON *json = cJSON_CreateObject();
        
        for (const auto& property : properties_) {
            cJSON *prop_json = cJSON_Parse(property.to_json().c_str());
            cJSON_AddItemToObject(json, property.name().c_str(), prop_json);
        }
        
        char *json_str = cJSON_PrintUnformatted(json);
        std::string result(json_str);
        cJSON_free(json_str);
        cJSON_Delete(json);
        
        return result;
    }
};

class McpTool {
private:
    std::string name_;
    std::string description_;
    PropertyList properties_;
    std::function<ReturnValue(const PropertyList&)> callback_;

    static std::string ReturnValueToText(const ReturnValue& return_value) {
        if (std::holds_alternative<std::string>(return_value)) {
            return std::get<std::string>(return_value);
        } else if (std::holds_alternative<bool>(return_value)) {
            return std::get<bool>(return_value) ? "true" : "false";
        } else if (std::holds_alternative<int>(return_value)) {
            return std::to_string(std::get<int>(return_value));
        }
        return "";
    }

    static std::string MakeCallResult(const std::string& text_value, bool is_error) {
        cJSON* result = cJSON_CreateObject();
        cJSON* content = cJSON_CreateArray();
        cJSON* text = cJSON_CreateObject();
        cJSON_AddStringToObject(text, "type", "text");
        cJSON_AddStringToObject(text, "text", text_value.c_str());
        cJSON_AddItemToArray(content, text);
        cJSON_AddItemToObject(result, "content", content);
        cJSON_AddBoolToObject(result, "isError", is_error);

        auto json_str = cJSON_PrintUnformatted(result);
        std::string result_str(json_str);
        cJSON_free(json_str);
        cJSON_Delete(result);
        return result_str;
    }

public:
    McpTool(const std::string& name, 
            const std::string& description, 
            const PropertyList& properties, 
            std::function<ReturnValue(const PropertyList&)> callback)
        : name_(name), 
        description_(description), 
        properties_(properties), 
        callback_(callback) {}

    inline const std::string& name() const { return name_; }
    inline const std::string& description() const { return description_; }
    inline const PropertyList& properties() const { return properties_; }

    std::string to_json() const {
        std::vector<std::string> required = properties_.GetRequired();
        
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "name", name_.c_str());
        cJSON_AddStringToObject(json, "description", description_.c_str());
        
        cJSON *input_schema = cJSON_CreateObject();
        cJSON_AddStringToObject(input_schema, "type", "object");
        
        cJSON *properties = cJSON_Parse(properties_.to_json().c_str());
        cJSON_AddItemToObject(input_schema, "properties", properties);
        
        if (!required.empty()) {
            cJSON *required_array = cJSON_CreateArray();
            for (const auto& property : required) {
                cJSON_AddItemToArray(required_array, cJSON_CreateString(property.c_str()));
            }
            cJSON_AddItemToObject(input_schema, "required", required_array);
        }
        
        cJSON_AddItemToObject(json, "inputSchema", input_schema);
        
        char *json_str = cJSON_PrintUnformatted(json);
        std::string result(json_str);
        cJSON_free(json_str);
        cJSON_Delete(json);
        
        return result;
    }

    std::string Call(const PropertyList& properties) {
        std::string validation_error = properties.GetErrorMessage();
        if (!validation_error.empty()) {
            return MakeCallResult(validation_error, true);
        }

        McpClearCallError();
        ReturnValue return_value = callback_(properties);
        std::string call_error = McpTakeCallError();
        if (!call_error.empty()) {
            return MakeCallResult(call_error, true);
        }
        return MakeCallResult(ReturnValueToText(return_value), false);
    }
};

class McpServer {
public:
    static McpServer& GetInstance() {
        static McpServer instance;
        return instance;
    }

    void AddCommonTools();
    void AddTool(McpTool* tool);
    void AddTool(const std::string& name, const std::string& description, const PropertyList& properties, std::function<ReturnValue(const PropertyList&)> callback);
    void ParseMessage(const cJSON* json);
    void ParseMessage(const std::string& message);
    void SendText(const std::string& text);
    void SendmcpMessage(const std::string& payload);

private:
    McpServer();
    ~McpServer();

    void ParseCapabilities(const cJSON* capabilities);

    void ReplyResult(int id, const std::string& result);
    void ReplyError(int id, const std::string& message);

    void GetToolsList(int id, const std::string& cursor);
    void DoToolCall(int id, const std::string& tool_name, const cJSON* tool_arguments, int stack_size);

    std::vector<McpTool*> tools_;
    // TODO: not used for now 
    // std::thread tool_call_thread_;
};


#endif // MCP_SERVER_H
