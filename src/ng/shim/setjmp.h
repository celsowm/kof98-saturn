#ifndef NG_SHIM_SETJMP_H
#define NG_SHIM_SETJMP_H

/* The 68000 configuration does not use setjmp/longjmp (address-error and trace
 * traps are off), so only the include needs to exist. */
typedef int jmp_buf[1];
typedef int sigjmp_buf[1];
int setjmp(jmp_buf);
int sigsetjmp(sigjmp_buf, int);
void longjmp(jmp_buf, int);
void siglongjmp(sigjmp_buf, int);

#endif