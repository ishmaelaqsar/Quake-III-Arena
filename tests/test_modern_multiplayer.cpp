#include <gtest/gtest.h>
#include "../code/sys/multiplayer/session.hpp"

using namespace q3::multiplayer;

TEST(ModernMultiplayerTest, SessionAddAndRemovePlayers) {
    auto& session = SessionManager::instance();
    session.reset();

    EXPECT_EQ(session.active_player_count(), 0);

    EXPECT_TRUE(session.add_local_player("Player 1"));
    EXPECT_TRUE(session.add_local_player("Player 2"));
    EXPECT_EQ(session.active_player_count(), 2);

    auto* p1 = session.get_player(0);
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ(p1->name, "Player 1");

    auto* p2 = session.get_player(1);
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(p2->name, "Player 2");

    EXPECT_TRUE(session.remove_local_player(0));
    EXPECT_EQ(session.active_player_count(), 1);
    EXPECT_EQ(session.get_player(0), nullptr);
}

TEST(ModernMultiplayerTest, SplitScreenViewportLayout) {
    auto& session = SessionManager::instance();
    session.reset();

    session.add_local_player("P1");
    session.add_local_player("P2");

    int screen_w = 1920;
    int screen_h = 1080;

    session.update_viewports(screen_w, screen_h, SplitScreenLayout::TwoPlayerHorizontal);

    auto* p1 = session.get_player(0);
    auto* p2 = session.get_player(1);

    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);

    EXPECT_EQ(p1->viewport.width, 1920);
    EXPECT_EQ(p1->viewport.height, 540);
    EXPECT_EQ(p1->viewport.y, 0);

    EXPECT_EQ(p2->viewport.width, 1920);
    EXPECT_EQ(p2->viewport.height, 540);
    EXPECT_EQ(p2->viewport.y, 540);
}
