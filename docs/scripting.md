# Scripting Engine Documentation

## Overview
`q3::scripting::ScriptEngine` in `code/sys/scripting/` integrates **Lua / LuaJIT** using the C++17 [Sol2](https://github.com/ThePhD/sol2) single-header library (`code/sys/scripting/sol/sol.hpp`).

## Features
- **Full Lua / LuaJIT Support**: Execute scripts, evaluate expressions, and manipulate Lua tables via `sol::state`.
- **Event System**: Subscribe to engine events (`game_init`, `player_spawn`, `map_change`, etc.) and dispatch arguments.
- **Timer Queue**: Schedule delayed async callbacks (`engine.schedule(delay, callback)`).
- **Entity Property Reflection**: Attach dynamic properties to entity IDs.

---

## Sol2 Lua C++ API Reference

### 1. Executing Lua Code
```cpp
#include "scripting/script_engine.hpp"

q3::scripting::ScriptEngine engine;

// Execute Lua code string
engine.execute(R"(
    player_health = 100
    player_name = "Sarge"
    print("Player initialized: " .. player_name .. " with HP: " .. tostring(player_health))
)");
```

### 2. Accessing Lua Variables from C++
```cpp
sol::state& lua = engine.lua_state();

// Set variable in Lua state
lua["gravity"] = 800.0;
lua["map_name"] = "q3dm17";

// Read variable from Lua state
double gravity = lua["gravity"]; // 800.0
std::string map = lua["map_name"]; // "q3dm17"
```

### 3. Exposing C++ Functions to Lua
```cpp
sol::state& lua = engine.lua_state();

// Bind a C++ lambda function
lua.set_function("apply_damage", [](int entity_id, double damage) {
    LOG_INFO("Damage applied to entity ", entity_id, ": ", damage);
});

// Invoke from Lua script
engine.execute("apply_damage(1, 50.0)");
```

### 4. Event Subscription and Dispatch
```cpp
// Subscribe to event
engine.subscribe_event("player_spawn", [](const std::vector<q3::scripting::ScriptValue>& args) {
    if (!args.empty() && std::holds_alternative<std::string>(args[0])) {
        LOG_INFO("Player spawned: ", std::get<std::string>(args[0]));
    }
});

// Dispatch event
engine.dispatch_event("player_spawn", { "Ranger" });
```

### 5. Scheduled Timers
```cpp
// Schedule callback after 2.5 seconds
engine.schedule(2.5, []() {
    LOG_INFO("Timer fired after 2.5s delay!");
});

// Update timer queue during main frame loop
engine.update_timers(delta_time_seconds);
```

### 6. Entity Property Reflection
```cpp
// Set entity property
engine.set_entity_property(1, "team", "red");
engine.set_entity_property(1, "frags", 15.0);

// Get entity property
auto team = engine.get_entity_property(1, "team");
if (team) {
    std::string team_name = std::get<std::string>(*team); // "red"
}
```

---

## Example Lua Mod Script (`scripts/match_rules.lua`)
```lua
-- Match initialization hook
function on_game_init(map_name)
    print("Loading match rules for map: " .. map_name)
    set_gravity(800)
    enable_quad_damage(true)
end

-- Player frag event
function on_player_frag(attacker_id, victim_id)
    print("Player " .. tostring(attacker_id) .. " fragged Player " .. tostring(victim_id))
end
```
