#include "vulkan_renderer.hpp"
#include "../util/rdp_log.hpp"
#include <volk.h>
#include <windows.h>
#include <algorithm>

VulkanRenderer::VulkanRenderer()
    : m_running(false)
    , m_rdram_size(0x400000)
{
}

VulkanRenderer::~VulkanRenderer()
{
    destroy();
}

bool VulkanRenderer::init(const GFX_INFO &gfx, uint32_t rdram_size)
{
    destroy();
    m_rdram_size = rdram_size;

    RDP_LOG_MSG("VulkanRenderer::init: start (hwnd=%p, rdram_size=0x%x)", gfx.hWnd, rdram_size);

    if (!Vulkan::Context::init_loader(nullptr)) {
        RDP_LOG_MSG("VulkanRenderer::init: Failed to initialize Vulkan loader via volk");
        return false;
    }
    RDP_LOG_MSG("VulkanRenderer::init: volk loader initialized");

    m_platform = std::make_unique<WSIPlatformWin32>(gfx.hWnd);
    m_wsi.set_platform(m_platform.get());
    m_wsi.set_present_mode(g_config.vsync ? Vulkan::PresentMode::SyncToVBlank : Vulkan::PresentMode::UnlockedForceTearing);
    m_wsi.set_backbuffer_format(Vulkan::BackbufferFormat::UNORM);
    m_wsi.set_extra_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    if (!m_wsi.init_simple(1, {})) {
        RDP_LOG_MSG("VulkanRenderer::init: WSI::init_simple failed!");
        m_wsi.set_platform(nullptr);
        m_platform.reset();
        return false;
    }
    RDP_LOG_MSG("VulkanRenderer::init: WSI initialized (swapchain %ux%u)",
                m_platform->get_surface_width(), m_platform->get_surface_height());

    auto &device = m_wsi.get_device();
    uintptr_t aligned_rdram = reinterpret_cast<uintptr_t>(gfx.RDRAM);
    uintptr_t offset = 0;

    bool supports_host = device.get_device_features().supports_external_memory_host;
    RDP_LOG_MSG("VulkanRenderer::init: device supports_external_memory_host=%d", supports_host ? 1 : 0);

    if (supports_host) {
        size_t align = device.get_device_features().host_memory_properties.minImportedHostPointerAlignment;
        if (align > 0) {
            offset = aligned_rdram & (align - 1);
            aligned_rdram -= offset;
            RDP_LOG_MSG("VulkanRenderer::init: host alignment=%zu, offset=%zu", align, offset);
        }
    }

    // Ensure the entire RDRAM range is committed and writable for host memory import
    if (gfx.RDRAM) {
        DWORD old_protect = 0;
        VirtualAlloc(gfx.RDRAM, m_rdram_size, MEM_COMMIT, PAGE_READWRITE);
        VirtualProtect(gfx.RDRAM, m_rdram_size, PAGE_READWRITE, &old_protect);

#if PARALLEL_RDP_LOG
        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery(gfx.RDRAM, &mbi, sizeof(mbi))) {
            RDP_LOG_MSG("VulkanRenderer::init: RDRAM after commit: Base=%p, RegionSize=0x%zx, State=0x%lx (MEM_COMMIT=%d), Protect=0x%lx, Type=0x%lx",
                        mbi.BaseAddress, mbi.RegionSize, (unsigned long)mbi.State, (mbi.State == MEM_COMMIT) ? 1 : 0,
                        (unsigned long)mbi.Protect, (unsigned long)mbi.Type);
        }
#endif
    }

    RDP::CommandProcessorFlags flags = 0;
    switch (g_config.upscaling) {
    case 2:
        flags |= RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_2X_BIT;
        break;
    case 4:
        flags |= RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_4X_BIT;
        break;
    case 8:
        flags |= RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_8X_BIT;
        break;
    default:
        break;
    }

    if (g_config.upscaling > 1 && g_config.ss_readbacks)
        flags |= RDP::COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_READ_BACK_BIT;
    if (g_config.ss_dither)
        flags |= RDP::COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_DITHER_BIT;

    m_frontend = std::make_unique<RDP::CommandProcessor>(
        device,
        reinterpret_cast<void *>(aligned_rdram),
        offset,
        m_rdram_size,
        m_rdram_size / 2,
        flags
    );

    if (!m_frontend->device_is_supported()) {
        RDP_LOG_MSG("VulkanRenderer::init: CommandProcessor::device_is_supported returned false!");
        m_frontend.reset();
        m_wsi.teardown();
        m_wsi.set_platform(nullptr);
        m_platform.reset();
        return false;
    }

    RDP::Quirks quirks;
    quirks.set_native_texture_lod(g_config.native_texture_lod);
    quirks.set_native_resolution_tex_rect(g_config.native_tex_rect);
    m_frontend->set_quirks(quirks);

    RDP_LOG_MSG("VulkanRenderer::init: success");
    m_running = true;
    m_initialized = true;
    return true;
}

