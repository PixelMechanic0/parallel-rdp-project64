#ifndef PARALLEL_RDP_CONFIG_HPP
#define PARALLEL_RDP_CONFIG_HPP

#include <stdint.h>
#include <stdbool.h>

struct RDPConfig {
    uint32_t window_width;       // Default window client width (default 800)
    uint32_t window_height;      // Default window client height (default 600)
    uint32_t upscaling;          // 1, 2, 4, 8 (default 4)
    bool synchronous;            // Wait on OpSyncFull (default true)
    bool ss_dither;              // Super-sampled dither (default false)
    bool ss_readbacks;           // Super-sampled readbacks
    bool native_texture_lod;     // Native texture LOD quirk
    bool native_tex_rect;        // Native resolution tex rect quirk
    bool divot_filter;           // VI divot filter
    bool gamma_dither;           // VI gamma dither
    bool vi_aa;                  // VI antialiasing
    bool vi_scale;               // VI bilinear scaling
    bool dither_filter;          // VI dither filter
    bool interlacing;            // Deinterlacing / interlacing blend
    uint32_t overscan_crop;      // Pixels to crop for overscan (0-32)
    bool vsync;                  // Enable VSync (default false)
    bool widescreen;             // 16:9 stretch vs 4:3 pillarbox
};

extern RDPConfig g_config;

void config_load_or_create(void);

#endif // PARALLEL_RDP_CONFIG_HPP
