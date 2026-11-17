/* ng_input_script.c - see ng_input_script.h. Host-only, no ROM data. */
#include "ng_input_script.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char* name;
    int kind;                    /* 1 P1 pad, 2 P2 pad, 3.. system */
    uint8_t bit;
} button_map_t;

static const button_map_t k_buttons[] = {
    {"up", 1, 0x01}, {"down", 1, 0x02}, {"left", 1, 0x04}, {"right", 1, 0x08},
    {"a", 1, 0x10}, {"b", 1, 0x20}, {"c", 1, 0x40}, {"d", 1, 0x80},
    {"p2up", 2, 0x01}, {"p2down", 2, 0x02}, {"p2left", 2, 0x04}, {"p2right", 2, 0x08},
    {"p2a", 2, 0x10}, {"p2b", 2, 0x20}, {"p2c", 2, 0x40}, {"p2d", 2, 0x80},
    {"start", 3, 0}, {"start2", 4, 0}, {"select", 5, 0},
    {"coin", 6, 0}, {"coin2", 7, 0}, {"service", 8, 0}, {"test", 9, 0},
};

static int apply_button(const char* name, ng_input_state_t* in) {
    for (size_t i = 0; i < sizeof(k_buttons) / sizeof(k_buttons[0]); i++) {
        if (strcmp(name, k_buttons[i].name)) continue;
        switch (k_buttons[i].kind) {
        case 1: in->p1 |= k_buttons[i].bit; break;
        case 2: in->p2 |= k_buttons[i].bit; break;
        case 3: in->start1 = 1; break;
        case 4: in->start2 = 1; break;
        case 5: in->select1 = 1; break;
        case 6: in->coin1 = 1; break;
        case 7: in->coin2 = 1; break;
        case 8: in->service = 1; break;
        case 9: in->test = 1; break;
        default: return -1;
        }
        return 0;
    }
    return -1;
}

static int fail(char* err, size_t err_size, const char* path, int line, const char* what,
                const char* detail) {
    if (err && err_size)
        snprintf(err, err_size, "%s:%d: %s%s%s", path, line, what,
                 detail ? " '" : "", detail ? detail : "");
    if (err && err_size && detail) {
        size_t n = strlen(err);
        if (n + 2 < err_size) { err[n] = '\''; err[n + 1] = 0; }
    }
    return -1;
}

static char* trim(char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
    return s;
}

int ng_input_script_load(const char* path, ng_input_event_t* events, int max_events,
                         char* err, size_t err_size) {
    FILE* f = fopen(path, "r");
    if (!f) {
        if (err && err_size) snprintf(err, err_size, "%s: cannot open", path);
        return -1;
    }
    int count = 0, lineno = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char* hash = strchr(line, '#');
        if (hash) *hash = 0;
        char* text = trim(line);
        if (!*text) continue;

        char* spec = text;
        char* rest = spec;
        while (*rest && !isspace((unsigned char)*rest)) rest++;
        if (*rest) *rest++ = 0;
        rest = trim(rest);
        if (!*rest) {
            fclose(f);
            return fail(err, err_size, path, lineno, "missing buttons", NULL);
        }

        errno = 0;
        char* endp = NULL;
        long start = strtol(spec, &endp, 10);
        if (errno || endp == spec || start < 0) {
            fclose(f);
            return fail(err, err_size, path, lineno, "bad frame", spec);
        }
        long end = start;
        if (*endp == '-') {
            const char* es = endp + 1;
            errno = 0;
            end = strtol(es, &endp, 10);
            if (errno || endp == es || end < start) {
                fclose(f);
                return fail(err, err_size, path, lineno, "bad frame range", spec);
            }
        }
        while (*endp && isspace((unsigned char)*endp)) endp++;
        if (*endp) {
            fclose(f);
            return fail(err, err_size, path, lineno, "bad frame spec", spec);
        }
        if (start == end) end = start + 5;   /* single press holds 6 frames */

        if (count >= max_events) {
            fclose(f);
            return fail(err, err_size, path, lineno, "too many events", NULL);
        }
        ng_input_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.start = start;
        ev.end = end;

        for (char* tok = rest; tok && *tok; ) {
            char* plus = strchr(tok, '+');
            if (plus) *plus = 0;
            if (!*tok) {
                fclose(f);
                return fail(err, err_size, path, lineno, "empty button", NULL);
            }
            if (apply_button(tok, &ev.in)) {
                fclose(f);
                return fail(err, err_size, path, lineno, "unknown button", tok);
            }
            tok = plus ? plus + 1 : NULL;
        }
        events[count++] = ev;
    }
    if (ferror(f)) {
        fclose(f);
        return fail(err, err_size, path, lineno, "read error", NULL);
    }
    fclose(f);
    return count;
}

void ng_input_script_apply(const ng_input_event_t* events, int count, long frame,
                           ng_input_state_t* out) {
    for (int i = 0; i < count; i++) {
        const ng_input_event_t* e = &events[i];
        if (frame < e->start || frame > e->end) continue;
        out->p1 |= e->in.p1;
        out->p2 |= e->in.p2;
        out->start1 |= e->in.start1;
        out->start2 |= e->in.start2;
        out->select1 |= e->in.select1;
        out->select2 |= e->in.select2;
        out->coin1 |= e->in.coin1;
        out->coin2 |= e->in.coin2;
        out->service |= e->in.service;
        out->test |= e->in.test;
    }
}
