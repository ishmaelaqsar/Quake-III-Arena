#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <functional>
#include <vector>
#include <variant>
#include <memory>
#include <optional>
#include <sstream>

namespace q3::scripting {

using ScriptValue = std::variant<std::monostate, double, std::string, bool>;

class IScriptEngine {
public:
    virtual ~IScriptEngine() = default;

    virtual bool execute(std::string_view script) = 0;
    virtual ScriptValue eval(std::string_view expression) = 0;

    virtual void set_variable(std::string_view name, const ScriptValue& val) = 0;
    virtual std::optional<ScriptValue> get_variable(std::string_view name) const = 0;

    using ScriptFunction = std::function<ScriptValue(const std::vector<ScriptValue>& args)>;
    virtual void register_function(std::string_view name, ScriptFunction fn) = 0;

    using EventHandler = std::function<void(const std::vector<ScriptValue>& args)>;
    virtual void subscribe_event(std::string_view event_name, EventHandler handler) = 0;
    virtual void dispatch_event(std::string_view event_name, const std::vector<ScriptValue>& args) = 0;
};

class ModernScriptEngine : public IScriptEngine {
public:
    ModernScriptEngine();

    bool execute(std::string_view script) override;
    ScriptValue eval(std::string_view expression) override;

    void set_variable(std::string_view name, const ScriptValue& val) override;
    std::optional<ScriptValue> get_variable(std::string_view name) const override;

    void register_function(std::string_view name, ScriptFunction fn) override;

    void subscribe_event(std::string_view event_name, EventHandler handler) override;
    void dispatch_event(std::string_view event_name, const std::vector<ScriptValue>& args) override;

private:
    std::vector<std::string> tokenize_line(std::string_view line);

    std::unordered_map<std::string, ScriptValue> variables_;
    std::unordered_map<std::string, ScriptFunction> functions_;
    std::unordered_map<std::string, std::vector<EventHandler>> event_handlers_;
};

} // namespace q3::scripting
