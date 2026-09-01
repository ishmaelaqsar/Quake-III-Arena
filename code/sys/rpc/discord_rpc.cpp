#include "discord_rpc.hpp"
#include "../logger/logger.hpp"

namespace q3::rpc {

void DiscordRpcManager::init(std::string_view client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    client_id_ = std::string(client_id);
    initialized_ = true;
    LOG_INFO("DiscordRpcManager: Initialized Discord RPC with client ID ", client_id_);
}

void DiscordRpcManager::update_presence(const DiscordPresence& presence) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_presence_ = presence;
    if (initialized_) {
        LOG_DEBUG("DiscordRpcManager: Presence updated -> ", presence.state, " | ", presence.details, " [Map: ", presence.map_name, "]");
    }
}

void DiscordRpcManager::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        LOG_INFO("DiscordRpcManager: Shutting down Discord RPC");
        initialized_ = false;
    }
}

} // namespace q3::rpc
