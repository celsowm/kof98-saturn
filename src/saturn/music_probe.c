/* kof_music - real KOF98 soundtrack streamed from CD through LibSaturn's SCSP
 * music path.
 *
 * The audio is not synthesized: it is a PCM16 excerpt of the actual game music
 * produced by the pinned KOF98 oracle (Z80 + YM2610 emulation running the real
 * 68000 program) and converted to raw interleaved S16BE by tools/convert_music.py.
 *
 * This target uses the built-in disc music feed (sat_disc_stream_music_start):
 * the CD Block reads the track in 16 KiB segments in the background, splits it
 * into two mono SCSP rings and the SCSP driver plays them. It does not stream
 * game assets, so there is no contention with a renderer; it is the isolated
 * "does the real soundtrack play on Saturn" slice.
 *
 * Counters (read from the ELF/map by scripts/kof_ymir_music.py):
 *   kof_music_frames          display fields serviced
 *   kof_music_services        disc scheduler service calls
 *   kof_music_playing         1 once the rings filled and the streams started
 *   kof_music_underruns       SCSP underruns (worse channel)
 *   kof_music_refills         ring refills
 *   kof_music_buffered        current ring fill (frames, limiting channel)
 *   kof_music_min_buffered    lowest fill seen while playing
 *   kof_music_bytes           music payload bytes delivered from CD
 *   kof_music_last_error      first scheduler/CD error (SAT_OK == none)
 */
#include <stdint.h>

#include <saturn/core.h>
#include <saturn/video.h>
#include <saturn/audio.h>
#include <saturn/cd_block.h>
#include <saturn/cd_mount.h>
#include <saturn/cdfs.h>
#include <saturn/disc_stream.h>

#include "kof_music_assets.h"

#define MUSIC_DISC_PATH "MUSIC.BIN"

static sat_cd_block_t g_block;
static sat_cd_device_t g_device;
static sat_cdfs_volume_t g_volume;
static sat_disc_stream_t g_disc;
static sat_disc_music_rings_t g_rings;

/* Read from the ELF/map after an emulated run. */
volatile uint32_t kof_music_frames;
volatile uint32_t kof_music_services;
volatile uint32_t kof_music_playing;
volatile uint32_t kof_music_underruns;
volatile uint32_t kof_music_refills;
volatile uint32_t kof_music_buffered;
volatile uint32_t kof_music_min_buffered;
volatile uint32_t kof_music_bytes;
volatile uint32_t kof_music_last_error;

static void fail(int line) {
    sat_panic((sat_result_t)(0x7000u + (uint32_t)line), __FILE__, line);
}

int main(void) {
    const sat_video_config_t cfg = {320, 224, SAT_VIDEO_NTSC, SAT_VIDEO_INTERLACE_NONE};
    if (sat_init(&cfg) != SAT_OK) fail(__LINE__);
    if (sat_vdp1_set_frame_mode(SAT_VDP1_FRAME_ON_SUBMIT) != SAT_OK) fail(__LINE__);
    if (sat_set_clear_color(0x1084u) != SAT_OK) fail(__LINE__);
    if (sat_audio_init() != SAT_OK) fail(__LINE__);

    /* Mount CD, then the disc scheduler that owns the drive. */
    if (sat_cd_mount_default(&g_block, 0, 0, &g_device, 0, &g_volume) != SAT_OK) fail(__LINE__);
    if (sat_disc_stream_init(&g_disc, &g_block) != SAT_OK) fail(__LINE__);

    /* Look the track up before any read is in flight: a lookup reads the disc. */
    sat_cdfs_file_t file;
    if (sat_cdfs_lookup(&g_volume, MUSIC_DISC_PATH, &file) != SAT_OK) fail(__LINE__);
    if (file.size != KOF_MUSIC_BYTES) fail(__LINE__);

    if (sat_disc_stream_music_start(&g_disc, &g_rings, &file, KOF_MUSIC_BYTES,
                                    KOF_MUSIC_SAMPLE_RATE, 2u) != SAT_OK) fail(__LINE__);

    for (;;) {
        if (sat_begin_frame() != SAT_OK) fail(__LINE__);

        /* Move landed sectors into the rings; start the next segment. */
        if (sat_disc_stream_service(&g_disc) != SAT_OK) {
            kof_music_last_error = (uint32_t)g_disc.stats.last_error;
        }
        /* SCSP: push the newest half of the rings to the sound hardware. */
        if (sat_audio_update() != SAT_OK) fail(__LINE__);

        if (sat_end_frame() != SAT_OK) fail(__LINE__);
        if (sat_wait_vblank() != SAT_OK) fail(__LINE__);

        uint32_t underruns = 0u, refills = 0u, buffered = 0u;
        sat_disc_stream_music_stats(&g_disc, &underruns, &refills, &buffered);
        kof_music_services = g_disc.stats.music_segments;
        kof_music_underruns = underruns;
        kof_music_refills = refills;
        kof_music_buffered = buffered;
        kof_music_min_buffered = g_disc.stats.music_min_buffered;
        kof_music_bytes = g_disc.stats.music_bytes;
        kof_music_playing = sat_disc_stream_music_playing(&g_disc) ? 1u : 0u;
        if (g_block.progress_error_count != 0u)
            kof_music_last_error = (uint32_t)g_block.progress_last_error;
        ++kof_music_frames;
    }
}