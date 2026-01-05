#include "ripc.c"
#include "ripc.h"

int main(void) {
    Arena *arena = arena_new(RIBC_KIB(1), RIBC_GIB(1));

    u8 *much_data = arena_push_array(arena, u8, RIBC_KIB(64));

    arena_free(arena);
}
