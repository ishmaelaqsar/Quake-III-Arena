#include "script_engine.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace q3::scripting {

ModernScriptEngine::ModernScriptEngine() {
    // Built-in standard script functions
    register_function("print", [this](const std::vector<ScriptValue>& args) -> ScriptValue {
        std::ostringstream ss;
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i > 0) ss << " ";
            std::visit([&ss](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    ss << "null";
                } else if constexpr (std::is_same_v<T, bool>) {
                    ss << (arg ? "true" : "false");
                } else {
                    ss << arg;
                }
            }, args[i]);
        }
        return ScriptValue(ss.str());
    });

    register_function("add", [](const std::vector<ScriptValue>& args) -> ScriptValue {
        double sum = 0.0;
        for (const auto& a : args) {
            if (std::holds_alternative<double>(a)) {
                sum += std::get<double>(a);
            }
        }
        return ScriptValue(sum);
    });

    register_function("multiply", [](const std::vector<ScriptValue>& args) -> ScriptValue {
        if (args.empty()) return ScriptValue(0.0);
        double prod = 1.0;
        for (const auto& a : args) {
            if (std::holds_alternative<double>(a)) {
                prod *= std::get<double>(a);
            }
        }
        return ScriptValue(prod);
    });

    register_function("sqrt", [](const std::vector<ScriptValue>& args) -> ScriptValue {
        if (!args.empty() && std::holds_alternative<double>(args[0])) {
            return ScriptValue(std::sqrt(std::get<double>(args[0])));
        }
        return ScriptValue(0.0);
    });

    register_function("sin", [](const std::vector<ScriptValue>& args) -> ScriptValue {
        if (!args.empty() && std::holds_alternative<double>(args[0])) {
            return ScriptValue(std::sin(std::get<double>(args[0])));
        }
        return ScriptValue(0.0);
    });

    register_function("cos", [](const std::vector<ScriptValue>& args) -> ScriptValue {
        if (!args.empty() && std::holds_alternative<double>(args[0])) {
            return ScriptValue(std::cos(std::get<double>(args[0])));
        }
        return ScriptValue(0.0);
    });
}

std::vector<std::string> ModernScriptEngine::tokenize_line(std::string_view line) {
    std::vector<std::string> tokens;
    std::string token;
    bool in_quotes = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (std::isspace(static_cast<unsigned char>(c)) && !in_quotes) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else {
            token.push_back(c);
        }
    }
    if (!token.empty()) {
        tokens.push_back(token);
    }
    return tokens;
}

bool ModernScriptEngine::execute(std::string_view script) {
    std::istringstream stream((std::string(script)));
    std::string line;

    while (std::getline(stream, line)) {
        // Strip comments and trim
        auto comment_pos = line.find("//");
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }

        auto tokens = tokenize_line(line);
        if (tokens.empty()) continue;

        const std::string& cmd = tokens[0];

        // Variable assignment: var name = value / set name value
        if (cmd == "var" || cmd == "let" || cmd == "set") {
            if (tokens.size() >= 3) {
                std::string var_name = tokens[1];
                std::string var_expr;
                std::size_t val_start = (tokens[2] == "=") ? 3 : 2;
                for (std::size_t i = val_start; i < tokens.size(); ++i) {
                    if (i > val_start) var_expr += " ";
                    var_expr += tokens[i];
                }
                set_variable(var_name, eval(var_expr));
            }
        }
        // Function invocation
        else if (auto it = functions_.find(cmd); it != functions_.end()) {
            std::vector<ScriptValue> args;
            for (std::size_t i = 1; i < tokens.size(); ++i) {
                args.push_back(eval(tokens[i]));
            }
            it->second(args);
        }
        // Event trigger: emit event_name [args...]
        else if (cmd == "emit" || cmd == "trigger") {
            if (tokens.size() >= 2) {
                std::string ev = tokens[1];
                std::vector<ScriptValue> args;
                for (std::size_t i = 2; i < tokens.size(); ++i) {
                    args.push_back(eval(tokens[i]));
                }
                dispatch_event(ev, args);
            }
        }
    }
    return true;
}

ScriptValue ModernScriptEngine::eval(std::string_view expression) {
    if (expression.empty()) return ScriptValue{};

    // Check if it's a number
    try {
        std::size_t idx = 0;
        std::string s(expression);
        double val = std::stod(s, &idx);
        if (idx == s.size()) {
            return ScriptValue(val);
        }
    } catch (...) {}

    // Check boolean
    if (expression == "true") return ScriptValue(true);
    if (expression == "false") return ScriptValue(false);

    // Check variable
    if (auto var = get_variable(expression)) {
        return *var;
    }

    // Default to string value
    return ScriptValue(std::string(expression));
}

void ModernScriptEngine::set_variable(std::string_view name, const ScriptValue& val) {
    variables_[std::string(name)] = val;
}

std::optional<ScriptValue> ModernScriptEngine::get_variable(std::string_view name) const {
    auto it = variables_.find(std::string(name));
    if (it != variables_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void ModernScriptEngine::register_function(std::string_view name, ScriptFunction fn) {
    functions_[std::string(name)] = std::move(fn);
}

void ModernScriptEngine::subscribe_event(std::string_view event_name, EventHandler handler) {
    event_handlers_[std::string(event_name)].push_back(std::move(handler));
}

void ModernScriptEngine::dispatch_event(std::string_view event_name, const std::vector<ScriptValue>& args) {
    auto it = event_handlers_.find(std::string(event_name));
    if (it != event_handlers_.end()) {
        for (const auto& handler : it->second) {
            handler(args);
        }
    }
}

void ModernScriptEngine::schedule(double delay_seconds, std::function<void()> callback) {
    tasks_.push_back({current_time_ + delay_seconds, std::move(callback)});
}

void ModernScriptEngine::update_timers(double current_time_seconds) {
    current_time_ = current_time_seconds;
    auto it = tasks_.begin();
    while (it != tasks_.end()) {
        if (current_time_ >= it->trigger_time) {
            auto cb = std::move(it->callback);
            it = tasks_.erase(it);
            if (cb) cb();
        } else {
            ++it;
        }
    }
}

void ModernScriptEngine::set_entity_property(int entity_id, std::string_view key, const ScriptValue& val) {
    entity_properties_[entity_id][std::string(key)] = val;
}

std::optional<ScriptValue> ModernScriptEngine::get_entity_property(int entity_id, std::string_view key) const {
    auto e_it = entity_properties_.find(entity_id);
    if (e_it != entity_properties_.end()) {
        auto p_it = e_it->second.find(std::string(key));
        if (p_it != e_it->second.end()) {
            return p_it->second;
        }
    }
    return std::nullopt;
}

} // namespace q3::scripting
