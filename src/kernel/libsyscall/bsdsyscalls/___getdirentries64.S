#define __SYSCALL_32BIT_ARG_BYTES 16
#include "SYS.h"

#ifndef SYS_getdirentries64
#error "SYS_getdirentries64 not defined. The header files libsyscall is building against do not match syscalls.master."
#endif

#if defined(__arm64__)
MI_ENTRY_POINT(___getdirentries64)
ZERO_EXTEND(2)
SYSCALL_NONAME(getdirentries64, 4, cerror_nocancel)
ret
#else
#if defined(__i386__) || defined(__x86_64__) || defined(__ppc__) || defined(__arm__) || defined(__arm64__)
__SYSCALL2(___getdirentries64, getdirentries64, 4, cerror_nocancel)
#endif

#endif

