# Scripting Engine Documentation

## Overview
`q3::scripting::ScriptEngine` in `code/sys/scripting/` provides embedded scripting engine capabilities with Lua / LuaJIT compatibility.

## Key Features
- **Lua / LuaJIT Syntax Compatibility**: Executes scripts, functions, and expression evaluations.
- **Event Dispatch**: Dispatches events (e.g., `game_init`, `player_spawn`, `map_change`) to registered event listeners.
- **Timer Queue**: Supports non-blocking scheduled timers triggered during `Sys_SubsystemFrame(msec)`.
- **Entity Property Reflection**: Allows dynamic key-value property storage and query per entity ID.
- **C-API Interface**: Invoked from engine code via `Sys_ScriptExecute()` and `Sys_ScriptEvent()`.

## Usage Example
```cpp
auto& engine = q3::scripting::ScriptEngine::instance();
engine.subscribe_event("game_init", [](const std::vector<q3::scripting::ScriptValue>& args) {
    LOG_INFO("Game initialized with map ", std::get<std::string>(args[0]));
});
```
