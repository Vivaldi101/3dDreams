#include "free_list.h"

static const size free_list_count = PAGE_SIZE / sizeof(list_node);

static void list_node_release(list_node* n)
{
   n->next = global_free_list.nodes;
   global_free_list.nodes = n;
   global_free_list.count++;
}

static list_node* list_node_push(arena* a, list_node* h)
{
   list_node* result = 0;

   // take from free list
   if(global_free_list.nodes && global_free_list.count != 0)
   {
      result = global_free_list.nodes;
      result->next = h->next;
      h->next = result;
      global_free_list.nodes++;
      global_free_list.count--;
      return result;
   }

   // make room in the free list
   global_free_list.nodes = push(a, list_node, free_list_count, arena_persistent_kind);

   result = global_free_list.nodes;
   result->next = h->next;

   h->next = result;

   global_free_list.count = free_list_count - 1;
   global_free_list.nodes++;

   return result;
}

static void list_release(list* list)
{
   list_node* t;
   do
   {
      t = list->head->next;
      list_node_release(list->head);
      list->head = t;
   }
   while(t);
}

#define list_push(a, h) (typeof(h))list_node_push((a), (list_node*)(h))
#define node_release(n) list_node_release((list_node*)(n))
