#if !defined(_FREE_LIST_H)
#define _FREE_LIST_H

#include "common.h"

typedef struct vk_memory_slot
{
   size slot_size;
   void* memory;
} vk_memory_slot;

align_struct list_node
{
   vk_memory_slot data;
   struct list_node* next;
} list_node;

static_assert(offsetof(vk_memory_slot, slot_size) == offsetof(list_node, data));

align_struct list
{
   list_node* free_list;
   list_node* nodes;
   list_node* head;
   size node_count;
} list;

#define list_push(a, l) list_node_push((a), (l))

// TODO: cleanup sanity asserts
#define list_free(l) \
   static_assert(offsetof(typeof(*l), node_count) == offsetof(list, node_count)); \
   static_assert(sizeof(typeof(*l)) == sizeof(list)); \
   list_release((list*)l)

#define node_release(l, n) list_node_release((l), (n))

#endif
