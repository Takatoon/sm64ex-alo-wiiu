#include <cassert>
#include "pc/dynos/dynos_graph.h"
extern "C" {
#include "engine/graph_node.h"
}
static GraphNode node(int type) {GraphNode n{};n.type=type;return n;}
static void circle(GraphNode &n){n.next=n.prev=&n;}
int main() {
    GraphNode root=node(GRAPH_NODE_TYPE_START),joint=node(GRAPH_NODE_TYPE_ANIMATED_PART);
    GraphNode sw=node(GRAPH_NODE_TYPE_SWITCH_CASE),a=node(GRAPH_NODE_TYPE_ANIMATED_PART),b=a;
    for(auto *n:{&root,&joint,&sw,&a,&b})circle(*n);
    root.children=&joint;joint.children=&sw;sw.children=&a;a.next=&b;b.next=&a;
    std::vector<int> parents;
    assert(dynos::native_hierarchy(&root,parents));assert((parents==std::vector<int>{-1,0}));
    GraphNode extra=node(GRAPH_NODE_TYPE_ANIMATED_PART);circle(extra);b.children=&extra;
    assert(!dynos::native_hierarchy(&root,parents));b.children=nullptr;
    GraphNode lod1=node(GRAPH_NODE_TYPE_LEVEL_OF_DETAIL),lod2=lod1;
    circle(lod1);circle(lod2);lod1.next=&lod2;lod2.next=&lod1;lod1.children=&a;lod2.children=&b;
    circle(a);circle(b);joint.children=&lod1;
    assert(dynos::native_hierarchy(&root,parents));assert((parents==std::vector<int>{-1,0}));
    b.children=&extra;assert(!dynos::native_hierarchy(&root,parents));
    joint.children=&root;assert(!dynos::native_hierarchy(&root,parents));
    GraphNode body=node(GRAPH_NODE_TYPE_START),shadow=body;
    for(auto *n:{&body,&shadow,&joint,&sw,&root,&extra})circle(*n);
    root.children=&sw;sw.children=&body;body.children=&joint;shadow.children=nullptr;joint.children=nullptr;
    body.next=&shadow;shadow.next=&body;
    assert(dynos::native_hierarchy(&root,parents));assert((parents==std::vector<int>{-1}));
    sw.next=&extra;extra.next=&sw;
    assert(!dynos::native_hierarchy(&root,parents));
    return 0;
}
