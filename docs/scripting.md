# Scripting Engine Documentation

## Overview
`q3::scripting::ModernScriptEngine` in `code/sys/scripting/` provides script execution, timer scheduling, and event dispatching.

## Key Features
- **Event Dispatch**: Dispatches events (e.g., `game_init`, `player_connect`, `map_change`) to registered event listeners.
- **Timer Queue**: Supports non-blocking scheduled timers triggered during `Sys_SubsystemFrame(msec)`.
- **C-API Interface**: Invoked from engine code via `Sys_ScriptExecute()` and `Sys_ScriptEvent()`.

## Usage Example
```cpp
auto& engine = q3::scripting::ModernScriptEngine::instance();
engine.add_event_listener("game_init", [](const std::vector<q3::scripting::ScriptValue>& args) {
    LOG_INFO("Game initialized with map ", args[0].to_string());
});
```
