#include "sys_api.h"
#include "cvar/cvar_manager.hpp"
#include "fs/vfs.hpp"
#include "net/http_downloader.hpp"
#include "multiplayer/session.hpp"
#include "scripting/script_engine.hpp"
#include "logger/logger.hpp"

#include <string>
#include <string_view>
#include <thread>
#include <memory>

static std::unique_ptr<q3::scripting::ScriptEngine> g_scriptEngine;
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
    Q3_NOEXCEPT_BOUNDARY(
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
        
        try {
            if (!g_scriptEngine) {
                g_scriptEngine = std::make_unique<q3::scripting::ScriptEngine>();
                LOG_INFO("Sys_SubsystemInit: Scripting engine initialized");
            }
        } catch (const std::exception &e) {
            Com_Printf("^1Scripting disabled: %s\n", e.what());
        }
    )
}

void Sys_SubsystemShutdown(void) {
    Q3_NOEXCEPT_BOUNDARY(
        g_httpDownloader.cancel();
        g_scriptEngine.reset();
        q3::log::Logger::instance().flush_queued();
    )
}

void Sys_LogApplyLevel(void) {
    Q3_NOEXCEPT_BOUNDARY(
        ApplyLogLevel();
        LOG_DEBUG("Sys_LogApplyLevel: com_logLevel is now ",
                  g_logLevelCvar ? g_logLevelCvar->integer : -1);
    )
}

void Sys_SubsystemFrame(int msec) {
    Q3_NOEXCEPT_BOUNDARY(
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
    )
}

qboolean Sys_ScriptExecute(const char* script) {
    if (!g_scriptEngine || !script) return qfalse;
    Q3_NOEXCEPT_BOUNDARY(
        LOG_DEBUG("Sys_ScriptExecute: Running script (", std::strlen(script), " chars)");
        return g_scriptEngine->execute(script) ? qtrue : qfalse;
    )
    return qfalse;
}

void Sys_ScriptEvent(const char* event_name, const char* arg) {
    if (!g_scriptEngine || !event_name) return;
    Q3_NOEXCEPT_BOUNDARY(
        LOG_DEBUG("Sys_ScriptEvent: Event ", event_name, arg ? " (arg: " : "", arg ? arg : "", arg ? ")" : "");
        std::vector<q3::scripting::ScriptValue> args;
        if (arg) {
            args.push_back(std::string(arg));
        }
        g_scriptEngine->dispatch_event(event_name, args);
    )
}

// Names the download code will accept. An allowlist, not a blacklist: the previous version
// enumerated dangerous extensions, which let through `.command`, `.py`, `.dylib.1`, and any
// case variant such as `evil.SO`, and its config-overwrite check was an exact whole-string
// compare nested inside the has-an-extension branch, so `maps/autoexec.cfg` and any
// extensionless name skipped it entirely.
//
// The only legitimate shape is `<gamedir>/<name>.pk3`. FS_ComparePaks builds the download list
// from the server's referenced pak names, which are `<gamedir>/<basename>` with `.pk3`
// appended (code/qcommon/files.cpp:2588-2614), so nothing else needs to pass.
static qboolean Sys_DownloadSegmentIsSane(std::string_view segment) {
    if (segment.empty()) {
        return qfalse;  // an empty segment means a leading, trailing, or doubled slash
    }
    if (segment == "." || segment == "..") {
        return qfalse;
    }
    for (const char c : segment) {
        const bool allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                             (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!allowed) {
            return qfalse;  // rejects '\\', ':', whitespace, control characters, and '%'
        }
    }
    return qtrue;
}

static qboolean Sys_DownloadHasPk3Extension(std::string_view file) {
    const std::string_view suffix(".pk3");
    if (file.size() <= suffix.size()) {
        return qfalse;  // the stem must not be empty
    }
    const std::string_view actual = file.substr(file.size() - suffix.size());
    return Q_stricmpn(actual.data(), suffix.data(), static_cast<int>(suffix.size())) == 0 ? qtrue
                                                                                         : qfalse;
}

// The paks that ship with the game. A server must never be able to replace them.
static qboolean Sys_DownloadIsStockPak(std::string_view gamedir, std::string_view file) {
    if (file.size() != sizeof("pak0.pk3") - 1) {
        return qfalse;
    }
    if (Q_stricmpn(file.data(), "pak", 3) != 0 || file[4] != '.') {
        return qfalse;
    }
    const char digit = file[3];
    if (digit < '0' || digit > '9') {
        return qfalse;
    }
    if (Q_stricmp(std::string(gamedir).c_str(), BASEGAME) == 0) {
        return digit <= '8' ? qtrue : qfalse;
    }
    if (Q_stricmp(std::string(gamedir).c_str(), "missionpack") == 0) {
        return digit <= '3' ? qtrue : qfalse;
    }
    return qfalse;
}

