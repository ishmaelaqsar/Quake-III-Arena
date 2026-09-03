/*
Tests for the engine logger (checklist 02 phase B4).

These assert through an installed sink rather than by capturing stdout, because the sink is what
the engine actually consumes and because stdout capture cannot express the properties that
matter: level filtering, basename-only paths, and off-main-thread queueing.

Note for checklist 03 step C1: these belong in `q3sys_tests` once the test binary is split. They
live in `quake3_tests` today because that is the only binary.
*/

#include <gtest/gtest.h>

#include <string>
#include <thread>

#include "../code/sys/logger/logger.hpp"

namespace {

// The sink is a plain function pointer, so the capture buffer has to be file-scope.
std::string g_captured;

void CapturingSink(const char *line) {
    if (line != nullptr) {
        g_captured += line;
    }
}

class LoggerFixture : public ::testing::Test {
protected:
    void SetUp() override {
        q3::log::Logger &logger = q3::log::Logger::instance();
        logger.flush_queued();  // discard anything an earlier test left queued
        g_captured.clear();
        logger.set_main_thread(std::this_thread::get_id());
        logger.set_level(q3::log::Level::Debug);
        logger.set_console_sink(CapturingSink);
    }
};

}  // namespace

// A formatted line reaches the sink with its level tag and its arguments concatenated.
TEST_F(LoggerFixture, CapturingSinkReceivesFormattedLine) {
    LOG_INFO("value is ", 42, " and ", 1.5);

    EXPECT_NE(g_captured.find("[INFO]"), std::string::npos) << g_captured;
    EXPECT_NE(g_captured.find("value is 42 and 1.5"), std::string::npos) << g_captured;
    EXPECT_EQ(g_captured.back(), '\n');
}

// Raising the level drops quieter lines. This is the property that makes a LOG_DEBUG in a hot
// path acceptable: nothing is formatted when the level filters it out.
TEST_F(LoggerFixture, LevelFilterDropsInfoBelowWarning) {
    q3::log::Logger::instance().set_level(q3::log::Level::Warning);

    LOG_DEBUG("debug line");
    LOG_INFO("info line");
    LOG_WARN("warn line");
    LOG_ERROR("error line");

    EXPECT_EQ(g_captured.find("debug line"), std::string::npos) << g_captured;
    EXPECT_EQ(g_captured.find("info line"), std::string::npos) << g_captured;
    EXPECT_NE(g_captured.find("warn line"), std::string::npos) << g_captured;
    EXPECT_NE(g_captured.find("error line"), std::string::npos) << g_captured;
}

// Lines name the source file, not the build path, so a user's console shows no directories.
TEST_F(LoggerFixture, LineCarriesBasenameNotAbsolutePath) {
    LOG_WARN("path check");

    EXPECT_NE(g_captured.find("test_modern_logger.cpp:"), std::string::npos) << g_captured;
    EXPECT_EQ(g_captured.find("/src/"), std::string::npos) << g_captured;
    EXPECT_EQ(g_captured.find("/Users/"), std::string::npos) << g_captured;
    EXPECT_EQ(g_captured.find("code/sys"), std::string::npos) << g_captured;
}

// The console sink walks engine state with no lock, so a worker's line waits for the main
// thread to flush it.
TEST_F(LoggerFixture, OffMainLogIsQueuedUntilFlush) {
    std::thread worker([] { LOG_WARN("from a worker thread"); });
    worker.join();

    EXPECT_EQ(g_captured.find("from a worker thread"), std::string::npos)
        << "a worker's line reached the sink directly: " << g_captured;
    EXPECT_EQ(q3::log::Logger::instance().queued_count(), 1u);

    q3::log::Logger::instance().flush_queued();

    EXPECT_NE(g_captured.find("from a worker thread"), std::string::npos) << g_captured;
    EXPECT_EQ(q3::log::Logger::instance().queued_count(), 0u);
}

// The regression test for the original defect: every LOG_* macro expanded to ((void)0) whenever
// NDEBUG was defined, so release builds reported nothing. The default build defines NDEBUG.
TEST_F(LoggerFixture, WarningsAndErrorsSurviveNDEBUG) {
#ifdef NDEBUG
    const bool releaseBuild = true;
#else
    const bool releaseBuild = false;
#endif

    LOG_WARN("warning survives");
    LOG_ERROR("error survives");

    EXPECT_NE(g_captured.find("warning survives"), std::string::npos)
        << "release build: " << releaseBuild << ", captured: " << g_captured;
    EXPECT_NE(g_captured.find("error survives"), std::string::npos) << g_captured;
}

// The queue is bounded, and the drop is reported rather than passed over in silence.
TEST_F(LoggerFixture, QueueIsBoundedAndReportsDrops) {
    const std::size_t overBy = 5;
    std::thread worker([overBy] {
        for (std::size_t i = 0; i < q3::log::Logger::kMaxQueued + overBy; ++i) {
            LOG_WARN("line ", i);
        }
    });
    worker.join();

    EXPECT_EQ(q3::log::Logger::instance().queued_count(), q3::log::Logger::kMaxQueued);

    q3::log::Logger::instance().flush_queued();

    EXPECT_NE(g_captured.find(std::to_string(overBy) + " log lines dropped"), std::string::npos)
        << "the drop was not reported";
}
