#include "sys_api.h"
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

void Sys_SubsystemInit(void) {
    q3::log::Logger::instance().set_console_sink(ConsolePrintSink);
    LOG_INFO("Sys_SubsystemInit: Initializing subsystem layers");
    q3::multiplayer::SessionManager::instance().reset();
    
    if (!g_scriptEngine) {
        g_scriptEngine = new q3::scripting::ModernScriptEngine();
        LOG_INFO("Sys_SubsystemInit: Scripting engine initialized");
    }
    
    // Mount baseq3 using VFS
    q3::fs::VirtualFileSystem::instance().mount_search_path("baseq3");
    LOG_INFO("Sys_SubsystemInit: Mounted baseq3 into VirtualFileSystem");
}

void Sys_SubsystemFrame(int msec) {
    if (g_scriptEngine) {
        // Convert msec to seconds
        g_scriptEngine->update_timers(msec / 1000.0);
    }
}

void Sys_ScriptExecute(const char* script) {
    if (g_scriptEngine && script) {
        LOG_DEBUG("Sys_ScriptExecute: Running script (", std::strlen(script), " chars)");
        g_scriptEngine->execute(script);
    }
}

void Sys_ScriptEvent(const char* event_name, const char* arg) {
    if (g_scriptEngine && event_name) {
        LOG_DEBUG("Sys_ScriptEvent: Event ", event_name, arg ? " (arg: " : "", arg ? arg : "", arg ? ")" : "");
        std::vector<q3::scripting::ScriptValue> args;
        if (arg) {
            args.push_back(std::string(arg));
        }
        g_scriptEngine->dispatch_event(event_name, args);
    }
}

int Sys_VFS_ReadFile(const char *qpath, void **buffer) {
    if (!qpath) return -1;
    auto data = q3::fs::VirtualFileSystem::instance().read_binary(qpath);
    if (!data) return -1;

    LOG_DEBUG("Sys_VFS_ReadFile: VFS read ", qpath, " (", data->size(), " bytes)");
    if (buffer) {
        *buffer = Hunk_AllocateTempMemory(data->size() + 1);
        std::memcpy(*buffer, data->data(), data->size());
        ((char*)*buffer)[data->size()] = 0;
    }
    return static_cast<int>(data->size());
}

void Sys_VFS_WriteFile(const char *qpath, const void *buffer, int size) {
    if (!qpath || !buffer) return;
    LOG_INFO("Sys_VFS_WriteFile: VFS write ", qpath, " (", size, " bytes)");
    q3::fs::VirtualFileSystem::instance().write_binary(
        qpath, static_cast<const uint8_t*>(buffer), static_cast<std::size_t>(size)
    );
}

void Sys_Cvar_NotifyChange(const char *var_name, const char *old_val, const char *new_val) {
    if (var_name) {
        LOG_DEBUG("Sys_Cvar_NotifyChange: ", var_name, " changed to '", new_val ? new_val : "", "' (was '", old_val ? old_val : "", "')");
        q3::cvar::CvarManager::instance().notify_change(
            var_name,
            old_val ? old_val : "",
            new_val ? new_val : ""
        );
    }
}

float Sys_VectorNormalize(float *v) {
    q3::math::Vec3 vec(v);
    float len = vec.normalize();
    vec.to_c_array(v);
    return len;
}

void Sys_CrossProduct(const float *v1, const float *v2, float *cross) {
    q3::math::Vec3 vec1(v1);
    q3::math::Vec3 vec2(v2);
    vec1.cross(vec2).to_c_array(cross);
}

void Sys_SetConsoleSink(void (*sink)(const char *msg)) {
    q3::log::Logger::instance().set_console_sink(sink);
}

void Sys_LogDebug(const char *msg) {
    if (msg) LOG_DEBUG(msg);
}

void Sys_LogInfo(const char *msg) {
    if (msg) LOG_INFO(msg);
}

void Sys_LogWarn(const char *msg) {
    if (msg) LOG_WARN(msg);
}

void Sys_LogError(const char *msg) {
    if (msg) LOG_ERROR(msg);
}

} // extern "C"
