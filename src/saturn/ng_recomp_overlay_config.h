/* ng_recomp_overlay_config.h - geometry for the Saturn game-overlay arena.
 *
 * The game recomp (local/ng_recomp_game64/) is split into 64 pages of ~46-92
 * KB compiled each. The arena holds a small resident budget of pages in
 * WRAM-H; the loader (ng_overlay_saturn.c) reads a page's bytes from the
 * disc-staged PAGES.BIN on demand.
 *
 * The arena slots are fixed-size (the maximum page), since the linker needs a
 * static layout; a page's actual byte count is carried in the page map
 * (ng_recomp_page_info) and the loader reads only that many bytes.
 */
#ifndef NG_RECOMP_OVERLAY_CONFIG_H
#define NG_RECOMP_OVERLAY_CONFIG_H

/* Resident page slots in the arena. The app uses ~816 KB of the 983 KB
 * budget (359 KB image + 457 KB BSS), leaving ~167 KB; at ~96 KB max page
 * that is 1 slot. Start with 1 (no eviction) and raise once the app trims. */
/* Resident page slots. 1024-page recomp gives ~16-42 KB pages; a 4-slot
 * direct-mapped arena at 49152 B each = 192 KiB, same WRAM-L total as before
 * but 4x more resident pages, cutting disc thrashing. */
#define NG_RECOMP_ARENA_PAGES 4u

/* 1024-page recomp (local/ng_recomp_game1024/) pages are 16-42 KB; 49152
 * (48 KiB) covers them. Arena = 4 * 49152 = 192 KiB. */
#define NG_RECOMP_PAGE_BYTES 49152u

#endif