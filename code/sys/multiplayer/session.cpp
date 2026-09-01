#include "session.hpp"

namespace q3::multiplayer {

SessionManager::SessionManager() {
    players_.resize(MAX_LOCAL_PLAYERS);
    for (int i = 0; i < MAX_LOCAL_PLAYERS; ++i) {
        players_[i].slot_id = i;
        players_[i].active = false;
    }
}

bool SessionManager::add_local_player(std::string_view name) {
    for (auto& p : players_) {
        if (!p.active) {
            p.active = true;
            p.name = std::string(name);
            p.origin = {0.0f, 0.0f, 0.0f};
            p.viewangles = {0.0f, 0.0f, 0.0f};
            return true;
        }
    }
    return false;
}

bool SessionManager::remove_local_player(int slot_id) {
    if (slot_id >= 0 && slot_id < MAX_LOCAL_PLAYERS && players_[slot_id].active) {
        players_[slot_id].active = false;
        players_[slot_id].name = "Player";
        return true;
    }
    return false;
}

int SessionManager::active_player_count() const noexcept {
    int count = 0;
    for (const auto& p : players_) {
        if (p.active) ++count;
    }
    return count;
}

LocalPlayerSlot* SessionManager::get_player(int slot_id) {
    if (slot_id >= 0 && slot_id < MAX_LOCAL_PLAYERS && players_[slot_id].active) {
        return &players_[slot_id];
    }
    return nullptr;
}

void SessionManager::update_viewports(int screen_width, int screen_height, SplitScreenLayout layout) {
    int active = active_player_count();
    if (active <= 1) {
        for (auto& p : players_) {
            if (p.active) {
                p.viewport = {0, 0, screen_width, screen_height};
                break;
            }
        }
        return;
    }

    if (layout == SplitScreenLayout::TwoPlayerHorizontal) {
        int half_h = screen_height / 2;
        int idx = 0;
        for (auto& p : players_) {
            if (p.active) {
                p.viewport = {0, idx * half_h, screen_width, half_h};
                if (++idx >= 2) break;
            }
        }
    } else if (layout == SplitScreenLayout::TwoPlayerVertical) {
        int half_w = screen_width / 2;
        int idx = 0;
        for (auto& p : players_) {
            if (p.active) {
                p.viewport = {idx * half_w, 0, half_w, screen_height};
                if (++idx >= 2) break;
            }
        }
    } else if (layout == SplitScreenLayout::FourPlayerGrid) {
        int half_w = screen_width / 2;
        int half_h = screen_height / 2;
        int idx = 0;
        for (auto& p : players_) {
            if (p.active) {
                int row = idx / 2;
                int col = idx % 2;
                p.viewport = {col * half_w, row * half_h, half_w, half_h};
                if (++idx >= 4) break;
            }
        }
    }
}

void SessionManager::reset() {
    for (int i = 0; i < MAX_LOCAL_PLAYERS; ++i) {
        players_[i].active = false;
        players_[i].name = "Player";
        players_[i].origin = {0.0f, 0.0f, 0.0f};
        players_[i].viewangles = {0.0f, 0.0f, 0.0f};
    }
}

} // namespace q3::multiplayer
