#include "kof98/dispatch.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "line %d: %s\n", __LINE__, #expr); return EXIT_FAILURE; } } while (0)
static void fixed(void) {}
static void bank0(void) {}
static void bank1(void) {}
static void bank3(void) {}
static void bios(void) {}

int main(void)
{
    const kof_dispatch_entry t[] = {
        {0x00000000, fixed}, {0x000ffffe, fixed}, {0x00200400, bank0},
        {0x00c01234, bios}, {0x01200400, bank1}, {0x03200400, bank3},
    };
    const size_t n = sizeof(t)/sizeof(t[0]);
    CHECK(kof_dispatch_validate(t,n));
    CHECK(kof_dispatch_validate(NULL,0));
    CHECK(!kof_dispatch_validate(NULL,1));
    CHECK(kof_dispatch_lookup(t,n,0,3) == fixed);
    CHECK(kof_dispatch_lookup(t,n,0xfffff,0) == fixed);
    CHECK(kof_dispatch_lookup(t,n,0x200400,0) == bank0);
    CHECK(kof_dispatch_lookup(t,n,0x200401,1) == bank1);
    CHECK(kof_dispatch_lookup(t,n,0x200400,2) == NULL);
    CHECK(kof_dispatch_lookup(t,n,0x200400,3) == bank3);
    CHECK(kof_dispatch_lookup(t,n,0x200400,4) == NULL);
    CHECK(kof_dispatch_lookup(t,n,0xc21234,3) == bios);
    CHECK(kof_dispatch_lookup(t,n,0xcf1235,0) == NULL);
    CHECK(kof_dispatch_lookup(t,n,0xabc01234,0) == bios);
    CHECK(kof_dispatch_lookup(t,n,0x100000,0) == NULL);
    CHECK(kof_dispatch_lookup(t,n,0x200402,3) == NULL);
    CHECK(kof_dispatch_lookup(NULL,0,0,0) == NULL);
    CHECK(kof_dispatch_lookup(t,0,0,0) == NULL);
    const kof_dispatch_entry duplicate[] = {{0,fixed},{0,bank0}};
    const kof_dispatch_entry unsorted[] = {{2,fixed},{0,bank0}};
    const kof_dispatch_entry odd[] = {{1,fixed}};
    const kof_dispatch_entry nullfn[] = {{0,NULL}};
    const kof_dispatch_entry invalid[] = {{0x04200000,bank0}};
    const kof_dispatch_entry mirror[] = {{0xc21234,bios}};
    CHECK(!kof_dispatch_validate(duplicate,2));
    CHECK(!kof_dispatch_validate(unsorted,2));
    CHECK(!kof_dispatch_validate(odd,1));
    CHECK(!kof_dispatch_validate(nullfn,1));
    CHECK(!kof_dispatch_validate(invalid,1));
    CHECK(!kof_dispatch_validate(mirror,1));
    puts("dispatch tests passed");
    return EXIT_SUCCESS;
}
