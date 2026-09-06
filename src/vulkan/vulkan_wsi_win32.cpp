#include "vulkan_wsi_win32.hpp"
#include <volk.h>

WSIPlatformWin32::WSIPlatformWin32(HWND hwnd)
    : m_hwnd(hwnd)
{
}

void WSIPlatformWin32::set_hwnd(HWND hwnd)
{
    m_hwnd = hwnd;
}

VkSurfaceKHR WSIPlatformWin32::create_surface(VkInstance instance, VkPhysicalDevice)
{
    if (!m_hwnd || !IsWindow(m_hwnd))
        return VK_NULL_HANDLE;

    VkWin32SurfaceCreateInfoKHR info = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    info.hinstance = GetModuleHandle(nullptr);
    info.hwnd = m_hwnd;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (vkCreateWin32SurfaceKHR(instance, &info, nullptr, &surface) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    return surface;
}

void WSIPlatformWin32::destroy_surface(VkInstance instance, VkSurfaceKHR surface)
{
    if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }
}

VkFormat WSIPlatformWin32::get_preferred_format()
{
    return VK_FORMAT_B8G8R8A8_UNORM;
}

std::vector<const char *> WSIPlatformWin32::get_instance_extensions()
{
    return {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    };
}

std::vector<const char *> WSIPlatformWin32::get_device_extensions()
{
    return {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
}

uint32_t WSIPlatformWin32::get_surface_width()
{
    RECT rect;
    if (m_hwnd && GetClientRect(m_hwnd, &rect)) {
        uint32_t w = rect.right - rect.left;
        if (w == 0) w = 640;
        if (w != current_swapchain_width && current_swapchain_width != 0)
            resize = true;
        return w;
    }
    return 640;
}

uint32_t WSIPlatformWin32::get_surface_height()
{
    RECT rect;
    if (m_hwnd && GetClientRect(m_hwnd, &rect)) {
        uint32_t h = rect.bottom - rect.top;
        if (h == 0) h = 480;
        if (h != current_swapchain_height && current_swapchain_height != 0)
            resize = true;
        return h;
    }
    return 480;
}

float WSIPlatformWin32::get_aspect_ratio()
{
    uint32_t w = get_surface_width();
    uint32_t h = get_surface_height();
    if (h == 0) return 4.0f / 3.0f;
    return static_cast<float>(w) / static_cast<float>(h);
}

bool WSIPlatformWin32::alive(Vulkan::WSI &)
{
    return m_hwnd && IsWindow(m_hwnd);
}

void WSIPlatformWin32::poll_input()
{
}

void WSIPlatformWin32::poll_input_async(Granite::InputTrackerHandler *)
{
}
