#include <gtest/gtest.h>
#include "../code/renderer/vulkan/vk_backend.hpp"

TEST(VulkanBackendTest, LifecycleAndDeviceInfo) {
    auto& vk = q3::vulkan::VulkanBackend::instance();
    EXPECT_FALSE(vk.is_initialized());

    EXPECT_TRUE(vk.init(nullptr));
    EXPECT_TRUE(vk.is_initialized());

    const auto& info = vk.device_info();
    EXPECT_FALSE(info.device_name.empty());
    EXPECT_EQ(info.api_version, 130);

    vk.begin_frame();
    vk.end_frame();

    vk.shutdown();
    EXPECT_FALSE(vk.is_initialized());
}
