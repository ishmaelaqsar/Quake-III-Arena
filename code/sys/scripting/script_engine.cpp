#include "script_engine.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace q3::scripting {

ScriptEngine::ScriptEngine() {
    lua_enabled_ = true;

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

std::vector<std::string> ScriptEngine::tokenize_line(std::string_view line) {
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

bool ScriptEngine::execute(std::string_view script) {
    std::istringstream stream((std::string(script)));
    std::string line;

    while (std::getline(stream, line)) {
        // Strip comments
        auto comment_pos = line.find("//");
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }

        auto tokens = tokenize_line(line);
        if (tokens.empty()) continue;

        // Command: let var_name = value  OR  set var_name value
        if ((tokens[0] == "set" || tokens[0] == "let") && tokens.size() >= 3) {
            std::size_t name_idx = 1;
            std::size_t val_idx = 2;
            if (tokens[0] == "let" && tokens.size() >= 4 && tokens[2] == "=") {
                val_idx = 3;
            }
            std::string var_name = tokens[name_idx];
            std::string val_str = tokens[val_idx];

            ScriptValue val;
            try {
                double d = std::stod(val_str);
                val = d;
            } catch (...) {
                if (val_str == "true") val = true;
                else if (val_str == "false") val = false;
                else val = val_str;
            }
            set_variable(var_name, val);
        }
        // Event emit: emit event_name arg
        else if (tokens[0] == "emit" && tokens.size() >= 2) {
            std::string event_name = tokens[1];
            std::vector<ScriptValue> args;
            for (std::size_t i = 2; i < tokens.size(); ++i) {
                try {
                    double d = std::stod(tokens[i]);
                    args.push_back(d);
                } catch (...) {
                    if (tokens[i] == "true") args.push_back(true);
                    else if (tokens[i] == "false") args.push_back(false);
                    else args.push_back(tokens[i]);
                }
            }
            dispatch_event(event_name, args);
        }
        // Function call: func_name arg1 arg2
        else if (functions_.find(tokens[0]) != functions_.end()) {
            std::vector<ScriptValue> args;
            for (std::size_t i = 1; i < tokens.size(); ++i) {
                try {
                    double d = std::stod(tokens[i]);
                    args.push_back(d);
                } catch (...) {
                    if (tokens[i] == "true") args.push_back(true);
                    else if (tokens[i] == "false") args.push_back(false);
                    else args.push_back(tokens[i]);
                }
            }
            functions_[tokens[0]](args);
        }
    }
    return true;
}

ScriptValue ScriptEngine::eval(std::string_view expression) {
    auto var_opt = get_variable(expression);
    if (var_opt.has_value()) {
        return *var_opt;
    }

    try {
        double d = std::stod(std::string(expression));
        return ScriptValue(d);
    } catch (...) {
        std::string expr(expression);
        if (expr == "true") return ScriptValue(true);
        if (expr == "false") return ScriptValue(false);
        return ScriptValue(expr);
    }
}

void ScriptEngine::set_variable(std::string_view name, const ScriptValue& val) {
    variables_[std::string(name)] = val;
}

std::optional<ScriptValue> ScriptEngine::get_variable(std::string_view name) const {
    auto it = variables_.find(std::string(name));
    if (it != variables_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void ScriptEngine::register_function(std::string_view name, ScriptFunction fn) {
    functions_[std::string(name)] = fn;
}

void ScriptEngine::subscribe_event(std::string_view event_name, EventHandler handler) {
    event_handlers_[std::string(event_name)].push_back(handler);
}

void ScriptEngine::dispatch_event(std::string_view event_name, const std::vector<ScriptValue>& args) {
    auto it = event_handlers_.find(std::string(event_name));
    if (it != event_handlers_.end()) {
        for (const auto& handler : it->second) {
            handler(args);
        }
    }
}

void ScriptEngine::schedule(double delay_seconds, std::function<void()> callback) {
    tasks_.push_back({current_time_ + delay_seconds, callback});
}

void ScriptEngine::update_timers(double delta_time_seconds) {
    current_time_ += delta_time_seconds;

    auto it = tasks_.begin();
    while (it != tasks_.end()) {
        if (current_time_ >= it->trigger_time) {
            if (it->callback) {
                it->callback();
            }
            it = tasks_.erase(it);
        } else {
            ++it;
        }
    }
}

void ScriptEngine::set_entity_property(int entity_id, std::string_view key, const ScriptValue& val) {
    entity_properties_[entity_id][std::string(key)] = val;
}

std::optional<ScriptValue> ScriptEngine::get_entity_property(int entity_id, std::string_view key) const {
    auto ent_it = entity_properties_.find(entity_id);
    if (ent_it != entity_properties_.end()) {
        auto prop_it = ent_it->second.find(std::string(key));
        if (prop_it != ent_it->second.end()) {
            return prop_it->second;
        }
    }
    return std::nullopt;
}

} // namespace q3::scripting
