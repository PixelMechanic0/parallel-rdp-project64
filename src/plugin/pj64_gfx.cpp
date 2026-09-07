#include "pj64_gfx.h"
#include "../config/config.hpp"
#include "../vulkan/vulkan_renderer.hpp"
#include "../util/rdp_log.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory>

#define DP_STATUS_XBUS_DMA 0x01
#define DP_INTERRUPT       0x20

static GFX_INFO g_gfx;
static uint32_t g_rdram_size = 0x400000;
static bool g_rom_open = false;
static std::unique_ptr<VulkanRenderer> g_renderer;

static int cmd_cur = 0;
static int cmd_ptr = 0;
static uint32_t cmd_data[0x00040000 >> 2];

static uint32_t g_dlist_calls = 0;
static uint32_t g_rdplist_calls = 0;

static bool g_fullscreen = false;
static HMENU g_old_menu = NULL;
static LONG g_old_style = 0;
static WINDOWPLACEMENT g_old_pos;

static const unsigned cmd_len_lut[64] = {
    1, 1, 1, 1, 1, 1, 1, 1, 4, 6, 12, 14, 12, 14, 20, 22,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,
    1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,
};

static void win32_client_resize(HWND hWnd, HWND hStatus, int32_t nWidth, int32_t nHeight)
{
    if (!hWnd || !IsWindow(hWnd)) return;

    RECT rclient;
    GetClientRect(hWnd, &rclient);

    RECT rwin;
    GetWindowRect(hWnd, &rwin);

    if (hStatus && IsWindow(hStatus)) {
        RECT rstatus;
        GetClientRect(hStatus, &rstatus);
        rclient.bottom -= rstatus.bottom;
    }

    POINT pdiff;
    pdiff.x = (rwin.right - rwin.left) - (rclient.right - rclient.left);
    pdiff.y = (rwin.bottom - rwin.top) - (rclient.bottom - rclient.top);

    MoveWindow(hWnd, rwin.left, rwin.top, nWidth + pdiff.x, nHeight + pdiff.y, TRUE);
}

static void ensure_window_size(HWND hwnd, HWND hStatus)
{
    if (!hwnd || !IsWindow(hwnd) || g_fullscreen) return;

    int target_w = (int)g_config.window_width;
    int target_h = (int)g_config.window_height;
    if (target_w <= 0) target_w = 800;
    if (target_h <= 0) target_h = 600;

    RECT rect;
    if (GetClientRect(hwnd, &rect)) {
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;
        if (width != target_w || height != target_h) {
            RDP_LOG_MSG("ensure_window_size: resizing from %dx%d to %dx%d", width, height, target_w, target_h);
            LONG style = GetWindowLongA(hwnd, GWL_STYLE);
            if ((style & WS_SIZEBOX) == 0) {
                style |= WS_SIZEBOX;
                SetWindowLongA(hwnd, GWL_STYLE, style);
            }
            win32_client_resize(hwnd, hStatus, target_w, target_h);
        }
    }
}

static void toggle_fullscreen(HWND hwnd, HWND hStatus)
{
    if (!hwnd || !IsWindow(hwnd)) return;

    g_fullscreen = !g_fullscreen;
    RDP_LOG_MSG("toggle_fullscreen: fullscreen=%d", g_fullscreen ? 1 : 0);

    if (g_fullscreen) {
        ShowCursor(FALSE);
        if (hStatus && IsWindow(hStatus)) {
            ShowWindow(hStatus, SW_HIDE);
        }
        g_old_menu = GetMenu(hwnd);
        if (g_old_menu) {
            SetMenu(hwnd, NULL);
        }
        g_old_pos.length = sizeof(g_old_pos);
        GetWindowPlacement(hwnd, &g_old_pos);
        g_old_style = GetWindowLongA(hwnd, GWL_STYLE);

        int vs_width = GetSystemMetrics(SM_CXSCREEN);
        int vs_height = GetSystemMetrics(SM_CYSCREEN);
        SetWindowLongA(hwnd, GWL_STYLE, WS_VISIBLE | WS_POPUP);
        SetWindowPos(hwnd, HWND_TOP, 0, 0, vs_width, vs_height, SWP_SHOWWINDOW);
    } else {
        ShowCursor(TRUE);
        if (hStatus && IsWindow(hStatus)) {
            ShowWindow(hStatus, SW_SHOW);
        }
        if (g_old_menu) {
            SetMenu(hwnd, g_old_menu);
            g_old_menu = NULL;
        }
        SetWindowLongA(hwnd, GWL_STYLE, g_old_style);
        g_old_pos.length = sizeof(g_old_pos);
        SetWindowPlacement(hwnd, &g_old_pos);
    }
}

