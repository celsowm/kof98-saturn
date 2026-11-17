/* ng_input_script.h - host-side scripted controller input (X1/X2 harness).
 *
 * Uses the same text format as the pinned neogeorecomp reference so its
 * authored test scripts can drive this core unchanged:
 *
 *   # comment
 *   <frame>[-<end>] <button>[+<button>...]
 *
 * Frame numbers are video frames from power-on, inclusive. A single-frame
 * event holds for 6 frames, long enough for the guest to debounce a press.
 * Every event active on a frame is OR-ed together, exactly like the
 * reference. `#` starts a comment.
 *
 * Buttons: up down left right a b c d, p2up p2down p2left p2right p2a p2b p2c
 * p2d, start start2 select coin coin2 service test. Unknown words are an
 * error (the reference treats them as game debug commands; this core has no
 * debug commands, so silently dropping one would hide a broken script).
 *
 * Host-only, no ROM data. The core itself just exposes setters; this module
 * only turns a script into the state for one frame.
 */
#ifndef NG_INPUT_SCRIPT_H
#define NG_INPUT_SCRIPT_H

#include <stddef.h>
#include <stdint.h>

#define NG_INPUT_SCRIPT_MAX_EVENTS 512

/* Active-high controller state, same bit order as ng_input_t:
 * bit 0 up, 1 down, 2 left, 3 right, 4 A, 5 B, 6 C, 7 D. */
typedef struct {
    uint8_t p1, p2;
    uint8_t start1, start2, select1, select2;
    uint8_t coin1, coin2, service, test;
} ng_input_state_t;

typedef struct {
    long start, end;             /* inclusive frame range */
    ng_input_state_t in;
} ng_input_event_t;

/* Parse `path` into `events`. Returns the event count, or -1 with `err`
 * filled. Rejects malformed lines, overflow and unknown buttons. */
int ng_input_script_load(const char* path, ng_input_event_t* events, int max_events,
                         char* err, size_t err_size);

/* OR every event active at `frame` into `out`. The caller starts from zero. */
void ng_input_script_apply(const ng_input_event_t* events, int count, long frame,
                           ng_input_state_t* out);

#endif
