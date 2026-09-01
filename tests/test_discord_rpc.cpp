#include <gtest/gtest.h>
#include "../code/sys/rpc/discord_rpc.hpp"

TEST(DiscordRpcTest, PresenceInitializationAndUpdate) {
    auto& rpc = q3::rpc::DiscordRpcManager::instance();
    rpc.init("1234567890");

    q3::rpc::DiscordPresence presence;
    presence.state = "Playing FFA";
    presence.details = "Match in progress";
    presence.map_name = "q3dm17";
    presence.current_players = 4;
    presence.max_players = 8;
    presence.is_in_game = true;

    rpc.update_presence(presence);

    const auto& current = rpc.current_presence();
    EXPECT_EQ(current.state, "Playing FFA");
    EXPECT_EQ(current.map_name, "q3dm17");
    EXPECT_EQ(current.current_players, 4);
    EXPECT_EQ(current.max_players, 8);
    EXPECT_TRUE(current.is_in_game);

    rpc.shutdown();
}
