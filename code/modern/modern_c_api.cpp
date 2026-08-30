#include "modern_c_api.h"
#include "cvar/cvar_manager.hpp"
#include "fs/vfs.hpp"
#include "multiplayer/session.hpp"
#include "scripting/script_engine.hpp"

static q3::scripting::ModernScriptEngine* g_scriptEngine = nullptr;

extern "C" {

void Modern_Init(void) {
    q3::multiplayer::SessionManager::instance().reset();
    
    if (!g_scriptEngine) {
        g_scriptEngine = new q3::scripting::ModernScriptEngine();
    }
    
    // Mount baseq3 using modern VFS
    q3::fs::VirtualFileSystem::instance().mount_search_path("baseq3");
}

void Modern_Frame(int msec) {
    if (g_scriptEngine) {
        // Convert msec to seconds
        g_scriptEngine->update_timers(msec / 1000.0);
    }
}

void Modern_ScriptExecute(const char* script) {
    if (g_scriptEngine && script) {
        g_scriptEngine->execute(script);
    }
}

void Modern_ScriptEvent(const char* event_name, const char* arg) {
    if (g_scriptEngine && event_name) {
        std::vector<q3::scripting::ScriptValue> args;
        if (arg) {
            args.push_back(std::string(arg));
        }
        g_scriptEngine->dispatch_event(event_name, args);
    }
}

int Modern_ReadFile(const char *qpath, void **buffer) {
    auto data = q3::fs::VirtualFileSystem::instance().read_binary(qpath);
    if (!data) return -1;
    if (buffer) {
        *buffer = Hunk_AllocateTempMemory(data->size() + 1);
        std::memcpy(*buffer, data->data(), data->size());
        ((char*)*buffer)[data->size()] = 0;
    }
    return static_cast<int>(data->size());
}

void Modern_WriteFile(const char *qpath, const void *buffer, int size) {
    q3::fs::VirtualFileSystem::instance().write_binary(
        qpath, static_cast<const uint8_t*>(buffer), static_cast<std::size_t>(size)
    );
}

void Modern_Cvar_NotifyChange(const char *var_name, const char *old_val, const char *new_val) {
    if (var_name) {
        q3::cvar::CvarManager::instance().notify_change(
            var_name,
            old_val ? old_val : "",
            new_val ? new_val : ""
        );
    }
}

float Modern_VectorNormalize(float *v) {
    q3::math::Vec3 vec(v);
    float len = vec.normalize();
    vec.to_c_array(v);
    return len;
}

void Modern_CrossProduct(const float *v1, const float *v2, float *cross) {
    q3::math::Vec3 vec1(v1);
    q3::math::Vec3 vec2(v2);
    vec1.cross(vec2).to_c_array(cross);
}

} // extern "C"
