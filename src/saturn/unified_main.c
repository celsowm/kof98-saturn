/* unified_main.c - one build: native real-time guest (pruned RECOMP_ONLY core)
 * + gated LSPC-to-VDP1 renderer + live Z80/YM2610 audio.
 *
 * Flat-out speed-bench structure (like ng_rawcode_bench_main.c): guest frames
 * advance as fast as the SH-2 can execute them, so guest_frames/emulated_fields
 * is the speed ratio. VDP1 frame setup/strip decode are gated to RENDER_FRAME
 * so the display path is exercised without taxing every frame. Live audio is
 * serviced from the core's slice hook (field cadence) and fed to one SCSP mono
 * stream. The 4 MiB cart remains data-only (P1/P2/BIOS/L0/tiles/M1/SM1).
 *
 * Emulator evidence only. Local build artifact; no game-derived code here.
 */
#include <saturn/core.h>
#include <saturn/video.h>
#include <saturn/audio.h>
#include <saturn/cd_mount.h>
#include <saturn/cdfs.h>
#include <saturn/ram_cart.h>
#ifdef KOF_PAD_INPUT
#include <saturn/input.h>
#endif

#include "ng_boot.h"
#include "kof98/ng_live_strips.h"
#include "kof98/asset_store.h"
#include "kof98/tile_cache.h"
#include "kof98/saturn_tile_cache.h"
#include "kof98/saturn_strip_renderer.h"
#include "tile_manifest.h"

extern int audio_init(const uint8_t* m1, size_t m1_size, const uint8_t* sm1,
                      const uint8_t* const* vroms, const size_t* vsizes, int nvrom);
extern size_t audio_take_samples(int16_t* out, size_t max_frames);

#define P1_BYTES 0x100000u
#define P2_BYTES 0x300000u
#define CART_BYTES 0x400000u
#define RENDER_FRAME 778u

static sat_cd_block_t g_block;
static sat_cd_device_t g_device;
static sat_cdfs_volume_t g_volume;
static sat_ram_cart_buffer_t g_cart;

static uint8_t g_bios_raw[0x20000] __attribute__((section(".wram_l")));
static uint8_t g_chunk[0x8000] __attribute__((section(".wram_l")));
static uint8_t g_sm1[0x20000] __attribute__((section(".wram_l")));
static int16_t g_ym_frames[1024] __attribute__((section(".wram_l")));
static int16_t g_ym_ring[4096] __attribute__((section(".wram_l")));
static sat_audio_stream_t g_ym_stream;

/* Renderer state in WRAM-H (.bss). */
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
volatile uint32_t kof_ym_samples;
volatile uint32_t kof_ym_written;
volatile uint32_t kof_ym_step;

/* Diagnostic: scan a range of guest frames for nonzero LSPC sprite counts.
 * Off by default; KOF_SPRITE_SCAN=1 enables it. The scan reuses the validated
 * ng_live_sprite_strips decoder at a coarse cadence so it does not distort
 * the guest timeline. */
#ifdef KOF_SPRITE_SCAN
volatile uint32_t kof_scan_first_frame;   /* first frame with sprites (>0) */
volatile uint32_t kof_scan_max_count;     /* largest sprite count observed */
volatile uint32_t kof_scan_max_frame;     /* frame of the largest count */
volatile uint32_t kof_scan_hits;          /* number of sampled frames with sprites */
volatile uint32_t kof_scan_samples;       /* number of frames sampled */
#endif

static void swap16(uint8_t* p, uint32_t n) {
    for (uint32_t i = 0; i + 1 < n; i += 2) { uint8_t t = p[i]; p[i] = p[i + 1]; p[i + 1] = t; }
}
static void fail(int line) { (void)line; sat_panic(SAT_ERR_INVALID_ARG, __FILE__, line); }

#ifdef KOF_PAD_INPUT
volatile uint32_t kof_pad_calls;
volatile uint32_t kof_pad_nonzero;
/* Map the Saturn pad to the Neo Geo active-high pad byte (ng_boot.h bit order:
 * 0 up, 1 down, 2 left, 3 right, 4 A, 5 B, 6 C, 7 D). Start and coin are
 * separate latches; Z is used for coin so a Ymir --pad-script tape can drive
 * the game into match/fight states. */
