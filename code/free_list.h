#include "common.h"

align_struct list_node
{
   struct list_node* next;
   void* data;
} list_node;

#define list_node(T) __declspec(align(custom_alignment)) \
struct { struct list_node* next; void* data; }

// sanity check for generic list_node
static_assert(offsetof(list_node, data) == offsetof(list_node(int), data));

align_struct free_list
{
   list_node* nodes;
   size count;
} free_list;

align_struct list
{
   free_list available_nodes;
   list_node* nodes;
   list_node* head;
} list;

static free_list global_free_list; // TODO: Remove this and use list struct above
