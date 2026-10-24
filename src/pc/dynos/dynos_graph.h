#ifndef DYNOS_GRAPH_H
#define DYNOS_GRAPH_H
#include <vector>
struct GraphNode;
namespace dynos {
bool native_hierarchy(GraphNode *root, std::vector<int> &parents);
}
#endif
