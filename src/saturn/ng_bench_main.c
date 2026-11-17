/* ng_bench_main.c - BIOS-only Neo Geo 68000 speed bench for Saturn.
 *
 * Loads the real AES system BIOS (BIOS.BIN) and runs it on the compact Neo Geo
 * bus in src/ng/ng_boot.c, flat out with no vblank wait, so the number of guest
 * frames completed per emulated Saturn frame is a direct measure of how fast the
 * SH-2 executes the 68000 program.
 *
 * Built twice from the same source so the only difference is the core:
 *   kof_ng_bench_interp  - Musashi interpreter (ng_boot.c, no recomp)
 *   kof_ng_bench_recomp  - native recomp, no interpreter (NG_BOOT_RECOMP_ONLY)
 * Both stage only BIOS.BIN, so the workload is identical. Emulator evidence
 * only: this is Ymir, not physical-console timing.
 */
#include <saturn/core.h>
#include <saturn/video.h>
#include <saturn/cd_mount.h>
#include <saturn/cdfs.h>

#include "ng_boot.h"

static sat_cd_block_t g_block;
static sat_cd_device_t g_device;
static sat_cdfs_volume_t g_volume;

static uint8_t g_bios_raw[0x20000] __attribute__((section(".wram_l")));
static uint8_t g_chunk[0x10000] __attribute__((section(".wram_l")));

static void swap16(uint8_t* p, uint32_t n) {
    for (uint32_t i = 0; i + 1 < n; i += 2) { uint8_t t = p[i]; p[i] = p[i + 1]; p[i + 1] = t; }
}

static void fail(int line) { (void)line; sat_panic(SAT_ERR_INVALID_ARG, __FILE__, line); }

int main(void) {
    const sat_video_config_t cfg = {320, 224, SAT_VIDEO_NTSC, SAT_VIDEO_INTERLACE_NONE};
    if (sat_init(&cfg) != SAT_OK) fail(__LINE__);
    if (sat_vdp1_set_frame_mode(SAT_VDP1_FRAME_ON_SUBMIT) != SAT_OK) fail(__LINE__);
    if (sat_set_clear_color(0x0000u) != SAT_OK) fail(__LINE__);
    if (sat_cd_mount_default(&g_block, 0, 0, &g_device, 0, &g_volume) != SAT_OK) fail(__LINE__);

    sat_cdfs_file_t f;
    if (sat_cdfs_lookup(&g_volume, "BIOS.BIN", &f) != SAT_OK) fail(__LINE__);
    if (f.directory || f.size < sizeof(g_bios_raw)) fail(__LINE__);
    uint32_t got = 0;
    if (sat_cdfs_read_at(&g_volume, &f, 0, g_bios_raw, sizeof(g_bios_raw), &got) != SAT_OK) fail(__LINE__);
    if (got != sizeof(g_bios_raw)) fail(__LINE__);
    swap16(g_bios_raw, sizeof(g_bios_raw));

    ng_boot_init(g_bios_raw, sizeof(g_bios_raw));

    /* Flat out: one guest frame at a time, no vblank wait. The emulator's frame
     * clock decides how many guest frames fit, so ng_boot_frames / emulated
     * frames is the speed ratio (1.0 = real time). */
    for (;;) {
        ng_boot_run_frames(1);
    }
}