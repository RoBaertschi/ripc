#include "ripc.h"
#include <assert.h>
#include <string.h>

#ifdef RIPC_POSIX
#   include <unistd.h>
#   include <limits.h>
#   include <sys/mman.h>
#endif

// ripc: platform
// ripc: platform -> virtual memory

usize vm_page_size(void) {
#ifdef RIPC_POSIX
    RIPC_FUNCTION_STATIC isize cached_page_size = -1;
    if (cached_page_size < 0) {
        cached_page_size = (isize)sysconf(_SC_PAGESIZE);
        // TODO(robin): panic/fail on error
        assert(cached_page_size > 0);
    }
    return cached_page_size;
#else
    assert(false && "Unsupported platform");
    return 0;
#endif
}

void *vm_reserve(usize size) {
#ifdef RIPC_POSIX
#ifdef MAP_ANONYMOUS
#   define RIPC_ANONYMOUS MAP_ANONYMOUS
#elif defined(MAP_ANON)
#   define RIPC_ANONYMOUS MAP_ANON
#else
#   error "Platform does not support anonymous mapping."
#endif

    void *address = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (address == MAP_FAILED) {
        address = NULL;
    }
    return address;
#else
    assert(false && "Unsupported platform");
    return NULL;
#endif
}

b32 vm_commit(void *ptr, usize size) {
#ifdef RIPC_POSIX
    int result = mprotect(ptr, size, PROT_READ | PROT_WRITE);
    if (result < 0) {
        return false;
    }
    return true;
#else
    assert(false && "Unsupported platform");
    return NULL;
#endif
}
void vm_release(void *ptr, usize size) {
#ifdef RIPC_POSIX
    RIPC_UNUSED(munmap(ptr, size));
#else
    assert(false && "Unsupported platform");
    return NULL;
#endif
}

// ripc: arena
//
RIPC_FUNC Arena *arena_new(usize commited, usize reserved) {
    usize page_size = vm_page_size();
    commited = RIPC_ALIGN_UP_POW2(commited + sizeof(Arena), page_size);
    reserved = RIPC_ALIGN_UP_POW2(RIPC_MAX(reserved, commited), page_size);

    void *data = vm_reserve(reserved);
    if (data == NULL) {
        return NULL;
    }

    if (!vm_commit(data, commited)) {
        vm_release(data, reserved);
        return NULL;
    }

    RIPC_ASAN_POISON(data, reserved);
    RIPC_ASAN_UNPOISON(data, sizeof(Arena));

    Arena *arena    = data;
    arena->commited = commited;
    arena->reserved = reserved;
    arena->data     = data;
    arena->pos      = sizeof(Arena);

    return arena;
}

RIPC_FUNC void arena_free(Arena *arena) {
#if RIPC_HAVE_ASAN
    void *data = arena->data;
    usize size = arena->reserved;
#endif

    vm_release(arena, arena->reserved);

#if RIPC_HAVE_ASAN
    RIPC_ASAN_POISON(data, size);
#endif
}

RIPC_FUNC void *arena_push(Arena *arena, isize size, isize alignment) {
    if (size <= 0) {
        return NULL;
    }

    if (alignment <= 0) {
        alignment = 16; // TODO(robin): is this a good default alignment?
    }

    usize current_pos = RIPC_ALIGN_UP_POW2(arena->pos, alignment);

    if (current_pos + size > arena->reserved) {
        // TODO(robin): support a linked list of memory blocks for the arena
        return NULL;
    }

    if (current_pos + size > arena->commited) {
        usize page_size = vm_page_size();
        usize new_commited = RIPC_ALIGN_UP_POW2(current_pos + (usize)size, page_size);
        if (!vm_commit(arena->data, new_commited)) {
            return NULL;
        }
        arena->commited = new_commited;
    }

    arena->pos += size;
    RIPC_ASAN_UNPOISON(arena->data, arena->pos);

    void *address = (void*)(current_pos + (uintptr)arena->data);
    memset(address, 0, (usize)size);
    return address;
}

RIPC_FUNC void arena_pos_set(Arena *arena, usize pos) {
    arena->pos = RIPC_MAX(pos, sizeof(Arena));
    RIPC_ASAN_POISON((void*)(arena->pos + (uintptr)arena->data), arena->reserved);
}

RIPC_FUNC void arena_clear(Arena *arena) {
    arena_pos_set(arena, sizeof(Arena));
}

RIPC_FUNC ArenaTemp arena_temp_begin(Arena *arena) {
    return (ArenaTemp) {
        .arena     = arena,
        .saved_pos = arena->pos,
    };
}

RIPC_FUNC void arena_temp_end(ArenaTemp temp) {
    arena_pos_set(temp.arena, temp.saved_pos);
}
