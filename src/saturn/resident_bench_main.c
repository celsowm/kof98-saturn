/* resident_bench_main.c - fully-resident native speed bench.
 *
 * Loads the real AES BIOS and KOF98 P1/P2 cart, then runs the guest flat out.
 * Hot routines are linked directly into the app (no disc paging). No renderer:
 * WRAM-H is fully used by the resident native blob. Emulator (Ymir) only.
 */
#include <saturn/core.h>
#include <saturn/video.h>
#include <saturn/cd_mount.h>
#include <saturn/cdfs.h>
#include <saturn/ram_cart.h>

#include "ng_boot.h"

static sat_cd_block_t g_block;
static sat_cd_device_t g_device;
static sat_cdfs_volume_t g_volume;
static sat_ram_cart_buffer_t g_cart;

static uint8_t g_bios_raw[0x20000] __attribute__((section(".wram_l")));
static uint8_t g_chunk[0x8000] __attribute__((section(".wram_l")));

#define P1_BYTES 0x100000u
#define P2_BYTES 0x300000u

static void swap16(uint8_t* p, uint32_t n) {
    for (uint32_t i = 0; i + 1 < n; i += 2) { uint8_t t = p[i]; p[i] = p[i + 1]; p[i + 1] = t; }
}
static void fail(int line) { (void)line; sat_panic(SAT_ERR_INVALID_ARG, __FILE__, line); }
static sat_result_t load_file(const char* path, uint8_t* dst, uint32_t bytes, uint32_t* got) {
    sat_cdfs_file_t f;
    SAT_TRY(sat_cdfs_lookup(&g_volume, path, &f));
    return sat_cdfs_read_at(&g_volume, &f, 0, dst, bytes, got);
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

int main(void) {
    const sat_video_config_t cfg = {320, 224, SAT_VIDEO_NTSC, SAT_VIDEO_INTERLACE_NONE};
    if (sat_init(&cfg) != SAT_OK) fail(__LINE__);
    if (sat_vdp1_set_frame_mode(SAT_VDP1_FRAME_ON_SUBMIT) != SAT_OK) fail(__LINE__);
    if (sat_set_clear_color(0x0000u) != SAT_OK) fail(__LINE__);
    if (sat_cd_mount_default(&g_block, 0, 0, &g_device, 0, &g_volume) != SAT_OK) fail(__LINE__);

    sat_ram_cart_info_t info;
    if (sat_ram_cart_init() != SAT_OK) fail(__LINE__);
    if (sat_ram_cart_info(&info) != SAT_OK) fail(__LINE__);
    if (info.type != SAT_RAM_CART_4MB || info.capacity != 0x400000u || info.bank_count != 2)
        sat_panic(SAT_ERR_UNSUPPORTED, __FILE__, __LINE__);
    if (sat_ram_cart_buffer_alloc(0x400000u, 32, &g_cart) != SAT_OK) fail(__LINE__);
    if (load_to_cart("P1.BIN", 0, P1_BYTES) != SAT_OK) fail(__LINE__);
    if (load_to_cart("P2.BIN", P1_BYTES, P2_BYTES) != SAT_OK) fail(__LINE__);

    uint32_t got = 0;
    if (load_file("BIOS.BIN", g_bios_raw, sizeof(g_bios_raw), &got) != SAT_OK ||
        got != sizeof(g_bios_raw)) fail(__LINE__);
    swap16(g_bios_raw, sizeof(g_bios_raw));

    ng_boot_set_cart((const uint8_t*)g_cart.bank[0], g_cart.bank_bytes[0],
                     (const uint8_t*)g_cart.bank[1], g_cart.bank_bytes[1]);
    ng_boot_init(g_bios_raw, sizeof(g_bios_raw));

    for (;;) ng_boot_run_frames(1);
}
