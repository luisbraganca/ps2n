// Portable attribute spellings used by the runtime and by generated code.
#pragma once

#if defined(__GNUC__) || defined(__clang__)
#define PS2_COLD __attribute__((cold, noinline))
#define PS2_NOINLINE __attribute__((noinline))
#define PS2_ALWAYS_INLINE inline __attribute__((always_inline))
#define PS2_PRINTF(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#elif defined(_MSC_VER)
#define PS2_COLD __declspec(noinline)
#define PS2_NOINLINE __declspec(noinline)
#define PS2_ALWAYS_INLINE __forceinline
#define PS2_PRINTF(fmt_idx, args_idx)
#else
#define PS2_COLD
#define PS2_NOINLINE
#define PS2_ALWAYS_INLINE inline
#define PS2_PRINTF(fmt_idx, args_idx)
#endif