static void detect_rdram_size(void)
{
    g_rdram_size = 0x400000;
    if (g_gfx.RDRAM) {
        // Zilmar's API does not report the RDRAM size. Probe the end of the
        // 8 MiB range instead of relying on the size of the first VM region;
        // a valid allocation may be split into multiple adjacent regions.
        const void *probe = g_gfx.RDRAM + 0x7f0000;
        constexpr size_t probe_size = 16;
        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery(probe, &mbi, sizeof(mbi)) != 0 &&
            mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0 &&
            mbi.RegionSize >= probe_size) {
            const uintptr_t region_begin = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            const uintptr_t region_end = region_begin + mbi.RegionSize;
            const uintptr_t probe_begin = reinterpret_cast<uintptr_t>(probe);
            if (probe_begin >= region_begin && probe_begin <= region_end - probe_size)
                g_rdram_size = 0x800000;
        }
    }
    RDP_LOG_MSG("detect_rdram_size: rdram_size=0x%x (%u MB)", g_rdram_size, g_rdram_size / (1024 * 1024));
}

static void acknowledge_dp_list(void)
{
    if (g_gfx.DPC_END_REG) {
        if (g_gfx.DPC_START_REG) *g_gfx.DPC_START_REG = *g_gfx.DPC_END_REG;
        if (g_gfx.DPC_CURRENT_REG) *g_gfx.DPC_CURRENT_REG = *g_gfx.DPC_END_REG;
    }
    if (g_gfx.MI_INTR_REG) {
        *g_gfx.MI_INTR_REG |= DP_INTERRUPT;
    }
    if (g_gfx.CheckInterrupts) {
        g_gfx.CheckInterrupts();
    }
}

