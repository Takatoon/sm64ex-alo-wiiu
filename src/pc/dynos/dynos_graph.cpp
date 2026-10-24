#include "dynos_graph.h"
#include <functional>
#include <set>
extern "C" {
#include "engine/graph_node.h"
}
namespace dynos {
bool native_hierarchy(GraphNode *root,std::vector<int> &parents) {
    std::set<GraphNode*> active;unsigned visits=0;bool valid=true;
    size_t optional_actor_size=0;
    std::function<std::vector<int>(GraphNode*,int,int,unsigned,bool)> scan;
    scan=[&](GraphNode *first,int parent,int base,unsigned depth,bool single) {
        std::vector<int> result;if(!first)return result;
        GraphNode *node=first;
        do {
            if(!node||depth>32||++visits>32768||!active.insert(node).second){valid=false;break;}
            int joint=parent;
            if(node->type==GRAPH_NODE_TYPE_ANIMATED_PART){joint=base+int(result.size());result.push_back(parent);}
            std::vector<int> sub;
            if(node->type==GRAPH_NODE_TYPE_SWITCH_CASE&&node->children) {
                GraphNode *child=node->children;bool initial=true,empty_variant=false;
                do {
                    auto variant=scan(child,joint,base+int(result.size()),depth+1,true);
                    if(joint==-1&&base==0&&result.empty()&&variant.empty())empty_variant=true;
                    else if(initial)sub=variant,initial=false;else if(variant!=sub)valid=false;
                    child=child->next;
                }while(valid&&child&&child!=node->children);
                if(empty_variant&&!sub.empty()){
                    if(optional_actor_size&&optional_actor_size!=sub.size())valid=false;
                    optional_actor_size=sub.size();
                }
            } else sub=scan(node->children,joint,base+int(result.size()),depth+1,false);
            // Consecutive LOD nodes are alternatives, not additional joints.
            if(node->type==GRAPH_NODE_TYPE_LEVEL_OF_DETAIL) {
                while(node->next&&node->next!=first&&node->next->type==GRAPH_NODE_TYPE_LEVEL_OF_DETAIL) {
                    active.erase(node);node=node->next;
                    if(++visits>32768||!active.insert(node).second){valid=false;break;}
                    if(scan(node->children,parent,base+int(result.size()),depth+1,false)!=sub)valid=false;
                }
            }
            result.insert(result.end(),sub.begin(),sub.end());
            if(result.size()>64)valid=false;
            active.erase(node);node=node->next;
        }while(valid&&!single&&node&&node!=first);
        return result;
    };
    parents=scan(root,-1,0,0,false);
    return valid&&(!optional_actor_size||optional_actor_size==parents.size());
}
}
