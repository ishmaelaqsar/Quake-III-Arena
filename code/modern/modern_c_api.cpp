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

} // extern "C"
