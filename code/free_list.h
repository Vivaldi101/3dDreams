#if !defined(_FREE_LIST_H)
#define _FREE_LIST_H

#include "common.h"

typedef struct vk_memory_slot
{
   void* memory;
   size slot_size;
} vk_memory_slot;

align_struct list_node
{
   struct list_node* next;
   vk_memory_slot data; // TODO: dummy for testing
} list_node;

//#define list_node(T) \
//__declspec(align(custom_alignment)) \
//struct { struct list_node* next; T data; }

// sanity check for generic list_node
//static_assert(offsetof(list_node, data) == offsetof(list_node(int), data));

align_struct list
{
   list_node* free_list;
   list_node* nodes;
   list_node* head;
   size node_count;
} list;

#define list_push(a, l) list_node_push((a), (l), sizeof(*(l)->nodes))

// TODO: cleanup sanity asserts
#define list_free(l) \
   static_assert(offsetof(typeof(*l), node_count) == offsetof(list, node_count)); \
   static_assert(sizeof(typeof(*l)) == sizeof(list)); \
   list_release((list*)l)

#define node_release(l, n) list_node_release((l), (n))

#endif
