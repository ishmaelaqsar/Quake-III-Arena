#include "cvar_manager.hpp"

namespace q3::cvar {

void CvarRef::set(std::string_view val) {
    if (!raw_) return;
    std::string s(val);
    std::string old_val(raw_->string);
    Cvar_Set(raw_->name, s.c_str());
    CvarManager::instance().notify_change(raw_->name, old_val, s);
}

void CvarRef::set(int val) {
    set(std::to_string(val));
}

void CvarRef::set(float val) {
    set(std::to_string(val));
}

void CvarRef::set(bool val) {
    set(std::string_view(val ? "1" : "0"));
}

void CvarRef::reset() {
    if (raw_ && raw_->resetString) {
        set(std::string_view(raw_->resetString));
    }
}

CvarRef CvarManager::declare(std::string_view name, std::string_view default_val, int flags) {
    std::string s_name(name);
    std::string s_val(default_val);
    cvar_t* cv = Cvar_Get(s_name.c_str(), s_val.c_str(), flags);
    return CvarRef(cv);
}

CvarRef CvarManager::declare(std::string_view name, int default_val, int flags) {
    return declare(name, std::string_view(std::to_string(default_val)), flags);
}

CvarRef CvarManager::declare(std::string_view name, float default_val, int flags) {
    return declare(name, std::string_view(std::to_string(default_val)), flags);
}

CvarRef CvarManager::declare(std::string_view name, bool default_val, int flags) {
    return declare(name, std::string_view(default_val ? "1" : "0"), flags);
}

std::optional<CvarRef> CvarManager::find(std::string_view name) {
    std::string s_name(name);
    // Cvar_FindVar is static in cvar.c, but Cvar_VariableString returns empty if not found or we can check via Cvar_Get / Cvar_Find
    const char* str = Cvar_VariableString(s_name.c_str());
    if (str && str[0] != '\0') {
        // Cvar_Get with same string will retrieve existing pointer without changing it
        cvar_t* cv = Cvar_Get(s_name.c_str(), str, 0);
        return CvarRef(cv);
    }
    return std::nullopt;
}

void CvarManager::add_change_listener(std::string_view name, ChangeCallback cb) {
    listeners_[std::string(name)].push_back(std::move(cb));
}

void CvarManager::notify_change(std::string_view name, std::string_view old_val, std::string_view new_val) {
    auto it = listeners_.find(std::string(name));
    if (it != listeners_.end()) {
        for (const auto& cb : it->second) {
            cb(name, old_val, new_val);
        }
    }
}

void CvarManager::reset_all() {
    Cvar_Restart_f();
}

} // namespace q3::cvar
