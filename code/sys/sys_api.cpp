#include "sys_api.h"
#include "cvar/cvar_manager.hpp"
#include "fs/vfs.hpp"
#include "net/http_downloader.hpp"
#include "multiplayer/session.hpp"
#include "scripting/script_engine.hpp"
#include "logger/logger.hpp"

#include <thread>

static q3::scripting::ScriptEngine* g_scriptEngine = nullptr;
static q3::net::HttpDownloader g_httpDownloader;
static cvar_t* g_logLevelCvar = nullptr;
static cvar_t* g_developerCvar = nullptr;

// com_logLevel: 0 debug, 1 info, 2 warn, 3 error. `developer 1` forces debug, so that turning
// the developer cvar on does not also require remembering this one.
static void ApplyLogLevel(void) {
    int level = g_logLevelCvar ? g_logLevelCvar->integer : 1;
    if (g_developerCvar && g_developerCvar->integer) {
        level = 0;
    }
    if (level < 0) {
        level = 0;
    }
    if (level > 3) {
        level = 3;
    }
    q3::log::Logger::instance().set_level(static_cast<q3::log::Level>(level));
}

static void ConsolePrintSink(const char* msg) {
    if (msg) {
        CL_ConsolePrint(msg);
    }
}

extern "C" {

void Sys_SubsystemInit(void) {
    // Before the sink is installed, so that a line logged from a worker during start-up is
    // queued rather than delivered on the wrong thread.
    q3::log::Logger::instance().set_main_thread(std::this_thread::get_id());

    // Quieter by default in an optimised build, because the info lines are development
    // commentary rather than something a player needs to read.
#ifdef NDEBUG
    g_logLevelCvar = Cvar_Get("com_logLevel", "2", CVAR_ARCHIVE);
#else
    g_logLevelCvar = Cvar_Get("com_logLevel", "1", CVAR_ARCHIVE);
#endif
    g_developerCvar = Cvar_Get("developer", "0", 0);
    ApplyLogLevel();

    q3::log::Logger::instance().set_console_sink(ConsolePrintSink);
    LOG_INFO("Sys_SubsystemInit: Initializing subsystem layers");
    q3::multiplayer::SessionManager::instance().reset();
    
    if (!g_scriptEngine) {
        g_scriptEngine = new q3::scripting::ScriptEngine();
        LOG_INFO("Sys_SubsystemInit: Scripting engine initialized");
    }
    
    // Mount baseq3 using VFS
    q3::fs::VirtualFileSystem::instance().mount_search_path("baseq3");
    LOG_INFO("Sys_SubsystemInit: Mounted baseq3 into VirtualFileSystem");
}

void Sys_SubsystemFrame(int msec) {
    if ((g_logLevelCvar && g_logLevelCvar->modified) ||
        (g_developerCvar && g_developerCvar->modified)) {
        if (g_logLevelCvar) {
            g_logLevelCvar->modified = qfalse;
        }
        if (g_developerCvar) {
            g_developerCvar->modified = qfalse;
        }
        ApplyLogLevel();
    }

    // Deliver anything the worker threads logged. Main thread only, which this is.
    q3::log::Logger::instance().flush_queued();

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

qboolean Sys_SanitizeDownloadFilename(const char *filename) {
    if (!filename || !*filename) return qfalse;

    std::string_view name(filename);

    // Prevent path traversal
    if (name.find("..") != std::string_view::npos || name.find(':') != std::string_view::npos) {
        LOG_WARN("Sys_SanitizeDownloadFilename: Path traversal blocked in '", filename, "'");
        return qfalse;
    }

    // Prevent absolute paths
    if (name.front() == '/' || name.front() == '\\') {
        LOG_WARN("Sys_SanitizeDownloadFilename: Absolute path blocked in '", filename, "'");
        return qfalse;
    }

    // Check dangerous file extensions
    auto ext_pos = name.rfind('.');
    if (ext_pos != std::string_view::npos) {
        std::string_view ext = name.substr(ext_pos);
        if (ext == ".so" || ext == ".dll" || ext == ".dylib" || ext == ".exe" ||
            ext == ".bat" || ext == ".sh" || ext == ".cmd") {
            LOG_WARN("Sys_SanitizeDownloadFilename: Executable extension blocked in '", filename, "'");
            return qfalse;
        }

        // Prevent config overwrites
        if (name == "autoexec.cfg" || name == "q3config.cfg" || name == "default.cfg") {
            LOG_WARN("Sys_SanitizeDownloadFilename: Config overwrite blocked in '", filename, "'");
            return qfalse;
        }
    }

    return qtrue;
}

void Sys_StartHttpDownload(const char *url, const char *outputPath) {
    if (!url || !outputPath) return;

    LOG_INFO("Sys_StartHttpDownload: ", url, " -> ", outputPath);
    g_httpDownloader.start_download(url, outputPath, [](std::size_t downloaded, std::size_t total) {
        Cvar_SetValue("cl_downloadCount", downloaded);
        if (total > 0) {
            Cvar_SetValue("cl_downloadSize", total);
        }
    });
}

int Sys_GetHttpDownloadStatus(void) {
    auto status = g_httpDownloader.status();
    switch (status) {
        case q3::net::DownloadStatus::Downloading: return 1;
        case q3::net::DownloadStatus::Completed:   return 2;
        case q3::net::DownloadStatus::Failed:      return 3;
        default:                                   return 0;
    }
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
