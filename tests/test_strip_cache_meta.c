/* The live emitter caches resolved pattern/LUT slots inside kof_strip. Those
 * fields are not serialized, so the only correctness requirement on the core
 * builder is exact invalidation: appended rows change the pattern key, while
 * a merged run provably keeps the same raw palette. New strips start
 * unresolved. This test drives the public FIX epoch path (which shares
 * append_run with the sprite paths) without any ROM or Saturn SDK. */
#include "kof98/strip.h"
#include <stdio.h>
#include <string.h>

static kof_strip strips[4096],carry_a[4096],carry_b[4096],carry_c[4096];
static uint16_t v[KOF_LSPC_WORDS];
static uint16_t pal[4096];
static uint16_t versions[256];

static int fail(const char *what, unsigned slot)
{
    printf("strip cache metadata: %s (slot %u)\n", what, slot);
    return 1;
}

static int check_strip(const kof_strip_builder *b, unsigned index,
                       uint8_t height, int resolved, uint32_t palette_epoch)
{
    const kof_strip *s = &b->strips[index];
    if (s->height != height) return fail("unexpected height", index);
    if (resolved) {
        if (!s->pattern_epoch) return fail("pattern epoch not resolved", index);
    } else if (s->pattern_epoch) {
        return fail("stale pattern epoch survived", index);
    }
    if (s->palette_epoch != palette_epoch)
        return fail("unexpected palette epoch", index);
    return 0;
}

int main(void)
{
    kof_strip_builder b;
    memset(v, 0, sizeof v);
    memset(pal, 0, sizeof pal);
    memset(versions, 0, sizeof versions);
    /* Constant tile/palette for lines 0..31 of every FIX column. */
    for (unsigned col = 0; col < 40u; ++col)
        for (unsigned scan = 16u; scan < 48u; scan += 8u)
            v[0x7000u + col * 32u + scan / 8u] =
                (uint16_t)((3u << 12) | (0x40u + col));

    if (kof_strip_begin(&b, strips, 4096u)) return fail("begin", 0);
    /* [0,8): one eight-row strip per column, unresolved. */
    if (kof_strip_push_fix_epoch(&b, v, pal, versions, 0, 8u))
        return fail("first epoch", 0);
    if (b.count != 40u) return fail("first strip count", b.count);
    for (unsigned i = 0; i < 40u; ++i)
        if (check_strip(&b, i, 8u, 0, 0u)) return 1;

    /* Pretend the emitter resolved the first strip. */
    strips[0].pattern_slot = 123u;
    strips[0].pattern_epoch = 7u;
    strips[0].lut_slot = 4u;
    strips[0].palette_epoch = 9u;

    /* [8,16): same tile/palette, so append_run extends strip 0. The pattern
     * key changed (new rows) and must be unresolved; the palette contents
     * cannot have changed by the merge contract. */
    if (kof_strip_push_fix_epoch(&b, v, pal, versions, 0, 16u))
        return fail("second epoch", 0);
    if (b.count != 40u) return fail("second strip count", b.count);
    if (check_strip(&b, 0, 16u, 0, 9u)) return 1;
    if (strips[0].pattern_slot != 123u || strips[0].lut_slot != 4u)
        return fail("slot storage changed", 0);

    /* A different palette starts a new strip; every cache reference must be
     * zero, including the palette epoch that the old strip still owns. */
    for (unsigned col = 0; col < 40u; ++col)
        for (unsigned scan = 48u; scan < 64u; scan += 8u)
            v[0x7000u + col * 32u + scan / 8u] =
                (uint16_t)((5u << 12) | (0x80u + col));
    if (kof_strip_push_fix_epoch(&b, v, pal, versions, 0, 24u))
        return fail("third epoch", 0);
    if (b.count != 80u) return fail("third strip count", b.count);
    for (unsigned i = 40u; i < 80u; ++i)
        if (check_strip(&b, i, 8u, 0, 0u)) return 1;

    /* Cross-frame carry is content-based: identical emitter inputs copy the
     * previous resolution, while one changed tile in one column leaves only
     * that column unresolved. */
    memset(v, 0, sizeof v);
    for (unsigned col = 0; col < 40u; ++col)
        for (unsigned scan = 16u; scan < 48u; scan += 8u)
            v[0x7000u + col * 32u + scan / 8u] =
                (uint16_t)((3u << 12) | (0x40u + col));
    kof_strip_builder ca, cb, cc;
    if (kof_strip_begin(&ca, carry_a, 4096u)) return fail("carry begin a", 0);
    if (kof_strip_push_fix_epoch(&ca, v, pal, versions, 0, 8u))
        return fail("carry frame a", 0);
    for (unsigned i = 0; i < 40u; ++i) {
        carry_a[i].pattern_slot = (uint16_t)(100u + i);
        carry_a[i].pattern_epoch = 200u + i;
        carry_a[i].lut_slot = (uint16_t)(10u + i);
        carry_a[i].palette_epoch = 300u + i;
    }
    if (kof_strip_begin(&cb, carry_b, 4096u)) return fail("carry begin b", 0);
    if (kof_strip_push_fix_epoch(&cb, v, pal, versions, 0, 8u))
        return fail("carry frame b", 0);
    uint8_t clean[KOF_STRIP_SLOTS];
    if (kof_strip_carry_cache(&cb, &ca, clean) != 40u)
        return fail("identical carry count", 0);
    for (unsigned i = 0; i < 40u; ++i)
        if (!clean[i]) return fail("identical slot not clean", i);
    for (unsigned i = 0; i < 40u; ++i)
        if (carry_b[i].pattern_slot != (uint16_t)(100u + i) ||
            carry_b[i].pattern_epoch != 200u + i ||
            carry_b[i].lut_slot != (uint16_t)(10u + i) ||
            carry_b[i].palette_epoch != 300u + i)
            return fail("carried metadata mismatch", i);

    /* Change column 5's tile. Every other column still matches. */
    v[0x7000u + 5u * 32u + 16u / 8u] = (uint16_t)((3u << 12) | 0x7fu);
    if (kof_strip_begin(&cc, carry_c, 4096u)) return fail("carry begin c", 0);
    if (kof_strip_push_fix_epoch(&cc, v, pal, versions, 0, 8u))
        return fail("carry frame c", 0);
    if (kof_strip_carry_cache(&cc, &cb, clean) != 39u)
        return fail("changed carry count", 0);
    if (carry_c[5].pattern_epoch || carry_c[5].palette_epoch)
        return fail("changed column carried", 5);
    if (carry_c[6].pattern_epoch != 206u || carry_c[6].pattern_slot != 106u)
        return fail("later column not carried", 6);
    if (clean[5]) return fail("changed slot marked clean", 5);
    if (!clean[6]) return fail("later slot not clean", 6);

    printf("strip cache metadata: invalidation and carry ok (%zu-byte strip)\n",
           sizeof(kof_strip));
    return 0;
}
