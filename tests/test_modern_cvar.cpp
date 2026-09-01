#include <gtest/gtest.h>
#include "../code/sys/cvar/cvar_manager.hpp"

extern "C" {
void Com_InitSmallZoneMemory(void);
void Com_InitZoneMemory(void);
}

class ModernCvarFixture : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        Com_InitSmallZoneMemory();
        Cmd_Init();
        Cvar_Init();
        Com_InitZoneMemory();
    }
};

TEST_F(ModernCvarFixture, DeclareAndAccessTypedCvars) {
    auto& mgr = q3::cvar::CvarManager::instance();

    auto cv_int = mgr.declare("modern_int", 42, static_cast<int>(q3::cvar::Flag::Archive));
    EXPECT_EQ(cv_int.int_value(), 42);
    EXPECT_EQ(cv_int.string_value(), "42");

    cv_int.set(100);
    EXPECT_EQ(cv_int.int_value(), 100);

    auto cv_float = mgr.declare("modern_float", 12.5f);
    EXPECT_FLOAT_EQ(cv_float.float_value(), 12.5f);

    auto cv_bool = mgr.declare("modern_bool", true);
    EXPECT_TRUE(cv_bool.bool_value());
    cv_bool.set(false);
    EXPECT_FALSE(cv_bool.bool_value());
}

TEST_F(ModernCvarFixture, ChangeListeners) {
    auto& mgr = q3::cvar::CvarManager::instance();

    std::string observed_old;
    std::string observed_new;

    mgr.add_change_listener("listen_var", [&](std::string_view name, std::string_view old_val, std::string_view new_val) {
        observed_old = old_val;
        observed_new = new_val;
    });

    auto cv = mgr.declare("listen_var", "initial");
    cv.set("updated");

    EXPECT_EQ(observed_old, "initial");
    EXPECT_EQ(observed_new, "updated");
}

TEST_F(ModernCvarFixture, FastMapLookup) {
    auto& mgr = q3::cvar::CvarManager::instance();
    mgr.declare("fast_lookup_cvar", "speed_test");

    auto found = mgr.find("fast_lookup_cvar");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->string_value(), "speed_test");

    auto missing = mgr.find("non_existent_cvar_xyz");
    EXPECT_FALSE(missing.has_value());
}

TEST_F(ModernCvarFixture, OffsetofStructFieldSanity) {
    EXPECT_EQ(offsetof(cvar_t, name), 0);
    EXPECT_GT(offsetof(cvar_t, string), 0);
    EXPECT_GT(offsetof(cvar_t, value), offsetof(cvar_t, string));
}
