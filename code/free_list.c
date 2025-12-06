#include "free_list.h"

static void release_list_node(list_node* n)
{
   n->next = n->free_list;
   n->free_list = n;
}

static list_node* get_list_node(list_node* n)
{
   list_node* result = 0;
   if(n->free_list)
   {
      result = n->free_list;
      n->free_list = n->free_list->next;

      return result;
   }

   result = push(n->a, list_node);

   return result;
}

static void release_list(list_node* head)
{
   list_node* t;
   do
   {
      t = head->next;
      release_list_node(head);
      head = t;
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
