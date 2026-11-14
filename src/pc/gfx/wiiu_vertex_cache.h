#ifndef WIIU_VERTEX_CACHE_H
#define WIIU_VERTEX_CACHE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* One lookup per RSP vertex slot, no hashing or floating-point comparison.
 * A slot reload, material change or submitted batch invalidates reuse.
 * LOD is triangle-dependent, so it is part of the key as well. */
#define WIIU_VERTEX_CACHE_SLOTS 68
struct WiiUVertexCacheEntry {
    uint32_t batch, material;
    uint16_t index;
    uint8_t lod;
};
struct WiiUVertexCache {
    uint32_t batch, material;
    struct WiiUVertexCacheEntry entries[WIIU_VERTEX_CACHE_SLOTS];
};
static inline void wiiu_vertex_cache_batch(struct WiiUVertexCache *cache) {
    if (++cache->batch == 0) {
        memset(cache->entries, 0, sizeof(cache->entries));
        cache->batch = 1;
    }
}
static inline void wiiu_vertex_cache_material(struct WiiUVertexCache *cache) {
    if (++cache->material == 0) {
        memset(cache->entries, 0, sizeof(cache->entries));
        cache->material = 1;
    }
}
static inline void wiiu_vertex_cache_reload(struct WiiUVertexCache *cache,
        unsigned first, unsigned count) {
    if (first <= WIIU_VERTEX_CACHE_SLOTS && count <= WIIU_VERTEX_CACHE_SLOTS - first)
        memset(cache->entries + first, 0, count * sizeof(cache->entries[0]));
}
static inline bool wiiu_vertex_cache_find(const struct WiiUVertexCache *cache,
        unsigned slot, uint8_t lod, uint16_t *index) {
    const struct WiiUVertexCacheEntry *entry = &cache->entries[slot];
    if (entry->batch != cache->batch || entry->material != cache->material ||
        entry->lod != lod)
        return false;
    *index = entry->index;
    return true;
}
static inline void wiiu_vertex_cache_store(struct WiiUVertexCache *cache,
        unsigned slot, uint8_t lod, uint16_t index) {
    struct WiiUVertexCacheEntry *entry = &cache->entries[slot];
    entry->batch = cache->batch;
    entry->material = cache->material;
    entry->index = index;
    entry->lod = lod;
}
#endif
