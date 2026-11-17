#ifndef NG_SHIM_STDIO_H
#define NG_SHIM_STDIO_H

#include <stdarg.h>
#include <stddef.h>

typedef struct _ng_FILE _ng_FILE;
typedef _ng_FILE FILE;
extern FILE* stderr;
extern FILE* stdout;
extern FILE* stdin;

int printf(const char* fmt, ...);
int fprintf(FILE* f, const char* fmt, ...);
int sprintf(char* s, const char* fmt, ...);
int snprintf(char* s, size_t n, const char* fmt, ...);
int vfprintf(FILE* f, const char* fmt, va_list ap);
int vsprintf(char* s, const char* fmt, va_list ap);
int vprintf(const char* fmt, va_list ap);
int puts(const char* s);
int fputc(int c, FILE* f);
int fputs(const char* s, FILE* f);
int putchar(int c);
FILE* fopen(const char* path, const char* mode);
int fclose(FILE* f);
size_t fread(void* p, size_t sz, size_t n, FILE* f);
size_t fwrite(const void* p, size_t sz, size_t n, FILE* f);
int fseek(FILE* f, long off, int whence);
long ftell(FILE* f);
int feof(FILE* f);
int fflush(FILE* f);
int remove(const char* path);

#endif