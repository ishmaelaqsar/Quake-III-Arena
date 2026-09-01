#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace q3::vulkan {

struct VulkanDeviceInfo {
    std::string device_name{"Unknown GPU"};
    uint32_t driver_version{0};
    uint32_t api_version{0};
    bool discrete_gpu{false};
};

class VulkanBackend {
public:
    static VulkanBackend& instance() noexcept {
        static VulkanBackend vk;
        return vk;
    }

    bool init(void* window_handle = nullptr);
    void shutdown();

    bool is_initialized() const noexcept { return initialized_; }
    const VulkanDeviceInfo& device_info() const noexcept { return device_info_; }

    void begin_frame();
    void end_frame();

private:
    VulkanBackend() = default;

    bool initialized_{false};
    VulkanDeviceInfo device_info_;
    uint32_t frame_index_{0};
};

} // namespace q3::vulkan
