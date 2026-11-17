#ifndef SH2_SHIM_STDIO_H
#define SH2_SHIM_STDIO_H
/* Freestanding stubs: ymfm's std::string log_keyon() and audio.c's debug
 * fprintf are never called on Saturn, but the declarations must exist. */
#include <stddef.h>
typedef struct { int unused; } FILE;
extern FILE* stdout;
extern FILE* stderr;
#ifdef __cplusplus
extern "C" {
#endif
int printf(const char* fmt, ...);
int fprintf(FILE* f, const char* fmt, ...);
int sprintf(char* s, const char* fmt, ...);
int snprintf(char* s, size_t n, const char* fmt, ...);
FILE* fopen(const char* path, const char* mode);
int fclose(FILE* f);
size_t fwrite(const void* p, size_t sz, size_t n, FILE* f);
int fputs(const char* s, FILE* f);
#ifdef __cplusplus
}
#endif
#endif
