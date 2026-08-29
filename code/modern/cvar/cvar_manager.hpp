#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

extern "C" {
#include "q_shared.h"
#include "qcommon.h"
}

namespace q3::cvar {

enum class Flag : int {
    Archive      = CVAR_ARCHIVE,
    UserInfo     = CVAR_USERINFO,
    ServerInfo   = CVAR_SERVERINFO,
    SystemInfo   = CVAR_SYSTEMINFO,
    Init         = CVAR_INIT,
    Latch        = CVAR_LATCH,
    Rom          = CVAR_ROM,
    UserCreated  = CVAR_USER_CREATED,
    Temp         = CVAR_TEMP,
    Cheat        = CVAR_CHEAT,
    NoRestart    = CVAR_NORESTART
};

inline int operator|(Flag a, Flag b) noexcept {
    return static_cast<int>(a) | static_cast<int>(b);
}

class CvarRef {
public:
    explicit CvarRef(cvar_t* raw = nullptr) noexcept : raw_(raw) {}

    bool valid() const noexcept { return raw_ != nullptr; }
    
    std::string_view name() const noexcept {
        return raw_ ? std::string_view(raw_->name) : std::string_view{};
    }

    std::string_view string_value() const noexcept {
        return raw_ ? std::string_view(raw_->string) : std::string_view{};
    }

    int int_value() const noexcept {
        return raw_ ? raw_->integer : 0;
    }

    float float_value() const noexcept {
        return raw_ ? raw_->value : 0.0f;
    }

    bool bool_value() const noexcept {
        return int_value() != 0;
    }

    void set(std::string_view val);
    void set(const char* val) { set(std::string_view(val ? val : "")); }
    void set(int val);
    void set(float val);
    void set(bool val);

    void reset();

    cvar_t* raw() const noexcept { return raw_; }

private:
    cvar_t* raw_{nullptr};
};

class CvarManager {
public:
    using ChangeCallback = std::function<void(std::string_view cvar_name, std::string_view old_val, std::string_view new_val)>;

    static CvarManager& instance() noexcept {
        static CvarManager mgr;
        return mgr;
    }

    CvarRef declare(std::string_view name, std::string_view default_val, int flags = 0);
    CvarRef declare(std::string_view name, const char* default_val, int flags = 0) {
        return declare(name, std::string_view(default_val ? default_val : ""), flags);
    }
    CvarRef declare(std::string_view name, int default_val, int flags = 0);
    CvarRef declare(std::string_view name, float default_val, int flags = 0);
    CvarRef declare(std::string_view name, bool default_val, int flags = 0);

    std::optional<CvarRef> find(std::string_view name);

    void add_change_listener(std::string_view name, ChangeCallback cb);
    void notify_change(std::string_view name, std::string_view old_val, std::string_view new_val);

    void reset_all();

private:
    CvarManager() = default;
    std::unordered_map<std::string, std::vector<ChangeCallback>> listeners_;
};

} // namespace q3::cvar
