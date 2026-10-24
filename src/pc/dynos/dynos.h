#ifndef PC_DYNOS_H
#define PC_DYNOS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct GraphNode;
struct Animation;
void dynos_init(void);
void dynos_update(void);
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
