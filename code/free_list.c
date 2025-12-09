#include "free_list.h"

static void list_node_release(list* l, list_node* n)
{
   //printf("Releasing node to free-list: %p with size: %zu\n", n, n->data.slot_size);

   n->next = l->free_list;
   l->free_list = n;
}

static list_node* list_node_push(arena* a, list* l, size node_size)
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
   const size list_count = PAGE_SIZE / node_size;

   // first time alloc or ran out
   if(!l->nodes || l->node_count == list_count)
   {
      l->node_count = 0;
      l->nodes = push(a, list_node, list_count, arena_persistent_kind);
   }

   result = l->nodes + l->node_count;
   result->next = l->head;
   l->head = result;

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

#if 0
static void free_list_tests(arena* a)
{
   list(size) l = {0};

   for(size i = 0; i < 64; ++i)
   {
      node_size_t* n = list_push(a, &l);

      n->data = i;
   }

   list_free(&l);

}
#endif
