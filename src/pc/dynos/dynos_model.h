#ifndef DYNOS_MODEL_H
#define DYNOS_MODEL_H

/* Native DynOS binary reader. No source-code generation or Lua execution. */
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <array>
#include <cstdint>
extern "C" {
#include <PR/mbi.h>
#include <PR/gbi.h>
#include "types.h"
}

namespace dynos {
using CallbackResolver = uintptr_t (*)(unsigned);
struct Word {
    uint32_t value = 0, offset = 0;
    unsigned kind = 0; // 0 literal, 1 named reference, 2 callback
    std::string name;
};
struct Record {
    unsigned type = 0;
    std::string name, texture_path, texture_alias;
    std::vector<uint8_t> bytes;
    std::vector<Vtx> vertices;
    std::vector<Gfx> display_list;
    std::vector<uintptr_t> layout;
    std::vector<Word> words;
    std::array<uint8_t, 32> light{};
};
struct ModelAnimation {
    Animation native{};
    std::vector<int16_t> values;
    std::vector<uint16_t> indices;
};
struct Model {
    std::map<std::string, std::unique_ptr<Record>> records;
    std::array<Lights1, 9> palette{};
    Record *root = nullptr;
    size_t memory_bytes = 0, vertex_count = 0, texture_count = 0;
    std::vector<int> animation_parents, compatible_hierarchy;
    std::map<std::string,std::unique_ptr<ModelAnimation>> animations;
    std::vector<ModelAnimation*> animation_table;
    bool custom_animation_hierarchy=false;
    unsigned static_joint_count=0;
};
std::unique_ptr<Model> load(const uint8_t *data, size_t size,
                          const std::string &key, const std::string &slot,
                          CallbackResolver resolver, std::string &error,
                          const std::vector<int> *expected_hierarchy = nullptr,
                          bool allow_actor_animations = false);
}
#endif
