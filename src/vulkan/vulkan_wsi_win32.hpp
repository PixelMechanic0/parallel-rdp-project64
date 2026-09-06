#ifndef VULKAN_WSI_WIN32_HPP
#define VULKAN_WSI_WIN32_HPP

#include "wsi.hpp"
#include <windows.h>
#include <vector>

class WSIPlatformWin32 : public Vulkan::WSIPlatform
{
public:
    explicit WSIPlatformWin32(HWND hwnd);
    ~WSIPlatformWin32() override = default;

    VkSurfaceKHR create_surface(VkInstance instance, VkPhysicalDevice gpu) override;
    void destroy_surface(VkInstance instance, VkSurfaceKHR surface) override;
    std::vector<const char *> get_instance_extensions() override;
    std::vector<const char *> get_device_extensions() override;

    uint32_t get_surface_width() override;
    uint32_t get_surface_height() override;
    float get_aspect_ratio() override;

    VkFormat get_preferred_format() override;

    bool alive(Vulkan::WSI &wsi) override;
    void poll_input() override;
    void poll_input_async(Granite::InputTrackerHandler *handler) override;

    void set_hwnd(HWND hwnd);
    HWND get_hwnd() const { return m_hwnd; }

private:
    HWND m_hwnd;
};

#endif // VULKAN_WSI_WIN32_HPP
