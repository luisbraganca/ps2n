// Host implementations that can replace guest functions.
//
// PS2_STUB(name, summary)
//
// The recompiler reads this list (it includes this file at build time) to
// validate stub bindings in a game config, and binds ELF symbols with these
// names automatically when [stubs].auto is on. Each entry is implemented in
// runtime/src/stubs/*.cpp as ps2::stub::<name>(), reading arguments from
// $a0-$a3/$t0-$t3 (EE EABI) and returning in $v0.

// Generic triage handlers, usable on any address.
PS2_STUB(ret0, "return 0")
PS2_STUB(ret1, "return 1")
PS2_STUB(retm1, "return -1")
PS2_STUB(nop, "return without touching $v0")

// C library. Replacing these is optional (the recompiled versions work), but
// printf-style output is far more useful on the host console.
PS2_STUB(printf, "int printf(const char *fmt, ...) -> host stdout")
PS2_STUB(puts, "int puts(const char *s) -> host stdout")
PS2_STUB(putchar, "int putchar(int c) -> host stdout")
PS2_STUB(sprintf, "int sprintf(char *buf, const char *fmt, ...)")
PS2_STUB(snprintf, "int snprintf(char *buf, size_t n, const char *fmt, ...)")
PS2_STUB(memcpy, "void *memcpy(void *dst, const void *src, size_t n)")
PS2_STUB(memmove, "void *memmove(void *dst, const void *src, size_t n)")
PS2_STUB(memset, "void *memset(void *dst, int c, size_t n)")
PS2_STUB(memcmp, "int memcmp(const void *a, const void *b, size_t n)")
PS2_STUB(strlen, "size_t strlen(const char *s)")
PS2_STUB(strcpy, "char *strcpy(char *dst, const char *src)")
PS2_STUB(strncpy, "char *strncpy(char *dst, const char *src, size_t n)")
PS2_STUB(strcmp, "int strcmp(const char *a, const char *b)")
PS2_STUB(strncmp, "int strncmp(const char *a, const char *b, size_t n)")
PS2_STUB(strcat, "char *strcat(char *dst, const char *src)")
PS2_STUB(strchr, "char *strchr(const char *s, int c)")
PS2_STUB(strrchr, "char *strrchr(const char *s, int c)")

// Sony / ps2sdk library entry points with a host implementation.
PS2_STUB(sceSifLoadModule, "int sceSifLoadModule(const char *path, int argLen, const char *args) -> HLE IOP module load")
PS2_STUB(SifLoadModule, "int SifLoadModule(const char *path, int argLen, const char *args) -> HLE IOP module load")
PS2_STUB(scePrintf, "int scePrintf(const char *fmt, ...) -> host stdout")
