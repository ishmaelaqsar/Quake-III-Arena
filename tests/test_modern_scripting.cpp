#include <gtest/gtest.h>
#include "../code/sys/scripting/script_engine.hpp"

using namespace q3::scripting;

TEST(ModernScriptingTest, VariableAssignmentAndEvaluation) {
    ScriptEngine engine;

    engine.execute("let health = 100\nset gravity 800");

    auto health = engine.get_variable("health");
    ASSERT_TRUE(health.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*health), 100.0);

    auto gravity = engine.get_variable("gravity");
    ASSERT_TRUE(gravity.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*gravity), 800.0);
}

TEST(ModernScriptingTest, CustomFunctionExecution) {
    ScriptEngine engine;

    double recorded_damage = 0.0;
    engine.register_function("apply_damage", [&](const std::vector<ScriptValue>& args) -> ScriptValue {
        if (!args.empty() && std::holds_alternative<double>(args[0])) {
            recorded_damage = std::get<double>(args[0]);
        }
        return ScriptValue{};
    });

    engine.execute("apply_damage 75");
    EXPECT_DOUBLE_EQ(recorded_damage, 75.0);
}

TEST(ModernScriptingTest, EventSubscriptionAndDispatch) {
    ScriptEngine engine;

    bool player_spawned = false;
    std::string player_name;

    engine.subscribe_event("player_spawn", [&](const std::vector<ScriptValue>& args) {
        player_spawned = true;
        if (!args.empty() && std::holds_alternative<std::string>(args[0])) {
            player_name = std::get<std::string>(args[0]);
        }
    });

    engine.execute("emit player_spawn \"Ranger\"");

    EXPECT_TRUE(player_spawned);
    EXPECT_EQ(player_name, "Ranger");
}

TEST(ModernScriptingTest, ScheduledTimerQueue) {
    ScriptEngine engine;

    bool timer_fired = false;
    engine.schedule(1.5, [&]() {
        timer_fired = true;
    });

    // Advance time before trigger
    engine.update_timers(1.0);
    EXPECT_FALSE(timer_fired);

    // Advance time past trigger
    engine.update_timers(1.6);
    EXPECT_TRUE(timer_fired);
}

TEST(ModernScriptingTest, EntityPropertyReflection) {
    ScriptEngine engine;

    engine.set_entity_property(1, "score", 42.0);
    engine.set_entity_property(1, "team", "red");

    auto score = engine.get_entity_property(1, "score");
    auto team = engine.get_entity_property(1, "team");

    ASSERT_TRUE(score.has_value());
    ASSERT_TRUE(team.has_value());

    EXPECT_DOUBLE_EQ(std::get<double>(*score), 42.0);
    EXPECT_EQ(std::get<std::string>(*team), "red");
}
