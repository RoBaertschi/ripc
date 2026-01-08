#include "ripc.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#ifdef RIPC_POSIX
#   include <unistd.h>
#   include <limits.h>
#   include <sys/mman.h>
#   include <fcntl.h>
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

// ripc: platform -> mutex

RIPC_FUNC void mutex_init(Mutex *mutex) {
#ifdef RIPC_POSIX
    RIPC_UNUSED(pthread_mutexattr_init(&mutex->attr));
    RIPC_UNUSED(pthread_mutexattr_settype(&mutex->attr, PTHREAD_MUTEX_NORMAL));
    RIPC_UNUSED(pthread_mutex_init(&mutex->mutex, &mutex->attr));
#else
    assert(false && "Unsupported platform");
#endif
}
RIPC_FUNC void mutex_destroy(Mutex *mutex) {
#ifdef RIPC_POSIX
    RIPC_UNUSED(pthread_mutex_destroy(&mutex->mutex));
    RIPC_UNUSED(pthread_mutexattr_destroy(&mutex->attr));
#else
    assert(false && "Unsupported platform");
#endif
}
RIPC_FUNC void mutex_lock(Mutex *mutex) {
#ifdef RIPC_POSIX
    RIPC_UNUSED(pthread_mutex_lock(&mutex->mutex));
#else
    assert(false && "Unsupported platform");
#endif
}
RIPC_FUNC b32 mutex_try_lock(Mutex *mutex) {
#ifdef RIPC_POSIX
    int result = pthread_mutex_trylock(&mutex->mutex);
    return result == 0;
#else
    assert(false && "Unsupported platform");
#endif
}
RIPC_FUNC void mutex_unlock(Mutex *mutex) {
    RIPC_UNUSED(pthread_mutex_unlock(&mutex->mutex));
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
    mutex_init(&arena->mutex);

    return arena;
}

RIPC_FUNC void arena_free(Arena *arena) {
#if RIPC_HAVE_ASAN
    void *data = NULL;
    usize size = 0;
    RIPC_MUTEX_GUARD(&arena->mutex) {
        data = arena->data;
        size = arena->reserved;
    }
#endif
    // WARN: possible race condition, the mutex might not be unlocked. It is very hard to prove that it isn't.
    mutex_destroy(&arena->mutex);
    vm_release(arena->data, arena->reserved);
#if RIPC_HAVE_ASAN
    RIPC_ASAN_POISON(data, size);
#endif
}

RIPC_FUNC void *arena_push(Arena *arena, isize size, isize alignment) {
    void *address = NULL;
    RIPC_MUTEX_GUARD(&arena->mutex) {
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

        address = (void*)(current_pos + (uintptr)arena->data);
        memset(address, 0, (usize)size);
    }

    return address;
}

RIPC_FUNC void arena_pos_set(Arena *arena, usize pos) {
    RIPC_MUTEX_GUARD(&arena->mutex) {
        arena->pos = RIPC_MAX(pos, sizeof(Arena));
        RIPC_ASAN_POISON((void*)(arena->pos + (uintptr)arena->data), arena->reserved);
    }
}

RIPC_FUNC void arena_clear(Arena *arena) {
    arena_pos_set(arena, sizeof(Arena));
}

RIPC_FUNC ArenaTemp arena_temp_begin(Arena *arena) {
    usize saved_pos = 0;
    RIPC_MUTEX_GUARD(&arena->mutex) {
        saved_pos = arena->pos;
    }

    return (ArenaTemp) {
        .arena     = arena,
        .saved_pos = saved_pos,
    };
}

RIPC_FUNC void arena_temp_end(ArenaTemp temp) {
    arena_pos_set(temp.arena, temp.saved_pos);
}

RIPC_THREAD_LOCAL Arena *_scratch_arenas[2] = { 0 };

RIPC_FUNC ArenaTemp arena_scratch_get(Arena **conflicts, isize conflicts_count) {
    isize scratch_index = -1;

    for (isize i = 0; i < RIPC_ARRAY_SIZE(_scratch_arenas); i++) {
        b32 conflict_found = false;

        for (isize j = 0; j < conflicts_count; j++) {
            if (_scratch_arenas[i] == conflicts[j]) {
                conflict_found = true;
                break;
            }
        }

        if (!conflict_found) {
            scratch_index = i;
            break;
        }
    }

    if (scratch_index == -1) {
        return (ArenaTemp) { 0 };
    }

    Arena **selected = &_scratch_arenas[scratch_index];

    if (*selected == NULL) {
        *selected = arena_new(RIPC_MIB(1), RIPC_MIB(64));
    }

    return arena_temp_begin(*selected);
}

RIPC_FUNC void arena_scratch_end(ArenaTemp temp) {
    arena_temp_end(temp);
}

// ripc: strings

RIPC_FUNC char* string_to_cstring(Arena *arena, String str) {
    char* cstr = arena_push_array(arena, char, str.len + 1);
    cstr[str.len] = 0;
    memcpy(cstr, str.data, (usize)str.len);
    return cstr;
}

RIPC_FUNC b32 string_eq(String a, String b) {
    if (a.len != b.len) {
        return false;
    }

    return memcmp(a.data, b.data, (usize)a.len) == 0;
}

// ripc: file system

RIPC_FUNC Bytes fs_read_entire_file(Arena *arena, String path, b32 *ok) {
    ArenaTemp temp = arena_scratch_get(NULL, 0);
    char const* cpath = string_to_cstring(temp.arena, path);
    arena_scratch_end(temp);
    FILE *file = fopen(cpath, "r");
    if (file == NULL) {
        if (ok != NULL) {
            *ok = false;
        }
        return (Bytes){0};
    }

    fseek(file, 0, SEEK_END);
    isize len = ftell(file);
    fseek(file, 0, SEEK_SET);

    Bytes data = arena_push_slice(arena, Bytes, len);
    fread(data.data, sizeof(u8), len, file);

    fclose(file);
    return data;
}
