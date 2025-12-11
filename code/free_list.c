#include "free_list.h"

static void list_node_release(list* l, list_node* n)
{
   n->next = l->free_list->next;
   l->free_list->next = n;
}

static list_node* free_list_node(list* l)
{
   assert(l->free_list && l->free_list->next);

   list_node* result = 0;

   // take first from free list
   result = l->free_list->next;
   l->free_list->next = l->free_list->next->next;

   return result;
}

static list_node* list_node_push(arena* a, list* l, size node_memory_size)
{
   size alloc_size = sizeof(list_node) + node_memory_size;
   list_node* result = alloc(a, alloc_size, __alignof(list_node), 1, list_persistent_kind);

   // dummy free list header
   if(!l->free_list)
      l->free_list = alloc(a, alloc_size, __alignof(list_node), 1, list_persistent_kind);

   result->next = l->head;
   l->head = result;


   result->data.memory = result + 1;

   return result;
}

static void free_list_print(list* l)
{
   list_node* n = l->free_list->next;
   
   while(n)
      n = n->next;
}

#if 0
static void free_list_tests(arena* a)
{
   list l = {0};

   for(size i = 0; i < 2; ++i)
   {
      list_node* n0 = list_push(a, &l);
      n0->data.slot_size = 1280;

      list_node* n1 = list_push(a, &l);
      n1->data.slot_size = 128;

      node_release(&l, n0);
      node_release(&l, n1);
   }

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

   list_release(&l);
}
#endif
