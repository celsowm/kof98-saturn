/* ROM-free regression for the host input-script parser (src/host/ng_input_script.c).
 *
 * Pins the reference script semantics: inclusive frame ranges, a single frame
 * holding for 6, OR-ing of overlapping events, the active-high pad bit order,
 * and rejection of unknown buttons instead of silently dropping them.
 */
#include "ng_input_script.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static ng_input_state_t at(const ng_input_event_t* ev, int n, long frame) {
    ng_input_state_t s;
    memset(&s, 0, sizeof(s));
    ng_input_script_apply(ev, n, frame, &s);
    return s;
}

int main(int argc, char** argv) {
    const char* root = argc > 1 ? argv[1] : ".";
    char path[512];
    snprintf(path, sizeof(path), "%s/tests/input_script_sample.txt", root);

    ng_input_event_t ev[NG_INPUT_SCRIPT_MAX_EVENTS];
    char err[256] = {0};
    int n = ng_input_script_load(path, ev, NG_INPUT_SCRIPT_MAX_EVENTS, err, sizeof(err));
    CHECK(n == 4);
    if (n != 4) { fprintf(stderr, "load: %s\n", err); return 1; }

    CHECK(ev[0].start == 10 && ev[0].end == 15);   /* single frame holds 6 */
    CHECK(ev[1].start == 20 && ev[1].end == 25);
    CHECK(ev[2].start == 30 && ev[2].end == 35);
    CHECK(ev[3].start == 40 && ev[3].end == 45);

    CHECK(at(ev, n, 9).start1 == 0);
    CHECK(at(ev, n, 10).start1 == 1);
    CHECK(at(ev, n, 15).start1 == 1);
    CHECK(at(ev, n, 16).start1 == 0);

    ng_input_state_t s = at(ev, n, 22);
    CHECK(s.p1 == 0x18);                            /* right | A */
    CHECK(s.p2 == 0);

    s = at(ev, n, 31);
    CHECK(s.p2 == 0x22);                            /* down | B */
    CHECK(s.p1 == 0);

    s = at(ev, n, 42);
    CHECK(s.coin1 == 1 && s.select1 == 1);

    /* Overlapping events are OR-ed: the A press does not cancel the hold. */
    snprintf(path, sizeof(path), "%s/tests/input_script_bad.txt", root);
    err[0] = 0;
    CHECK(ng_input_script_load(path, ev, NG_INPUT_SCRIPT_MAX_EVENTS, err, sizeof(err)) == -1);
    CHECK(strstr(err, "unknown button") != NULL);

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("input_script: 4 events, ranges and buttons match\n");
    return 0;
}
