#include "free_list.h"

static void list_node_release(list* l, list_node* n)
{
   n->next = l->free_list->next;
   l->free_list->next = n;
}

static list_node* list_node_push(arena* a, list* l, size node_size)
{
   list_node* result = 0;

   if(l->free_list && l->free_list->next)
   {
      // take first from free list
      result = l->free_list->next;
      l->free_list->next = l->free_list->next->next;
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

   // dummy free list header
   if(!l->free_list)
      l->free_list = push(a, list_node);

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
   list_node* n = l->free_list->next;
   
   while(n)
   {
      printf("Free-list node: %p\n", n);
      n = n->next;
   }
}

#if 0
static void free_list_tests(arena* a)
{
   // l->head is m, m->next is n, n->next is null
   list l = {0};

   list_node* n = list_push(a, &l); n->data.slot_size = 1;
   list_node* m = list_push(a, &l); m->data.slot_size = 2;
   list_node* q = list_push(a, &l); q->data.slot_size = 42;

   node_release(&l, q);
   node_release(&l, m);
   node_release(&l, n);

   free_list_print(&l);

   size target_size = 42;
   list_node target = {0};
   target.data.slot_size = target_size;

   list_node* f = l.free_list;
   list_node* prev = 0;
   while(f && (target.data.slot_size != f->data.slot_size))
   {
      prev = f;
      f = f->next;
   }

   assert(prev);
   assert(prev != f);
   assert(!f || (target.data.slot_size == f->data.slot_size));

   if(f)
   {
      prev->next = f->next;
      f->next = 0;
      target = *f;
      printf("Found slot size: %zu in %p\n", target.data.slot_size, f);
   }

   assert(implies(f, target.data.slot_size == f->data.slot_size));

   target.data.slot_size = target_size;
   f = l.free_list;

   while(f && (target.data.slot_size != f->data.slot_size))
      f = f->next;

   assert(!f || (target.data.slot_size == f->data.slot_size));

   if(f)
   {
      target = *f;
      printf("Found slot size: %zu\n", target.data.slot_size);
   }

   assert(implies(f, target.data.slot_size == f->data.slot_size));

   return false;
}
#endif
