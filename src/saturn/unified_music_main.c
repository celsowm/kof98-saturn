/* unified_music_main.c - P5 experiment: pruned native guest + gated renderer +
 * PRE-RENDERED game audio (MUSIC.BIN SCSP stream), no live YM2610 synthesis.
 *
 * PORTING_PLAN Phase F permits a hybrid driver approach instead of full YM2610
 * software emulation. MUSIC.BIN is generated offline by the pinned oracle
 * (tools/convert_music.py, real KOF98 Z80+YM2610 output). This variant tests
 * whether removing the ~52% SH-2 audio-synthesis cost lets the single build
 * meet the ~1.0x P5 speed gate with renderer + game audio still present.
 *
 * The 4 MiB cart stays data-only; all native code is WRAM-H resident.
 * Emulator evidence only. Local build artifact; no game-derived code here.
 */
#include <saturn/core.h>
#include <saturn/video.h>
#include <saturn/audio.h>
#include <saturn/cd_mount.h>
#include <saturn/cdfs.h>
#include <saturn/disc_stream.h>
#include <saturn/ram_cart.h>

#include "ng_boot.h"
#include "kof98/ng_live_strips.h"
#include "kof98/asset_store.h"
#include "kof98/tile_cache.h"
#include "kof98/saturn_tile_cache.h"
#include "kof98/saturn_strip_renderer.h"
#include "tile_manifest.h"
#include "kof_music_assets.h"

#define MUSIC_DISC_PATH "MUSIC.BIN"
#define P1_BYTES 0x100000u
#define P2_BYTES 0x300000u
#define CART_BYTES 0x400000u
#define RENDER_FRAME 778u
#define NG_DISC_CYCLES 7670u   /* ~10 ms between drive services */

static sat_cd_block_t g_block;
static sat_cd_device_t g_device;
static sat_cdfs_volume_t g_volume;
static sat_disc_stream_t g_disc;
static sat_disc_music_rings_t g_rings;
static sat_ram_cart_buffer_t g_cart;

static uint8_t g_bios_raw[0x20000] __attribute__((section(".wram_l")));
static uint8_t g_chunk[0x8000] __attribute__((section(".wram_l")));

static uint8_t g_zoom[0x10000];
static uint8_t g_tile_src[0x10000];
static uint8_t g_tile_arena[0x10000];
static uint32_t g_tile_src_size;
static kof_asset_store g_store;
static kof_tile_view g_view;
static kof_saturn_tile_cache g_tiles;
static kof_saturn_strip_renderer g_renderer;
static uint8_t g_scratch[2048];

/* Probe counters. */
volatile uint32_t kof_live_sprite_count;
volatile uint32_t kof_live_sprite_hash;
volatile uint32_t kof_live_draw_strips;
volatile uint32_t kof_live_draw_native;
volatile uint32_t kof_live_draw_variants;
volatile uint32_t kof_live_tile_uploads;
volatile uint32_t kof_live_draw_error;
volatile uint32_t kof_music_frames;
volatile uint32_t kof_music_services;
volatile uint32_t kof_music_playing;
volatile uint32_t kof_music_underruns;
volatile uint32_t kof_music_refills;
volatile uint32_t kof_music_buffered;
volatile uint32_t kof_music_min_buffered;
volatile uint32_t kof_music_bytes;
volatile uint32_t kof_music_last_error;

static void swap16(uint8_t* p, uint32_t n) {
    for (uint32_t i = 0; i + 1 < n; i += 2) { uint8_t t = p[i]; p[i] = p[i + 1]; p[i + 1] = t; }
}
static void fail(int line) { (void)line; sat_panic(SAT_ERR_INVALID_ARG, __FILE__, line); }