qboolean Sys_SanitizeDownloadFilename(const char *filename) {
    if (!filename || !*filename) {
        return qfalse;
    }

    Q3_NOEXCEPT_BOUNDARY(
        const std::string_view name(filename);
        if (name.size() >= MAX_QPATH) {
            LOG_WARN("Sys_SanitizeDownloadFilename: too long: '", filename, "'");
            return qfalse;
        }

        // Exactly two segments: the game directory and the pak file.
        const std::size_t slash = name.find('/');
        if (slash == std::string_view::npos || name.find('/', slash + 1) != std::string_view::npos) {
            LOG_WARN("Sys_SanitizeDownloadFilename: expected <gamedir>/<name>.pk3, got '", filename, "'");
            return qfalse;
        }

        const std::string_view gamedir = name.substr(0, slash);
        const std::string_view file = name.substr(slash + 1);

        if (!Sys_DownloadSegmentIsSane(gamedir) || !Sys_DownloadSegmentIsSane(file)) {
            LOG_WARN("Sys_SanitizeDownloadFilename: unsafe path component in '", filename, "'");
            return qfalse;
        }

        if (!Sys_DownloadHasPk3Extension(file)) {
            LOG_WARN("Sys_SanitizeDownloadFilename: only .pk3 files may be downloaded: '", filename, "'");
            return qfalse;
        }

        // The game directory must be the base game or the mod the client is running, so that a
        // server cannot write into an unrelated directory.
        const char *fsGame = Cvar_VariableString("fs_game");
        const bool gamedirAllowed =
            Q_stricmp(std::string(gamedir).c_str(), BASEGAME) == 0 ||
            (fsGame && *fsGame && Q_stricmp(std::string(gamedir).c_str(), fsGame) == 0);
        if (!gamedirAllowed) {
            LOG_WARN("Sys_SanitizeDownloadFilename: game directory not allowed in '", filename, "'");
            return qfalse;
        }

        if (Sys_DownloadIsStockPak(gamedir, file)) {
            LOG_WARN("Sys_SanitizeDownloadFilename: refusing to overwrite a stock pak: '", filename, "'");
            return qfalse;
        }

        return qtrue;
    )
    return qfalse;
}

void Sys_StartHttpDownload(const char *url, const char *outputPath) {
    if (!url || !outputPath) return;

    Q3_NOEXCEPT_BOUNDARY(
        LOG_INFO("Sys_StartHttpDownload: ", url, " -> ", outputPath);
        g_httpDownloader.start_download(url, outputPath, [](std::size_t downloaded, std::size_t total) {
            Cvar_SetValue("cl_downloadCount", downloaded);
            if (total > 0) {
                Cvar_SetValue("cl_downloadSize", total);
            }
        });
    )
}

int Sys_GetHttpDownloadStatus(void) {
    Q3_NOEXCEPT_BOUNDARY(
        auto status = g_httpDownloader.status();
        switch (status) {
            case q3::net::DownloadStatus::Downloading: return 1;
            case q3::net::DownloadStatus::Completed:   return 2;
            case q3::net::DownloadStatus::Failed:      return 3;
            default:                                   return 0;
        }
    )
    return 0;
}

void Sys_Cvar_NotifyChange(const char *var_name, const char *old_val, const char *new_val) {
    if (var_name) {
        Q3_NOEXCEPT_BOUNDARY(
            LOG_DEBUG("Sys_Cvar_NotifyChange: ", var_name, " changed to '", new_val ? new_val : "", "' (was '", old_val ? old_val : "", "')");
            q3::cvar::CvarManager::instance().notify_change(
                var_name,
                old_val ? old_val : "",
                new_val ? new_val : ""
            );
        )
    }
}

float Sys_VectorNormalize(float *v) {
    Q3_NOEXCEPT_BOUNDARY(
        q3::math::Vec3 vec(v);
        float len = vec.normalize();
        vec.to_c_array(v);
        return len;
    )
    return 0.0f;
}

void Sys_CrossProduct(const float *v1, const float *v2, float *cross) {
    Q3_NOEXCEPT_BOUNDARY(
        q3::math::Vec3 vec1(v1);
        q3::math::Vec3 vec2(v2);
        vec1.cross(vec2).to_c_array(cross);
    )
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
