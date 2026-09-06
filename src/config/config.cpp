#include "config.hpp"
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define CONFIG_SECTION "paraLLEl-RDP"

RDPConfig g_config = {
    .window_width = 800,
    .window_height = 600,
    .upscaling = 4,
    .synchronous = true,
    .ss_dither = false,
    .ss_readbacks = false,
    .native_texture_lod = false,
    .native_tex_rect = true,
    .divot_filter = true,
    .gamma_dither = true,
    .vi_aa = true,
    .vi_scale = true,
    .dither_filter = true,
    .interlacing = true,
    .overscan_crop = 0,
    .vsync = false,
    .widescreen = false,
};

static void config_path(char *out, size_t size)
{
    HMODULE module = NULL;
    out[0] = '\0';
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(const void *)&config_path, &module))
        return;
    const DWORD length = GetModuleFileNameA(module, out, (DWORD)size);
    if (length == 0u || length >= size) {
        out[0] = '\0';
        return;
    }
    char *const slash = strrchr(out, '\\');
    if (!slash) {
        out[0] = '\0';
        return;
    }
    slash[1] = '\0';
    if (strlen(out) + sizeof("parallel-rdp.ini") > size) {
        out[0] = '\0';
        return;
    }
    strcat(out, "parallel-rdp.ini");
}

static uint32_t clamp_upscaling(uint32_t val)
{
    if (val >= 8) return 8;
    if (val >= 4) return 4;
    if (val >= 2) return 2;
    return 1;
}

void config_load_or_create(void)
{
    char path[MAX_PATH];
    config_path(path, sizeof(path));
    if (path[0] == '\0') return;

    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        char buf[32];
        sprintf(buf, "%u", g_config.window_width);
        WritePrivateProfileStringA(CONFIG_SECTION, "WindowWidth", buf, path);
        sprintf(buf, "%u", g_config.window_height);
        WritePrivateProfileStringA(CONFIG_SECTION, "WindowHeight", buf, path);
        sprintf(buf, "%u", g_config.upscaling);
        WritePrivateProfileStringA(CONFIG_SECTION, "Upscaling", buf, path);
        sprintf(buf, "%u", g_config.synchronous ? 1 : 0);
        WritePrivateProfileStringA(CONFIG_SECTION, "Synchronous", buf, path);
        sprintf(buf, "%u", g_config.ss_dither ? 1 : 0);
        WritePrivateProfileStringA(CONFIG_SECTION, "SuperSampleDither", buf, path);
        sprintf(buf, "%u", g_config.ss_readbacks ? 1 : 0);
        WritePrivateProfileStringA(CONFIG_SECTION, "SuperSampleReadbacks", buf, path);
        sprintf(buf, "%u", g_config.native_texture_lod ? 1 : 0);
        WritePrivateProfileStringA(CONFIG_SECTION, "NativeTextureLOD", buf, path);
        sprintf(buf, "%u", g_config.native_tex_rect ? 1 : 0);
        WritePrivateProfileStringA(CONFIG_SECTION, "NativeTexRect", buf, path);
        sprintf(buf, "%u", g_config.divot_filter ? 1 : 0);
        WritePrivateProfileStringA(CONFIG_SECTION, "DivotFilter", buf, path);
        sprintf(buf, "%u", g_config.gamma_dither ? 1 : 0);
        WritePrivateProfileStringA(CONFIG_SECTION, "GammaDither", buf, path);
        sprintf(buf, "%u", g_config.vi_aa ? 1 : 0);
        WritePrivateProfileStringA(CONFIG_SECTION, "ViAA", buf, path);
        sprintf(buf, "%u", g_config.vi_scale ? 1 : 0);
        WritePrivateProfileStringA(CONFIG_SECTION, "ViScale", buf, path);
        sprintf(buf, "%u", g_config.dither_filter ? 1 : 0);
        WritePrivateProfileStringA(CONFIG_SECTION, "DitherFilter", buf, path);
        sprintf(buf, "%u", g_config.interlacing ? 1 : 0);
        WritePrivateProfileStringA(CONFIG_SECTION, "Interlacing", buf, path);
        sprintf(buf, "%u", g_config.overscan_crop);
        WritePrivateProfileStringA(CONFIG_SECTION, "OverscanCrop", buf, path);
        sprintf(buf, "%u", g_config.vsync ? 1 : 0);
        WritePrivateProfileStringA(CONFIG_SECTION, "VSync", buf, path);
        sprintf(buf, "%u", g_config.widescreen ? 1 : 0);
        WritePrivateProfileStringA(CONFIG_SECTION, "Widescreen", buf, path);
    }

    g_config.window_width = (uint32_t)GetPrivateProfileIntA(CONFIG_SECTION, "WindowWidth", 800, path);
    g_config.window_height = (uint32_t)GetPrivateProfileIntA(CONFIG_SECTION, "WindowHeight", 600, path);
    if (g_config.window_width < 320) g_config.window_width = 320;
    if (g_config.window_height < 240) g_config.window_height = 240;

    g_config.upscaling = clamp_upscaling(GetPrivateProfileIntA(CONFIG_SECTION, "Upscaling", 4, path));
    g_config.synchronous = GetPrivateProfileIntA(CONFIG_SECTION, "Synchronous", 1, path) != 0;
    g_config.ss_dither = GetPrivateProfileIntA(CONFIG_SECTION, "SuperSampleDither", 0, path) != 0;
    g_config.ss_readbacks = GetPrivateProfileIntA(CONFIG_SECTION, "SuperSampleReadbacks", 0, path) != 0;
    g_config.native_texture_lod = GetPrivateProfileIntA(CONFIG_SECTION, "NativeTextureLOD", 0, path) != 0;
    g_config.native_tex_rect = GetPrivateProfileIntA(CONFIG_SECTION, "NativeTexRect", 1, path) != 0;
    g_config.divot_filter = GetPrivateProfileIntA(CONFIG_SECTION, "DivotFilter", 1, path) != 0;
    g_config.gamma_dither = GetPrivateProfileIntA(CONFIG_SECTION, "GammaDither", 1, path) != 0;
    g_config.vi_aa = GetPrivateProfileIntA(CONFIG_SECTION, "ViAA", 1, path) != 0;
    g_config.vi_scale = GetPrivateProfileIntA(CONFIG_SECTION, "ViScale", 1, path) != 0;
    g_config.dither_filter = GetPrivateProfileIntA(CONFIG_SECTION, "DitherFilter", 1, path) != 0;
    g_config.interlacing = GetPrivateProfileIntA(CONFIG_SECTION, "Interlacing", 1, path) != 0;
    g_config.overscan_crop = (uint32_t)GetPrivateProfileIntA(CONFIG_SECTION, "OverscanCrop", 0, path);
    if (g_config.overscan_crop > 64) g_config.overscan_crop = 64;
    g_config.vsync = GetPrivateProfileIntA(CONFIG_SECTION, "VSync", 0, path) != 0;
    g_config.widescreen = GetPrivateProfileIntA(CONFIG_SECTION, "Widescreen", 0, path) != 0;
}