void VulkanRenderer::destroy()
{
    if (!m_initialized) {
        return;
    }
    RDP_LOG_MSG("VulkanRenderer::destroy");
    m_initialized = false;
    m_running = false;

    // 1. Drain pending RDP commands from frontend ring buffer
    if (m_frontend) {
        m_frontend->idle();
    }

    // 2. Wait for all GPU queues to complete executing work BEFORE freeing any buffers
    try {
        m_wsi.get_device().wait_idle();
    } catch (...) {
    }

    // 3. Safely destroy frontend and release its Vulkan buffers (RDRAM, TMEM, etc.)
    if (m_frontend) {
        m_frontend.reset();
    }

    // 4. Wait for all buffer release operations to settle
    try {
        m_wsi.get_device().wait_idle();
    } catch (...) {
    }

    // 5. Synchronously tear down swapchain, surface, device and context
    m_wsi.teardown();
    m_wsi.set_platform(nullptr);
    m_platform.reset();
}

void VulkanRenderer::enqueue_commands(const uint32_t *commands, uint32_t count)
{
    if (m_running && m_frontend && commands && count > 0) {
        m_frontend->enqueue_command(count, commands);
    }
}

void VulkanRenderer::sync_full()
{
    if (m_running && m_frontend && g_config.synchronous) {
        m_frontend->wait_for_timeline(m_frontend->signal_timeline());
    }
}