static void feed_pad(void) {
    sat_pad_state_t st;
    kof_pad_calls++;
    if (sat_pad_poll(&st) != SAT_OK) return;
    uint16_t h = st.held;
    if (h) kof_pad_nonzero++;
    uint8_t p = 0;
    if (h & SAT_PAD_UP)    p |= 1u << 0;
    if (h & SAT_PAD_DOWN)  p |= 1u << 1;
    if (h & SAT_PAD_LEFT)  p |= 1u << 2;
    if (h & SAT_PAD_RIGHT) p |= 1u << 3;
    if (h & SAT_PAD_A)     p |= 1u << 4;
    if (h & SAT_PAD_B)     p |= 1u << 5;
    if (h & SAT_PAD_C)     p |= 1u << 6;
    if (h & SAT_PAD_START) p |= 1u << 7;
    ng_boot_set_p1(p);
    ng_boot_set_start((h & SAT_PAD_START) ? 1 : 0);
    ng_boot_set_coin1((h & SAT_PAD_Z) ? 1 : 0);
}
#endif

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

/* Live-audio service: called from the core slice hook at field cadence. */
static void audio_slice(void) {
    int16_t stereo[2048];
    size_t n = audio_take_samples(stereo, 1024);
    for (size_t i = 0; i < n; i++)
        g_ym_frames[i] = (int16_t)((stereo[i * 2] + stereo[i * 2 + 1]) >> 1);
    kof_ym_samples += (uint32_t)n;
    if (n && sat_audio_stream_write(g_ym_stream, g_ym_frames, (uint32_t)n) == SAT_OK)
        kof_ym_written += (uint32_t)n;
    if (sat_audio_update() != SAT_OK) fail(__LINE__);
}

int main(void) {
    const sat_video_config_t cfg = {320, 224, SAT_VIDEO_NTSC, SAT_VIDEO_INTERLACE_NONE};
    if (sat_init(&cfg) != SAT_OK) fail(__LINE__);
    if (sat_vdp1_set_frame_mode(SAT_VDP1_FRAME_ON_SUBMIT) != SAT_OK) fail(__LINE__);
    if (sat_set_clear_color(0x0000u) != SAT_OK) fail(__LINE__);
    if (sat_audio_init() != SAT_OK) fail(__LINE__);
    if (sat_cd_mount_default(&g_block, 0, 0, &g_device, 0, &g_volume) != SAT_OK) fail(__LINE__);

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

    /* Live game audio: SM1 BIOS driver + game M1 ROM, one SCSP mono stream. */
    if (load_file("SM1.BIN", g_sm1, sizeof(g_sm1), &got) != SAT_OK || got != sizeof(g_sm1)) fail(__LINE__);
    kof_ym_step = 1;
    if (load_file("M1.BIN", g_bios_raw, sizeof(g_bios_raw), &got) != SAT_OK || got != sizeof(g_bios_raw)) fail(__LINE__);
    if (audio_init(g_bios_raw, sizeof(g_bios_raw), g_sm1, 0, 0, 0) != 0) fail(__LINE__);
    kof_ym_step = 2;
    {
        sat_audio_spec_t spec = { 55555u, 4096u, 1u, SAT_AUDIO_PCM_S16, 0u };
        if (sat_audio_stream_open(&g_ym_stream, &spec, g_ym_ring, sizeof(g_ym_ring)) != SAT_OK) fail(__LINE__);
    }
    kof_ym_step = 3;
    ng_boot_set_slice_hook(audio_slice, 4000u);

    int rendered = 0;
    for (;;) {
        /* Keep the guest advancing after the one-time render: the unified
         * build is a real-time target, not a stop-at-frame bench. VDP1 frame
         * setup / strip decode remain gated to the render window; once drawn,
         * the last submitted VDP1 list stays displayed while the guest runs. */
        if (ng_boot_frames + 2u >= RENDER_FRAME && !rendered) {
            if (sat_begin_frame() != SAT_OK) fail(__LINE__);
        }
#ifdef KOF_PAD_INPUT
        feed_pad();
#endif
        ng_boot_run_frames(1);

#ifdef KOF_SPRITE_SCAN
        /* Sample every 16th guest frame once past the boot window. */
        if (ng_boot_frames >= 128u && (ng_boot_frames & 15u) == 0u) {
            uint32_t hs = 0;
            uint32_t sn = ng_live_sprite_strips(ng_boot_vram(), g_zoom, ng_boot_pal(),
                                               ng_boot_anim(), ng_boot_fix_bios(), &hs);
            kof_scan_samples++;
            if (sn != 0u && sn != 0xFFFFFFFFu) {
                if (kof_scan_hits == 0u) kof_scan_first_frame = ng_boot_frames;
                kof_scan_hits++;
                if (sn > kof_scan_max_count) {
                    kof_scan_max_count = sn;
                    kof_scan_max_frame = ng_boot_frames;
                }
            }
        }
#endif

        uint32_t n = 0, h = 0;
        /* Strip decoding walks all 224 LSPC lines (~32% of the SH-2 profile).
         * The result is only consumed by the single RENDER_FRAME draw, so once
         * that draw has succeeded stop decoding every subsequent frame. */
        if (!rendered && ng_boot_frames + 2u >= RENDER_FRAME) {
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
    }
}
