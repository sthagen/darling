#define __SYSCALL_32BIT_ARG_BYTES 8
#include "SYS.h"

#ifndef SYS_fstatfs
#error "SYS_fstatfs not defined. The header files libsyscall is building against do not match syscalls.master."
#endif

#if defined(__i386__) || defined(__x86_64__) || defined(__ppc__)
__SYSCALL2(_fstatfs, fstatfs, 2, cerror_nocancel)
#else
__SYSCALL2(___fstatfs, fstatfs, 2, cerror_nocancel)
#endif

