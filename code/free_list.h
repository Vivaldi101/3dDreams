#include "common.h"

align_struct list_node
{
   struct list_node* next;
   struct list_node* free_list;
   arena* a;
   void* data;
} list_node;

#define list_node(T) __declspec(align(custom_alignment)) \
struct { T* data; arena* a; struct list_node* next; struct list_node* free_list; }
