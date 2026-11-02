#ifndef PC_DYNOS_H
#define PC_DYNOS_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
struct GraphNode;
struct Animation;
void dynos_init(void);
void dynos_update(void);
void dynos_set_reload_hooks(bool (*begin)(void), void (*end)(void));
void dynos_request_apply(void);
bool dynos_apply_pending(void);
unsigned int dynos_texture_count(void);
const unsigned char **dynos_texture_labels(void);
unsigned int dynos_npc_pack_count(void);
const unsigned char **dynos_npc_pack_labels(void);
uint64_t dynos_resident_bytes(void);
extern unsigned int dynos_mods_enabled, dynos_texture_choice, dynos_npc_choice;
void dynos_shutdown(void);
void dynos_request_player_step(int direction);
void dynos_clear_npc_bindings(void);
void dynos_bind_model(unsigned model,const void *layout,struct GraphNode *original);
struct GraphNode *dynos_npc_graph(struct GraphNode *original);
struct GraphNode *dynos_npc_graph_with_animation(struct GraphNode *original,
    const struct Animation *native, const struct Animation **replacement);
struct GraphNode *dynos_player_graph(struct GraphNode *original);
void *dynos_texture_read(const char *path, uint64_t *size);
unsigned int dynos_player_count(void);
const unsigned char **dynos_player_labels(void);
extern unsigned int dynos_player_choice;
#ifdef __cplusplus
}
#endif
#endif
