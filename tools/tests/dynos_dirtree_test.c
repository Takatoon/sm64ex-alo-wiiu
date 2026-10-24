#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Wii U heap allocations can contain old data; do not rely on fresh zero pages. */
static void *poisoned_malloc(size_t size) {
    void *memory = malloc(size);
    if (memory) memset(memory, 0xa5, size);
    return memory;
}
#define malloc poisoned_malloc
#include "../../src/pc/fs/dirtree.c"
#undef malloc

static bool count_file(void *user, const char *name) {
    assert(!strcmp(name, "Pack/mario_geo.bin"));
    ++*(unsigned *)user;
    return true;
}

int main(void) {
    fs_dirtree_t tree;
    unsigned count = 0;
    assert(fs_dirtree_init(&tree, sizeof(fs_dirtree_entry_t)));
    assert(tree.root->next_child == NULL);
    assert(tree.root->next_sibling == NULL);
    assert(tree.root->next_hash == NULL);
    assert(fs_dirtree_walk(&tree, "", count_file, &count, true) == FS_WALK_SUCCESS);
    assert(count == 0);
    char path[] = "Pack/mario_geo.bin";
    assert(fs_dirtree_add(&tree, path, false));
    assert(fs_dirtree_walk(&tree, "", count_file, &count, true) == FS_WALK_SUCCESS);
    assert(count == 1);
    fs_dirtree_free(&tree);
    return 0;
}
