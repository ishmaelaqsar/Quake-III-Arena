#pragma once

#include <string>
#include <string_view>
#include <atomic>
#include <mutex>

namespace q3::rpc {

struct DiscordPresence {
    std::string state{"In Main Menu"};
    std::string details{"Quake III Arena (C++17)"};
    std::string map_name{"Menu"};
    std::string large_image_key{"logo"};
    std::string large_image_text{"Quake III Arena Modern"};
    int current_players{0};
    int max_players{0};
    bool is_in_game{false};
};

class DiscordRpcManager {
public:
    static DiscordRpcManager& instance() noexcept {
        static DiscordRpcManager mgr;
        return mgr;
    }

    void init(std::string_view client_id = "123456789012345678");
    void update_presence(const DiscordPresence& presence);
    void shutdown();

    const DiscordPresence& current_presence() const noexcept { return current_presence_; }

private:
    DiscordRpcManager() = default;

    std::string client_id_;
    DiscordPresence current_presence_;
    std::atomic<bool> initialized_{false};
    mutable std::mutex mutex_;
};

} // namespace q3::rpc
