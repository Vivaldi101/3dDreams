#include "common.h"

align_struct list_node
{
   struct list_node* next;
   void* data;
} list_node;

#define list_node(T) __declspec(align(custom_alignment)) \
struct { struct list_node* next; T data; }

// sanity check for generic list_node
static_assert(offsetof(list_node, data) == offsetof(list_node(int), data));

align_struct list
{
   list_node* free_list;
   list_node* nodes;
   size node_count;
} list;

//#define list(T) __declspec(align(custom_alignment)) \
//struct { struct list_node* next; T data; }

// sanity check for generic list
//static_assert(offsetof(list, nodes_left) == offsetof(list(int), nodes_left));