static sat_result_t load_file(const char* path, uint8_t* dst, uint32_t bytes, uint32_t* got) {
    sat_cdfs_file_t f;
    SAT_TRY(sat_cdfs_lookup(&g_volume, path, &f));
    return sat_cdfs_read_at(&g_volume, &f, 0, dst, bytes, got);
}
static void ng_copy(void* dst, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dst; const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}
static int src_read(void* ctx, uint32_t off, void* dst, uint32_t n) {
    (void)ctx; ng_copy(dst, g_tile_src + off, n); return 0;
}
static int arena_read(void* ctx, uint32_t off, void* dst, uint32_t n) {
    (void)ctx; ng_copy(dst, g_tile_arena + off, n); return 0;
}
static int arena_write(void* ctx, uint32_t off, const void* src, uint32_t n) {
    (void)ctx; ng_copy(g_tile_arena + off, src, n); return 0;
}
static sat_result_t load_to_cart(const char* path, uint32_t dst, uint32_t total) {
    sat_cdfs_file_t f;
    SAT_TRY(sat_cdfs_lookup(&g_volume, path, &f));
    if (f.directory || f.size < total) return SAT_ERR_INVALID_ARG;
    for (uint32_t done = 0; done < total; ) {
        uint32_t n = total - done;
        if (n > sizeof(g_chunk)) n = sizeof(g_chunk);
        uint32_t got = 0;
        SAT_TRY(sat_cdfs_read_at(&g_volume, &f, done, g_chunk, n, &got));
        if (got != n) return SAT_ERR_INVALID_ARG;
        swap16(g_chunk, n);
        SAT_TRY(sat_ram_cart_buffer_write_at(&g_cart, dst + done, g_chunk, n));
        done += n;
    }
    return SAT_OK;
}

static void music_stats_refresh(void) {
    uint32_t underruns = 0u, refills = 0u, buffered = 0u;
    sat_disc_stream_music_stats(&g_disc, &underruns, &refills, &buffered);
    kof_music_services = g_disc.stats.music_segments;
    kof_music_underruns = underruns;
    kof_music_refills = refills;
    kof_music_buffered = buffered;
    kof_music_min_buffered = g_disc.stats.music_min_buffered;
    kof_music_bytes = g_disc.stats.music_bytes;
    kof_music_playing = sat_disc_stream_music_playing(&g_disc) ? 1u : 0u;
}

/* Music service: disc scheduler gated (~10 ms) + SCSP stream update, from the
 * core slice hook so audio is serviced at field cadence inside the guest frame. */
static void music_slice(void) {
    const uint32_t now = ng_boot_cycles_lo();
    static uint32_t s_last_disc;
#ifndef KOF_MUSIC_NO_DISC_SERVICE
    if ((uint32_t)(now - s_last_disc) >= NG_DISC_CYCLES) {
        s_last_disc = now;
        if (sat_disc_stream_service(&g_disc) != SAT_OK)
            kof_music_last_error = (uint32_t)g_disc.stats.last_error;
    }
#endif
#ifndef KOF_MUSIC_NO_AUDIO_UPDATE
    if (sat_audio_update() != SAT_OK) fail(__LINE__);
#endif
    music_stats_refresh();
}

