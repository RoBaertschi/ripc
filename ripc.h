#ifndef RIPC_H
#define RIPC_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t   u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t   i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uintptr_t uintptr;

typedef uintptr   usize;
typedef ptrdiff_t isize;


typedef u8   b8;
typedef u16 b16;
typedef u32 b32;
typedef u64 b64;

#define false 0
#define true  1

typedef float  f32;
typedef double f64;

#ifndef RIPC_FUNC
#   define RIPC_FUNC
#endif

#if defined (__linux) || defined (linux) || defined (__linux__)
#   ifndef RIPC_LINUX
#       define RIPC_LINUX 1
#   endif
#else
#   error "Unsupported operating system."
#endif

#if defined (__clang__)
#   define RIPC_COMPILER_CLANG 1
#elif defined (__GNUC__)
#   define RIPC_COMPILER_GCC 1
#else
#   error "Unsupported compiler."
#endif

#if defined (RIPC_COMPILER_CLANG)
#   define RIPC_ALIGNOF(a) __alignof(a)
#elif defined (RIPC_COMPILER_GCC)
#   define RIPC_ALIGNOF(a) __alignof__(a)
#else
#   error "Unsupported alignof for current compiler."
#endif

#ifdef RIPC_LINUX
#   ifndef RIPC_POSIX
#       define RIPC_POSIX 1
#   endif
#endif

#ifndef RIPC_ALIGN_UP_POW2
#   define RIPC_ALIGN_UP_POW2(n, p) (((usize)(n) + ((usize)(p) - 1)) & (~((usize)(p) - 1)))
#endif

#ifndef RIPC_FUNCTION_STATIC
#   define RIPC_FUNCTION_STATIC static
#endif

#ifndef RIPC_UNUSED
#   define RIPC_UNUSED(a) (void)a
#endif

#ifndef RIPC_MIN
#   define RIPC_MIN(a, b) (a) > (b) ? (b) : (a)
#endif

#ifndef RIPC_MAX
#   define RIPC_MAX(a, b) (a) <= (b) ? (b) : (a)
#endif

#ifndef RIBC_MEM_UNITS_DEFINED
#   define RIBC_KIB(n) ((u64)(n) << 10)
#   define RIBC_MIB(n) ((u64)(n) << 20)
#   define RIBC_GIB(n) ((u64)(n) << 30)
#   define RIBC_TIB(n) ((u64)(n) << 40)
#endif

// ripc: platform
// ripc: asan

#ifdef __has_feature
#   if __has_feature(address_sanitizer) || defined (__SANITIZE_ADDRESS__)
#       define RIPC_HAVE_ASAN 1
#   else
#       define RIPC_HAVE_ASAN 0
#   endif
#elif defined(__SANITIZE_ADDRESS__)
#   define RIPC_HAVE_ASAN 1
#else
#   define RIPC_HAVE_ASAN 0
#endif

#if RIPC_HAVE_ASAN
// WARN(robin): Not thread safe.
void __asan_poison_memory_region(void const volatile *addr, usize size);
// WARN(robin): Not thread safe.
void __asan_unpoison_memory_region(void const volatile *addr, usize size);
// WARN(robin): Not thread safe.
#   define RIPC_ASAN_POISON(addr, size) __asan_poison_memory_region((addr), (size))
// WARN(robin): Not thread safe.
#   define RIPC_ASAN_UNPOISON(addr, size) __asan_unpoison_memory_region((addr), (size))
#else
// WARN(robin): Remember that the actual functions are not thread safe.
#   define RIPC_ASAN_POISON(addr, size) RIPC_UNUSED(addr), RIPC_UNUSED(size)
// WARN(robin): Remember that the actual functions are not thread safe.
#   define RIPC_ASAN_UNPOISON(addr, size) RIPC_UNUSED(addr), RIPC_UNUSED(size)
#endif

// ripc: platform -> virtual memory

usize vm_page_size(void);

void *vm_reserve(usize size);
b32 vm_commit(void *ptr, usize size);
void vm_release(void *ptr, usize size);

// ripc: arena

// TODO(robin): add mutex for thread saftey with asan and allocations
typedef struct Arena {
    void  *data;
    usize commited;
    usize reserved;
    usize pos;
} Arena;

typedef struct ArenaTemp {
    Arena *arena;
    usize  saved_pos;
} ArenaTemp;

RIPC_FUNC Arena *arena_new(usize commited, usize reserved);
RIPC_FUNC void arena_free(Arena *arena);

#define arena_push_struct(arena, type) arena_push((arena), sizeof(type), RIPC_ALIGNOF(type))
#define arena_push_array(arena, type, count) arena_push((arena), sizeof(type) * (count), RIPC_ALIGNOF(type))

RIPC_FUNC void *arena_push(Arena *arena, isize size, isize alignment);
RIPC_FUNC void arena_pos_set(Arena *arena, usize pos);
RIPC_FUNC void arena_clear(Arena *arena);
RIPC_FUNC ArenaTemp arena_temp_begin(Arena *arena);
RIPC_FUNC void arena_temp_end(ArenaTemp temp);

#endif // RIPC_H
