#include "free_list.h"

static void list_node_release(list* l, list_node* n)
{
   printf("Releasing node to free-list: %p\n", n);

   n->next = l->free_list;
   l->free_list = n;
}

static list_node* list_node_push(arena* a, list* l)
{
   list_node* result = 0;

   if(l->free_list)
   {
      // take first from free list
      result = l->free_list;
      l->free_list = l->free_list->next;
      return result;
   }

   // how much to allocate in burst - align to 4k page size usually
   const size list_count = PAGE_SIZE / sizeof(list_node);

   // first time alloc or ran out
   if(!l->nodes || l->node_count == list_count)
   {
      l->node_count = 0;
      l->nodes = push(a, list_node, list_count, arena_persistent_kind);
   }

   // circular
   result = l->nodes + l->node_count;
   result->next = l->nodes->next;

   l->nodes->next = result;
   l->node_count++;

   return result;
}

static void list_release(list* l)
{
   for(size i = 0; i < l->node_count; ++i)
      list_node_release(l, l->nodes + i);
}

static void free_list_print(list* l)
{
   list_node* n = l->free_list;
   
   while(n)
   {
      printf("Free-list node: %p\n", n);
      n = n->next;
   }
}

#define list_push(a, l) (typeof(*(l.nodes)))list_node_push((a), (list*)(l))

#define list_free(l, t) \
   static_assert(offsetof(l, node_count) == offsetof(list, node_count)); \
   list_release((list*)(l))

#define node_release(l, n) list_node_release((l), (n))