int main(void) {
    const sat_video_config_t cfg = {320, 224, SAT_VIDEO_NTSC, SAT_VIDEO_INTERLACE_NONE};
    if (sat_init(&cfg) != SAT_OK) fail(__LINE__);
    if (sat_vdp1_set_frame_mode(SAT_VDP1_FRAME_ON_SUBMIT) != SAT_OK) fail(__LINE__);
    if (sat_set_clear_color(0x0000u) != SAT_OK) fail(__LINE__);
    if (sat_audio_init() != SAT_OK) fail(__LINE__);
    if (sat_cd_mount_default(&g_block, 0, 0, &g_device, 0, &g_volume) != SAT_OK) fail(__LINE__);
    if (sat_disc_stream_init(&g_disc, &g_block) != SAT_OK) fail(__LINE__);

    sat_ram_cart_info_t info;
    if (sat_ram_cart_init() != SAT_OK) fail(__LINE__);
    if (sat_ram_cart_info(&info) != SAT_OK) fail(__LINE__);
    if (info.type != SAT_RAM_CART_4MB || info.capacity != CART_BYTES || info.bank_count != 2)
        sat_panic(SAT_ERR_UNSUPPORTED, __FILE__, __LINE__);
    if (sat_ram_cart_buffer_alloc(CART_BYTES, 32, &g_cart) != SAT_OK) fail(__LINE__);
    if (load_to_cart("P1.BIN", 0, P1_BYTES) != SAT_OK) fail(__LINE__);
    if (load_to_cart("P2.BIN", P1_BYTES, P2_BYTES) != SAT_OK) fail(__LINE__);

    uint32_t got = 0;
    if (load_file("BIOS.BIN", g_bios_raw, sizeof(g_bios_raw), &got) != SAT_OK ||
        got != sizeof(g_bios_raw)) fail(__LINE__);
    swap16(g_bios_raw, sizeof(g_bios_raw));
    if (load_file("L0.BIN", g_zoom, sizeof(g_zoom), &got) != SAT_OK || got < sizeof(g_zoom)) fail(__LINE__);
    {
        sat_cdfs_file_t f;
        if (sat_cdfs_lookup(&g_volume, "TILES.BIN", &f) != SAT_OK) fail(__LINE__);
        if (sat_cdfs_read_at(&g_volume, &f, 0, g_tile_src, f.size, &got) != SAT_OK || got != f.size) fail(__LINE__);
        g_tile_src_size = f.size;
    }

    if (kof_asset_init(&g_store, sizeof(g_tile_arena), g_tile_src_size, src_read, 0,
                       arena_read, arena_write, 0) != KOF_ASSET_OK) fail(__LINE__);
    kof_asset_result ar;
    if (kof_asset_begin(&g_store, kof_local_tile_assets, 2u) != KOF_ASSET_OK) fail(__LINE__);
    do { ar = kof_asset_step(&g_store, g_scratch, sizeof(g_scratch)); } while (ar == KOF_ASSET_PENDING);
    if (ar != KOF_ASSET_OK) fail(__LINE__);
    if (kof_tile_view_open(&g_view, &g_store) != KOF_ASSET_OK) fail(__LINE__);
    if (kof_asset_enter_combat(&g_store) != KOF_ASSET_OK) fail(__LINE__);
    if (kof_saturn_tile_cache_init(&g_tiles) != SAT_OK) fail(__LINE__);
    if (kof_saturn_strip_renderer_init(&g_renderer, &g_tiles) != SAT_OK) fail(__LINE__);

    ng_boot_set_cart((const uint8_t*)g_cart.bank[0], g_cart.bank_bytes[0],
                     (const uint8_t*)g_cart.bank[1], g_cart.bank_bytes[1]);
    ng_boot_init(g_bios_raw, sizeof(g_bios_raw));

#ifndef KOF_MUSIC_DISABLE
    /* Pre-rendered soundtrack: one CDFS lookup, then the single disc scheduler
     * owns the drive for the rest of the run. */
    sat_cdfs_file_t music_file;
    if (sat_cdfs_lookup(&g_volume, MUSIC_DISC_PATH, &music_file) != SAT_OK) fail(__LINE__);
    if (music_file.size != KOF_MUSIC_BYTES) fail(__LINE__);
    if (sat_disc_stream_music_start(&g_disc, &g_rings, &music_file, KOF_MUSIC_BYTES,
                                    KOF_MUSIC_SAMPLE_RATE, 2u) != SAT_OK) fail(__LINE__);
    ng_boot_set_slice_hook(music_slice, 4000u);
#endif

    int rendered = 0;
    for (;;) {
        if (ng_boot_frames + 2u >= RENDER_FRAME && !rendered) {
            if (sat_begin_frame() != SAT_OK) fail(__LINE__);
        }
        ng_boot_run_frames(1);

        uint32_t n = 0, h = 0;
        if (ng_boot_frames + 2u >= RENDER_FRAME) {
            n = ng_live_sprite_strips(ng_boot_vram(), g_zoom, ng_boot_pal(),
                                      ng_boot_anim(), ng_boot_fix_bios(), &h);
            kof_live_sprite_count = n;
            kof_live_sprite_hash = (n == 0xFFFFFFFFu) ? 0u : h;
        }
        if (ng_boot_frames == RENDER_FRAME && n != 0xFFFFFFFFu) {
            kof_emit_stats stats;
            sat_result_t dr = kof_saturn_strip_draw(&g_renderer, ng_live_builder(), &g_view, &stats);
            kof_live_draw_error = (uint32_t)dr;
            if (dr == SAT_OK) {
                kof_live_draw_strips = ng_live_builder()->count;
                kof_live_draw_native = stats.native;
                kof_live_draw_variants = stats.variants;
                kof_live_tile_uploads = g_tiles.cache.tile_misses + g_tiles.cache.lut_misses;
            }
            (void)kof_tile_cache_end(&g_tiles.cache);
            if (dr == SAT_OK) rendered = 1;
        }
        if (ng_boot_frames + 2u >= RENDER_FRAME && !rendered) {
            if (sat_end_frame() != SAT_OK) fail(__LINE__);
        }
        ++kof_music_frames;
    }
}
