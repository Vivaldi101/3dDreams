#include "free_list.h"

static const size free_list_count = PAGE_SIZE / sizeof(list_node);

static void list_node_release(list_node* n)
{
   n->next = global_free_list.nodes;
   global_free_list.nodes = n;
   global_free_list.count++;
}

static list_node* list_node_get(list_node* h)
{
   list_node* result = 0;

   // take from free list
   if(global_free_list.nodes && global_free_list.count != 0)
   {
      result = global_free_list.nodes;
      global_free_list.nodes = result->next;
      global_free_list.count--;
      return result;
   }

   // make room in the free list
   global_free_list.nodes = push(h->a, list_node, free_list_count, arena_persistent_kind);

   result = global_free_list.nodes;
   result->a = h->a;

   h->next = result;

   global_free_list.nodes = result->next;
   global_free_list.count = free_list_count - 1;

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


   // wp(Body, I)
   // B1: t = head->next;
   // B2: release_list_node(head);
   // B3: head = t;

   // wp(B1, B2, B3, I)
   // wp(B1, B2, wp(B3, I))
   // wp(B1, B2, wp(head = t, I))
   // wp(B1, B2, I{head := t})
   // wp(B1, wp(B2, I{head := t}))
   // wp(B1, wp(free(head), I{head := t}))
   // wp(B1, (free(head) && I{head := t}))

   // free(head) && I{head := head->next} && !head->next => R
}
