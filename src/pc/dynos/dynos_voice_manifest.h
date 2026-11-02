#ifndef PC_DYNOS_VOICE_MANIFEST_H
#define PC_DYNOS_VOICE_MANIFEST_H
#include <map>
#include <string>
#include <vector>
namespace dynos {
using VoiceManifest = std::map<std::string, std::vector<std::string>>;
/* Extracts static data only. No Lua VM, bytecode, callbacks or script execution. */
bool voice_manifest(const std::string &lua, const std::string &model, VoiceManifest &out, std::string &error);
bool voice_relative_path(const std::string &path);
std::string voice_sound_key(unsigned bits);
}
#endif
