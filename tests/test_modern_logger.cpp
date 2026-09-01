#include <gtest/gtest.h>
#include "../code/sys/logger/logger.hpp"
#include "../code/sys/sys_api.h"
#include <sstream>

TEST(ModernLoggerTest, MacroFormattingAndLevels) {
    testing::internal::CaptureStdout();

    LOG_DEBUG("Debug message ", 123);
    LOG_INFO("Info message ", "test");
    LOG_WARN("Warning message");

    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_NE(output.find("DEBUG"), std::string::npos);
    EXPECT_NE(output.find("Debug message 123"), std::string::npos);
    EXPECT_NE(output.find("INFO"), std::string::npos);
    EXPECT_NE(output.find("Info message test"), std::string::npos);
    EXPECT_NE(output.find("WARN"), std::string::npos);
}

TEST(ModernLoggerTest, ErrorLevelToStderr) {
    testing::internal::CaptureStderr();

    LOG_ERROR("Fatal error ", 500);

    std::string err_output = testing::internal::GetCapturedStderr();

    EXPECT_NE(err_output.find("ERROR"), std::string::npos);
    EXPECT_NE(err_output.find("Fatal error 500"), std::string::npos);
}

TEST(ModernLoggerTest, CApiLoggingWrappers) {
    testing::internal::CaptureStdout();

    Modern_LogInfo("C-API Info message");

    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_NE(output.find("INFO"), std::string::npos);
    EXPECT_NE(output.find("C-API Info message"), std::string::npos);
}
