# Discord Rich Presence Integration

## Overview
`q3::rpc::DiscordRpcManager` in `code/sys/rpc/` manages Discord Rich Presence status for singleplayer and multiplayer matches.

## Status Fields
- **state**: Match mode or state (e.g., `Playing FFA`, `In Main Menu`).
- **details**: Current server or match details.
- **map_name**: Current BSP map (e.g., `oa_minia`, `q3dm17`).
- **current_players** & **max_players**: Player count in session.

## C++ API
```cpp
auto& rpc = q3::rpc::DiscordRpcManager::instance();
rpc.init("CLIENT_ID");

q3::rpc::DiscordPresence presence;
presence.state = "Playing Free For All";
presence.map_name = "oa_minia";
presence.current_players = 4;
presence.max_players = 8;
presence.is_in_game = true;

rpc.update_presence(presence);
```
