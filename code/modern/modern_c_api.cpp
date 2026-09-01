#include "modern_c_api.h"
#include "cvar/cvar_manager.hpp"
#include "fs/vfs.hpp"
#include "multiplayer/session.hpp"
#include "scripting/script_engine.hpp"
#include "logger/logger.hpp"

static q3::scripting::ModernScriptEngine* g_scriptEngine = nullptr;

static void ConsolePrintSink(const char* msg) {
    if (msg) {
        CL_ConsolePrint(const_cast<char*>(msg));
    }
}

extern "C" {

void Modern_Init(void) {
    q3::log::Logger::instance().set_console_sink(ConsolePrintSink);
    LOG_INFO("Modern_Init: Initializing C++17 modern subsystem layers");
    q3::multiplayer::SessionManager::instance().reset();
    
    if (!g_scriptEngine) {
        g_scriptEngine = new q3::scripting::ModernScriptEngine();
        LOG_INFO("Modern_Init: Scripting engine initialized");
    }
    
    // Mount baseq3 using modern VFS
    q3::fs::VirtualFileSystem::instance().mount_search_path("baseq3");
    LOG_INFO("Modern_Init: Mounted baseq3 into VirtualFileSystem");
}

void Modern_Frame(int msec) {
    if (g_scriptEngine) {
        // Convert msec to seconds
        g_scriptEngine->update_timers(msec / 1000.0);
    }
}

void Modern_ScriptExecute(const char* script) {
    if (g_scriptEngine && script) {
        LOG_DEBUG("Modern_ScriptExecute: Running script (", std::strlen(script), " chars)");
        g_scriptEngine->execute(script);
    }
}

void Modern_ScriptEvent(const char* event_name, const char* arg) {
    if (g_scriptEngine && event_name) {
        LOG_DEBUG("Modern_ScriptEvent: Event ", event_name, arg ? " (arg: " : "", arg ? arg : "", arg ? ")" : "");
        std::vector<q3::scripting::ScriptValue> args;
        if (arg) {
            args.push_back(std::string(arg));
        }
        g_scriptEngine->dispatch_event(event_name, args);
    }
}

int Modern_ReadFile(const char *qpath, void **buffer) {
    if (!qpath) return -1;
    auto data = q3::fs::VirtualFileSystem::instance().read_binary(qpath);
    if (!data) return -1;

    LOG_DEBUG("Modern_ReadFile: VFS read ", qpath, " (", data->size(), " bytes)");
    if (buffer) {
        *buffer = Hunk_AllocateTempMemory(data->size() + 1);
        std::memcpy(*buffer, data->data(), data->size());
        ((char*)*buffer)[data->size()] = 0;
    }
    return static_cast<int>(data->size());
}

void Modern_WriteFile(const char *qpath, const void *buffer, int size) {
    if (!qpath || !buffer) return;
    LOG_INFO("Modern_WriteFile: VFS write ", qpath, " (", size, " bytes)");
    q3::fs::VirtualFileSystem::instance().write_binary(
        qpath, static_cast<const uint8_t*>(buffer), static_cast<std::size_t>(size)
    );
}

void Modern_Cvar_NotifyChange(const char *var_name, const char *old_val, const char *new_val) {
    if (var_name) {
        LOG_DEBUG("Modern_Cvar_NotifyChange: ", var_name, " changed to '", new_val ? new_val : "", "' (was '", old_val ? old_val : "", "')");
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

void Modern_SetConsoleSink(void (*sink)(const char *msg)) {
    q3::log::Logger::instance().set_console_sink(sink);
}

void Modern_LogDebug(const char *msg) {
    if (msg) LOG_DEBUG(msg);
}

void Modern_LogInfo(const char *msg) {
    if (msg) LOG_INFO(msg);
}

void Modern_LogWarn(const char *msg) {
    if (msg) LOG_WARN(msg);
}

void Modern_LogError(const char *msg) {
    if (msg) LOG_ERROR(msg);
}

} // extern "C"
