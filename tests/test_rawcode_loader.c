/* host test: C rawcode loader reproduces the Python-patched reference. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kof98/rawcode_loader.h"

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s PAGES.BIN RELOCS.BIN\n", argv[0]); return 2; }
    FILE* fp = fopen(argv[1], "rb");
    if (!fp) return 2;
    fseek(fp, 0, SEEK_END); long n = ftell(fp); fseek(fp, 0, SEEK_SET);
    uint8_t* pages = malloc(n); fread(pages, 1, n, fp); fclose(fp);
    fp = fopen(argv[2], "rb");
    if (!fp) { free(pages); return 2; }
    fseek(fp, 0, SEEK_END); long rn = ftell(fp); fseek(fp, 0, SEEK_SET);
    uint8_t* relocs = malloc(rn); fread(relocs, 1, rn, fp); fclose(fp);

    /* Load page 0 into a fresh buffer (simulate disc load at BASE). */
    const uint32_t BASE = 0x06000000u;
    const uint32_t STRIDE = 196608u;
    uint8_t* page = malloc(STRIDE);
    memset(page, 0, STRIDE);
    memcpy(page, pages, STRIDE);  /* page 0 blob = first stride bytes */
    uint32_t patched = rawcode_patch_page(page, relocs, 0, BASE);

    fp = fopen("local/ymir/rawverify/patched.bin", "rb");
    if (!fp) { fprintf(stderr, "no reference\n"); return 2; }
    fseek(fp, 0, SEEK_END); long refn = ftell(fp); fseek(fp, 0, SEEK_SET);
    uint8_t* ref = malloc(refn); fread(ref, 1, refn, fp); fclose(fp);
    int diff = 0, shown = 0;
    for (long i = 0; i < refn && i < STRIDE; i++) if (page[i] != ref[i]) diff++;
    for (long i = 0; i < refn && i < STRIDE && shown < 5; i += 4) {
        if (page[i] != ref[i]) {
            printf("  off=%ld c=%02x ref=%02x\n", i, page[i], ref[i]);
            shown++;
        }
    }
    printf("[rawloader] slots=%u ref=%ld diffbytes=%d equal=%s\n",
           patched, refn, diff, diff == 0 ? "yes" : "no");
    free(pages); free(relocs); free(page); free(ref);
    return diff == 0 ? 0 : 1;
}
