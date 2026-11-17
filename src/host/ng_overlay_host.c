/* ng_overlay_host.c - host platform hook for recomp routine overlay/streaming.
 *
 * The core (ng_boot.c) owns the paging policy: which page is resident, when to
 * fault one in, which to evict. This file is the platform half, and on the host
 * the page bytes are already linked in place, so "loading" a page only records
 * that the page was brought in and hands back success.
 *
 * On Saturn the same hook copies the page's linked bytes from the disc into the
 * page's fixed arena address; the core code does not change.
 */
#include <stdint.h>

/* Bookkeeping so a test can check the hook really ran. */
unsigned ng_overlay_host_load_calls;
unsigned ng_overlay_host_last_page;
unsigned ng_overlay_host_page_loads[256];

int ng_overlay_loader(unsigned page) {
    if (page >= 256u) return -1;
    ng_overlay_host_load_calls++;
    ng_overlay_host_last_page = page;
    ng_overlay_host_page_loads[page]++;
    return 0;
}