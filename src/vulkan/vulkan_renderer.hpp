#ifndef VULKAN_RENDERER_HPP
#define VULKAN_RENDERER_HPP

#include <memory>
#include <vector>
#include <stdint.h>
#include <stdbool.h>

#include "vulkan_wsi_win32.hpp"
#include "rdp_device.hpp"
#include "config.hpp"
#include "pj64_gfx.h"

class VulkanRenderer
{
public:
    VulkanRenderer();
    ~VulkanRenderer();

    bool init(const GFX_INFO &gfx, uint32_t rdram_size);
    void destroy();

    void enqueue_commands(const uint32_t *commands, uint32_t count);
    void sync_full();
    void render_frame(const GFX_INFO &gfx);
    void change_window();

    bool is_running() const { return m_running; }

private:
    bool m_running = false;
    bool m_initialized = false;
    bool m_frame_open = false;
    std::unique_ptr<WSIPlatformWin32> m_platform;
    Vulkan::WSI m_wsi;
    std::unique_ptr<RDP::CommandProcessor> m_frontend;
    uint32_t m_rdram_size = 0x400000;
};

#endif // VULKAN_RENDERER_HPP
