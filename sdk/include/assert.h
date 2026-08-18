#ifndef KUROGANE_LIBC_ASSERT_H
#define KUROGANE_LIBC_ASSERT_H
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC system_header
#endif

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include_next <assert.h>
#else

#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#if defined(__GNUC__) || defined(__clang__)
#define assert(expression) \
    ((expression) ? (void)0 : __builtin_trap())
#else
#define assert(expression) ((void)(expression))
#endif
#endif

#endif
#endif
