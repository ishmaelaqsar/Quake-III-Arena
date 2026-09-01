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
#include <cmath>

#ifndef SOL_HAS_STD_OPTIONAL
#define SOL_HAS_STD_OPTIONAL 1
#endif
#include "sol/sol.hpp"

namespace q3::scripting {

using ScriptValue = std::variant<std::monostate, double, std::string, bool>;

struct ScheduledTask {
    double trigger_time{0.0};
    std::function<void()> callback;
};

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

    virtual void schedule(double delay_seconds, std::function<void()> callback) = 0;
    virtual void update_timers(double current_time_seconds) = 0;

    virtual void set_entity_property(int entity_id, std::string_view key, const ScriptValue& val) = 0;
    virtual std::optional<ScriptValue> get_entity_property(int entity_id, std::string_view key) const = 0;
};

// Sol2 C++17 Sol2 Lua / LuaJIT embedded scripting engine
class ScriptEngine : public IScriptEngine {
public:
    ScriptEngine();

    bool execute(std::string_view script) override;
    ScriptValue eval(std::string_view expression) override;

    void set_variable(std::string_view name, const ScriptValue& val) override;
    std::optional<ScriptValue> get_variable(std::string_view name) const override;

    void register_function(std::string_view name, ScriptFunction fn) override;

    void subscribe_event(std::string_view event_name, EventHandler handler) override;
    void dispatch_event(std::string_view event_name, const std::vector<ScriptValue>& args) override;

    void schedule(double delay_seconds, std::function<void()> callback) override;
    void update_timers(double current_time_seconds) override;

    void set_entity_property(int entity_id, std::string_view key, const ScriptValue& val) override;
    std::optional<ScriptValue> get_entity_property(int entity_id, std::string_view key) const override;

    sol::state& lua_state() noexcept { return lua_; }
    bool is_lua_enabled() const noexcept { return lua_enabled_; }

private:
    std::vector<std::string> tokenize_line(std::string_view line);

    sol::state lua_;
    double current_time_{0.0};
    bool lua_enabled_{true};
    std::unordered_map<std::string, ScriptValue> variables_;
    std::unordered_map<std::string, ScriptFunction> functions_;
    std::unordered_map<std::string, std::vector<EventHandler>> event_handlers_;
    std::vector<ScheduledTask> tasks_;
    std::unordered_map<int, std::unordered_map<std::string, ScriptValue>> entity_properties_;
};

} // namespace q3::scripting
