#include "common.h"

align_struct list_node
{
   struct list_node* next;
   void* data;
} list_node;

align_struct free_list
{
   size count; // TODO: might not need this though
   list_node* nodes;
} free_list;

align_struct list
{
   free_list available_nodes;
   list_node* nodes;
   list_node* head;
} list;

static free_list global_free_list;

#define list_node(T) __declspec(align(custom_alignment)) \
struct { struct list_node* next; void* data; }
