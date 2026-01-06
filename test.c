#include "ripc.c"
#include "ripc.h"
#include <stdio.h>

int main(void) {
    Arena *arena = arena_new(RIBC_KIB(1), RIBC_GIB(1));

    u8 *much_data = arena_push_array(arena, u8, RIBC_KIB(64));

    SliceU8 s = {0};

    SliceU8 s2 = {0};
    RIPC_SLICE(s, s2, 0, 0);

    String str = RIPC_STR("Hello World\n");
    printf("%.*s", (int)str.len, str.data);

    String allocated = arena_push_slice(arena, String, 20);

    arena_free(arena);
}