void VulkanRenderer::render_frame(const GFX_INFO &gfx)
{
    if (!m_running || !m_frontend)
        return;

#if PARALLEL_RDP_LOG
    static uint32_t frame_count = 0;
    frame_count++;
#endif

    if (gfx.VI_STATUS_REG) m_frontend->set_vi_register(RDP::VIRegister::Control, *gfx.VI_STATUS_REG);
    if (gfx.VI_ORIGIN_REG) m_frontend->set_vi_register(RDP::VIRegister::Origin, *gfx.VI_ORIGIN_REG);
    if (gfx.VI_WIDTH_REG) m_frontend->set_vi_register(RDP::VIRegister::Width, *gfx.VI_WIDTH_REG);
    if (gfx.VI_INTR_REG) m_frontend->set_vi_register(RDP::VIRegister::Intr, *gfx.VI_INTR_REG);
    if (gfx.VI_V_CURRENT_LINE_REG) m_frontend->set_vi_register(RDP::VIRegister::VCurrentLine, *gfx.VI_V_CURRENT_LINE_REG);
    if (gfx.VI_V_BURST_REG) m_frontend->set_vi_register(RDP::VIRegister::Timing, *gfx.VI_V_BURST_REG);
    if (gfx.VI_V_SYNC_REG) m_frontend->set_vi_register(RDP::VIRegister::VSync, *gfx.VI_V_SYNC_REG);
    if (gfx.VI_H_SYNC_REG) m_frontend->set_vi_register(RDP::VIRegister::HSync, *gfx.VI_H_SYNC_REG);
    if (gfx.VI_LEAP_REG) m_frontend->set_vi_register(RDP::VIRegister::Leap, *gfx.VI_LEAP_REG);
    if (gfx.VI_H_START_REG) m_frontend->set_vi_register(RDP::VIRegister::HStart, *gfx.VI_H_START_REG);
    if (gfx.VI_V_START_REG) m_frontend->set_vi_register(RDP::VIRegister::VStart, *gfx.VI_V_START_REG);
    if (gfx.VI_V_BURST_REG) m_frontend->set_vi_register(RDP::VIRegister::VBurst, *gfx.VI_V_BURST_REG);
    if (gfx.VI_X_SCALE_REG) m_frontend->set_vi_register(RDP::VIRegister::XScale, *gfx.VI_X_SCALE_REG);
    if (gfx.VI_Y_SCALE_REG) m_frontend->set_vi_register(RDP::VIRegister::YScale, *gfx.VI_Y_SCALE_REG);

    RDP::ScanoutOptions opts = {};
    opts.persist_frame_on_invalid_input = true;
    opts.vi.aa = g_config.vi_aa;
    opts.vi.scale = g_config.vi_scale;
    opts.vi.dither_filter = g_config.dither_filter;
    opts.vi.divot_filter = g_config.divot_filter;
    opts.vi.gamma_dither = g_config.gamma_dither;
    opts.blend_previous_frame = g_config.interlacing;
    opts.upscale_deinterlacing = !g_config.interlacing;
    opts.crop_overscan_pixels = g_config.overscan_crop;

    m_frontend->begin_frame_context();

    Vulkan::ImageHandle scanout = m_frontend->scanout(opts);

    RDP_LOG_MSG("render_frame (#%u): scanout=%s VI_STATUS=0x%08x ORIGIN=0x%08x WIDTH=%u",
                frame_count,
                scanout ? "valid" : "null",
                gfx.VI_STATUS_REG ? *gfx.VI_STATUS_REG : 0,
                gfx.VI_ORIGIN_REG ? *gfx.VI_ORIGIN_REG : 0,
                gfx.VI_WIDTH_REG ? *gfx.VI_WIDTH_REG : 0);

    if (!m_wsi.begin_frame()) {
        RDP_LOG_MSG("render_frame (#%u): WSI::begin_frame() returned false", frame_count);
        return;
    }

    auto &device = m_wsi.get_device();
    auto cmd = device.request_command_buffer();
    auto &backbuffer = device.get_swapchain_view().get_image();

    uint32_t fb_w = backbuffer.get_width();
    uint32_t fb_h = backbuffer.get_height();

    cmd->image_barrier(backbuffer,
                       VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                       0,
                       VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT,
                       VK_ACCESS_2_TRANSFER_WRITE_BIT);

    if (!scanout || scanout->get_width() == 0 || scanout->get_height() == 0) {
        RDP_LOG_MSG("render_frame (#%u): empty scanout buffer, clearing swapchain", frame_count);
        VkClearValue black = {};
        black.color.float32[0] = 0.0f;
        black.color.float32[1] = 0.0f;
        black.color.float32[2] = 0.0f;
        black.color.float32[3] = 1.0f;
        cmd->clear_image(backbuffer, black);
        cmd->swapchain_touch_in_stages(VK_PIPELINE_STAGE_2_CLEAR_BIT);
        device.submit(cmd);
        m_wsi.end_frame();
        return;
    }

    uint32_t src_w = scanout->get_width();
    uint32_t src_h = scanout->get_height();

    int target_w = static_cast<int>(fb_w);
    int target_h = static_cast<int>(fb_h);

    if (!g_config.widescreen && fb_h > 0) {
        float fb_aspect = static_cast<float>(fb_w) / static_cast<float>(fb_h);
        float n64_aspect = 4.0f / 3.0f;
        if (fb_aspect > n64_aspect) {
            target_w = static_cast<int>(fb_h * n64_aspect);
            target_h = static_cast<int>(fb_h);
        } else {
            target_w = static_cast<int>(fb_w);
            target_h = static_cast<int>(fb_w / n64_aspect);
        }
    }

    int dst_x = (static_cast<int>(fb_w) - target_w) / 2;
    int dst_y = (static_cast<int>(fb_h) - target_h) / 2;

    if (dst_x > 0 || dst_y > 0) {
        VkClearValue black = {};
        black.color.float32[0] = 0.0f;
        black.color.float32[1] = 0.0f;
        black.color.float32[2] = 0.0f;
        black.color.float32[3] = 1.0f;
        cmd->clear_image(backbuffer, black);
    }

    cmd->image_barrier(*scanout,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                       VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_BLIT_BIT,
                       VK_ACCESS_2_TRANSFER_READ_BIT);

    VkOffset3D dst_offset0 = { dst_x, dst_y, 0 };
    VkOffset3D dst_extent = { target_w, target_h, 1 };
    VkOffset3D src_offset0 = { 0, 0, 0 };
    VkOffset3D src_extent = { static_cast<int32_t>(src_w), static_cast<int32_t>(src_h), 1 };

    cmd->blit_image(backbuffer, *scanout,
                    dst_offset0, dst_extent,
                    src_offset0, src_extent,
                    0, 0, 0, 0, 1, VK_FILTER_LINEAR);

    // Restore scanout image layout so subsequent frame's video_interface scanout doesn't trigger assertion
    cmd->image_barrier(*scanout,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_PIPELINE_STAGE_2_BLIT_BIT,
                       VK_ACCESS_2_TRANSFER_READ_BIT,
                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                       VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    // Notify Granite WSI that swapchain was touched by blit command
    cmd->swapchain_touch_in_stages(VK_PIPELINE_STAGE_2_BLIT_BIT);

    device.submit(cmd);
    m_wsi.end_frame();

    RDP_LOG_MSG("render_frame (#%u): presented %ux%u -> %ux%u (dst=%d,%d swapchain=%ux%u)",
                frame_count, src_w, src_h, target_w, target_h, dst_x, dst_y, fb_w, fb_h);
}

void VulkanRenderer::change_window()
{
}