extern "C" {

PJ64_EXPORT void PJ64_CALL GetDllInfo(PLUGIN_INFO *plugin_info)
{
    if (!plugin_info) return;
    plugin_info->Version = PLUGIN_VERSION;
    plugin_info->Type = PLUGIN_TYPE_GFX;
    strncpy(plugin_info->Name, "paraLLEl-RDP Vulkan Rasterizer", sizeof(plugin_info->Name) - 1);
    plugin_info->Name[sizeof(plugin_info->Name) - 1] = '\0';
    plugin_info->NormalMemory = TRUE;
    plugin_info->MemoryBswaped = TRUE;
}

PJ64_EXPORT BOOL PJ64_CALL InitiateGFX(GFX_INFO gfx_info)
{
    g_gfx = gfx_info;
    config_load_or_create();
    rdp_log_init();

    RDP_LOG_MSG("InitiateGFX: hWnd=%p hStatusBar=%p RDRAM=%p DMEM=%p Swapped=%d",
                gfx_info.hWnd, gfx_info.hStatusBar, gfx_info.RDRAM, gfx_info.DMEM, gfx_info.MemoryBswaped);

    return TRUE;
}

PJ64_EXPORT void PJ64_CALL RomClosed(void)
{
    if (!g_rom_open) return;
    RDP_LOG_MSG("RomClosed");
    g_rom_open = false;

    // PJ64 may close and reopen the graphics runtime while loading a state.
    // Preserve the window mode across that lifecycle transition.
    if (g_renderer) {
        g_renderer->destroy();
        g_renderer.reset();
    }

    cmd_cur = 0;
    cmd_ptr = 0;
    g_dlist_calls = 0;
    g_rdplist_calls = 0;
}

PJ64_EXPORT void PJ64_CALL CloseDLL(void)
{
    RDP_LOG_MSG("CloseDLL");
    RomClosed();
    memset(&g_gfx, 0, sizeof(g_gfx));
    rdp_log_close();
}

PJ64_EXPORT void PJ64_CALL RomOpen(void)
{
    if (g_rom_open) {
        RomClosed();
    }
    config_load_or_create();
    rdp_log_init();

    RDP_LOG_MSG("RomOpen: hWnd=%p", g_gfx.hWnd);

    ensure_window_size(g_gfx.hWnd, g_gfx.hStatusBar);
    detect_rdram_size();

    cmd_cur = 0;
    cmd_ptr = 0;
    g_dlist_calls = 0;
    g_rdplist_calls = 0;

    g_renderer = std::make_unique<VulkanRenderer>();
    if (!g_renderer->init(g_gfx, g_rdram_size)) {
        RDP_LOG_MSG("RomOpen: VulkanRenderer::init failed!");
        MessageBoxA(g_gfx.hWnd,
                    "paraLLEl-RDP failed to initialize Vulkan.\n\n"
                    "Please check that your GPU driver supports Vulkan 1.1 with VK_KHR_8bit_storage.",
                    "paraLLEl-RDP Error",
                    MB_OK | MB_ICONERROR);
        g_renderer.reset();
        return;
    }

    g_rom_open = true;
    RDP_LOG_MSG("RomOpen: initialized successfully");
}

static void process_commands(void)
{
    if (!g_renderer || !g_renderer->is_running()) {
        acknowledge_dp_list();
        return;
    }

    if (!g_gfx.DPC_CURRENT_REG || !g_gfx.DPC_END_REG)
        return;

    const uint32_t dp_current = *g_gfx.DPC_CURRENT_REG & 0x00FFFFF8;
    const uint32_t dp_end = *g_gfx.DPC_END_REG & 0x00FFFFF8;

    int length = static_cast<int>(dp_end - dp_current);
    if (length <= 0)
        return;

    length = static_cast<unsigned>(length) >> 3;
    if ((cmd_ptr + length) & ~(0x0003FFFF >> 3))
        return;

    uint32_t offset = dp_current;
    if (g_gfx.DPC_STATUS_REG && (*g_gfx.DPC_STATUS_REG & DP_STATUS_XBUS_DMA)) {
        if (!g_gfx.DMEM) return;
        do {
            offset &= 0xFF8;
            cmd_data[2 * cmd_ptr + 0] = *reinterpret_cast<const uint32_t *>(g_gfx.DMEM + offset);
            cmd_data[2 * cmd_ptr + 1] = *reinterpret_cast<const uint32_t *>(g_gfx.DMEM + offset + 4);
            offset += sizeof(uint64_t);
            cmd_ptr++;
        } while (--length > 0);
    } else {
        if (!g_gfx.RDRAM) return;
        if (dp_end > 0x7ffffff || dp_current > 0x7ffffff)
            return;

        do {
            offset &= 0xFFFFF8;
            cmd_data[2 * cmd_ptr + 0] = *reinterpret_cast<const uint32_t *>(g_gfx.RDRAM + offset);
            cmd_data[2 * cmd_ptr + 1] = *reinterpret_cast<const uint32_t *>(g_gfx.RDRAM + offset + 4);
            offset += sizeof(uint64_t);
            cmd_ptr++;
        } while (--length > 0);
    }

    while (cmd_cur < cmd_ptr) {
        uint32_t w1 = cmd_data[2 * cmd_cur];
        uint32_t command = (w1 >> 24) & 63;
        int cmd_length = cmd_len_lut[command];

        if (cmd_ptr - cmd_cur < cmd_length) {
            if (g_gfx.DPC_START_REG) *g_gfx.DPC_START_REG = *g_gfx.DPC_END_REG;
            if (g_gfx.DPC_CURRENT_REG) *g_gfx.DPC_CURRENT_REG = *g_gfx.DPC_END_REG;
            return;
        }

        if (command >= 8 && g_renderer) {
            g_renderer->enqueue_commands(&cmd_data[2 * cmd_cur], cmd_length * 2);
        }

        if (command == static_cast<uint32_t>(RDP::Op::SyncFull)) {
            RDP_LOG_MSG("SyncFull reached (cmd_cur=%d, len=%d)", cmd_cur, cmd_length);
            if (g_renderer) {
                g_renderer->sync_full();
            }
            if (g_gfx.MI_INTR_REG) {
                *g_gfx.MI_INTR_REG |= DP_INTERRUPT;
            }
            if (g_gfx.CheckInterrupts) {
                g_gfx.CheckInterrupts();
            }
        }

        cmd_cur += cmd_length;
    }

    cmd_ptr = 0;
    cmd_cur = 0;
    if (g_gfx.DPC_START_REG) *g_gfx.DPC_START_REG = *g_gfx.DPC_END_REG;
    if (g_gfx.DPC_CURRENT_REG) *g_gfx.DPC_CURRENT_REG = *g_gfx.DPC_END_REG;
}

PJ64_EXPORT void PJ64_CALL ProcessDList(void)
{
    g_dlist_calls++;
    RDP_LOG_MSG("ProcessDList (#%u) called. Note: If game graphics do not render, disable Graphic HLE in RSP settings.", g_dlist_calls);
}

PJ64_EXPORT void PJ64_CALL ProcessRDPList(void)
{
    g_rdplist_calls++;
    process_commands();
}

PJ64_EXPORT void PJ64_CALL UpdateScreen(void)
{
    if (!g_rom_open || !g_renderer || !g_renderer->is_running()) return;
    ensure_window_size(g_gfx.hWnd, g_gfx.hStatusBar);
    g_renderer->render_frame(g_gfx);
}

PJ64_EXPORT void PJ64_CALL ShowCFB(void)
{
    // No-op: presentation is owned exclusively by UpdateScreen.
}

PJ64_EXPORT void PJ64_CALL ChangeWindow(void)
{
    toggle_fullscreen(g_gfx.hWnd, g_gfx.hStatusBar);
}

PJ64_EXPORT void PJ64_CALL DrawScreen(void)
{
}

PJ64_EXPORT void PJ64_CALL ReadScreen(void **dest, long *width, long *height)
{
    if (dest) {
        *dest = NULL;
    }
    if (width) {
        *width = 0;
    }
    if (height) {
        *height = 0;
    }
}

PJ64_EXPORT void PJ64_CALL ViStatusChanged(void)
{
}

PJ64_EXPORT void PJ64_CALL ViWidthChanged(void)
{
}

PJ64_EXPORT void PJ64_CALL MoveScreen(int xpos, int ypos)
{
    (void)xpos;
    (void)ypos;
}

PJ64_EXPORT void PJ64_CALL FBWrite(DWORD addr, DWORD size)
{
    (void)addr;
    (void)size;
}

PJ64_EXPORT void PJ64_CALL FBWList(FrameBufferModifyEntry *plist, DWORD size)
{
    (void)plist;
    (void)size;
}

PJ64_EXPORT void PJ64_CALL FBRead(DWORD addr)
{
    (void)addr;
}

PJ64_EXPORT void PJ64_CALL FBGetFrameBufferInfo(void *pinfo)
{
    (void)pinfo;
}

PJ64_EXPORT void PJ64_CALL CaptureScreen(char *directory)
{
    (void)directory;
}

PJ64_EXPORT void PJ64_CALL DllAbout(HWND hwnd)
{
    MessageBoxA(hwnd,
        "paraLLEl-RDP Vulkan Rasterizer\n"
        "Vulkan Compute RDP plugin for Project64.\n\n"
        "Core: Themaister's paraLLEl-RDP\n"
        "Plugin Port by PixelMechanic0",
        "About paraLLEl-RDP Vulkan Rasterizer",
        MB_OK | MB_ICONINFORMATION);
}

PJ64_EXPORT void PJ64_CALL DllConfig(HWND hwnd)
{
    MessageBoxA(hwnd,
        "Configuration is managed via 'parallel-rdp.ini' located next to this DLL.\n\n"
        "Options include upscaling (1x, 2x, 4x, 8x), VSync, deinterlacing, VI filters, and widescreen",
        "paraLLEl-RDP Configuration",
        MB_OK | MB_ICONINFORMATION);
}

PJ64_EXPORT void PJ64_CALL DllTest(HWND hwnd)
{
    (void)hwnd;
}

} // extern "C"
