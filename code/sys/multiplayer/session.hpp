#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <optional>
#include <cstdint>
#include "../net/transport.hpp"
#include "../math/vec3.hpp"

namespace q3::multiplayer {

struct ViewportRect {
    int x{0};
    int y{0};
    int width{0};
    int height{0};
};

struct LocalPlayerSlot {
    int slot_id{0};
    std::string name{"Player"};
    bool active{false};
    math::Vec3 origin{0.0f, 0.0f, 0.0f};
    math::Angles viewangles{0.0f, 0.0f, 0.0f};
    ViewportRect viewport;
    net::LoopbackTransport transport;
};

enum class SplitScreenLayout {
    SinglePlayer,
    TwoPlayerVertical,
    TwoPlayerHorizontal,
    FourPlayerGrid
};

class SessionManager {
public:
    static constexpr int MAX_LOCAL_PLAYERS = 4;

    static SessionManager& instance() noexcept {
        static SessionManager mgr;
        return mgr;
    }

    SessionManager();

    bool add_local_player(std::string_view name);
    bool remove_local_player(int slot_id);
    int active_player_count() const noexcept;

    LocalPlayerSlot* get_player(int slot_id);
    const std::vector<LocalPlayerSlot>& players() const noexcept { return players_; }

    void update_viewports(int screen_width, int screen_height, SplitScreenLayout layout);

    void reset();

private:
    std::vector<LocalPlayerSlot> players_;
};

} // namespace q3::multiplayer
