#ifndef PC_DYNOS_VOICE_MANIFEST_H
#define PC_DYNOS_VOICE_MANIFEST_H
#include <map>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace dynos {
using VoiceManifest = std::map<std::string, std::vector<std::string>>;
struct PlayerDeclaration {
    std::string model, name, slot="mario";
    bool has_palette=false;
    std::array<uint32_t,8> palette{};
};
std::vector<PlayerDeclaration> player_declarations(const std::string &lua);
bool read_pack_lua(const char *pack, std::string &lua);
/* Extracts static data only. No Lua VM, bytecode, callbacks or script execution. */
bool voice_manifest(const std::string &lua, const std::string &model, VoiceManifest &out, std::string &error);
bool voice_relative_path(const std::string &path);
std::string voice_sound_key(unsigned bits);
}
#endif
