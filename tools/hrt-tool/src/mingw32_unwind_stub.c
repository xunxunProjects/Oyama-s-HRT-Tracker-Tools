/*
 * The 32-bit MinGW toolchain used for the Windows/386 release is built with
 * SJLJ exceptions. libgcc provides the SJLJ entry points but not the two
 * DWARF resume/raise functions that Rust's i686-pc-windows-gnu std
 * references. The printer is built with panic=abort and has no unwinding
 * path, so make those two entry points explicit aborts.
 *
 * Only these two symbols are defined: the PE assembler folds the extra
 * leading underscore, so defining the other __Unwind_* names collides with
 * libgcc's unwind-sjlj.o.
 */
#if defined(_WIN32) && !defined(__x86_64__)
__asm__(
    ".text\n"
    ".globl _Unwind_Resume\n"
    "_Unwind_Resume:\n"
    "  call _abort\n"
    ".globl __Unwind_Resume\n"
    "__Unwind_Resume:\n"
    "  call _abort\n"
    ".globl _Unwind_RaiseException\n"
    "_Unwind_RaiseException:\n"
    "  call _abort\n"
    ".globl __Unwind_RaiseException\n"
    "__Unwind_RaiseException:\n"
    "  call _abort\n");
#endif
