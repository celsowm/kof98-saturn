#ifndef KOF98_NATIVE_PROFILE_H
#define KOF98_NATIVE_PROFILE_H
#include <stdint.h>
/* Host only: NULL path disables profiling. Never link into Saturn runtime. */
void kof_native_profile_init(const char *path, const char *identity);
int kof_native_profile_enabled(void);
void kof_native_profile_record(uint32_t pc, unsigned bank, uint64_t instructions, uint64_t cycles);
void kof_native_profile_finish(void);
#endif
