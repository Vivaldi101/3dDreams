#include "vulkan_shader_module.h"

#include <stdio.h>

typedef struct hash_key_obj
{
   i32 vi, vti, vni;
} hash_key_obj;

typedef struct hash_key_gltf
{
   i32 vi, vti, vni;
} hash_key_gltf;

// ordered open addressing with linear probing
typedef u32 hash_value;

typedef struct index_hash_table
{
   u32* values;
   hash_key_obj* keys;
   size max_count;
   size count;
} index_hash_table;

static bool obj_key_equals(hash_key_obj a, hash_key_obj b)
{
   return memcmp(&a, &b, sizeof(hash_key_obj)) == 0;
}

static bool obj_key_less(hash_key_obj a, hash_key_obj b)
{
   return memcmp(&a, &b, sizeof(hash_key_obj)) < 0;
}

static inline bool obj_key_is_empty(hash_key_obj k)
{
   hash_key_obj empty = {-1, -1, -1};
   return memcmp(&k, &empty, sizeof(hash_key_obj)) == 0;
}

static u32 obj_hash_index(hash_key_obj k)
{
   u32 hash = 2166136261u;

#define HASH(f) do {          \
        u32 bits = (f);       \
        hash ^= bits;         \
        hash *= 16777619u;    \
    } while(0)

   HASH(k.vi); 
   HASH(k.vni); 
   HASH(k.vti);

#undef HASH

   return hash;
}

static u32 hash(const char* key)
{
   uint32_t result = 2166136261U;
   while(*key)
   {
      result *= 16777619U;
      result ^= (uint8_t)(*key);
      key++;
   }

   return result;
}

static void hash_insert(index_hash_table* table, hash_key_obj key, hash_value value)
{
   if(table->count == table->max_count)
      return;

   u32 index = obj_hash_index(key) % table->max_count;

   while(!obj_key_is_empty(table->keys[index]))
   {
      if(obj_key_equals(table->keys[index], key))
      {
         table->values[index] = value; // update
         return;
      }
      if(obj_key_less(key, table->keys[index]))
      {
         hash_key_obj tmp_key = table->keys[index];
         hash_value tmp_value = table->values[index];

         table->keys[index] = key;
         table->values[index] = value;

         key = tmp_key;
         value = tmp_value;
      }

      index = (index + 1) % table->max_count;
   }

   table->keys[index] = key;
   table->values[index] = value;
   table->count++;
}

static hash_value hash_lookup(index_hash_table* table, hash_key_obj key)
{
   u32 index = obj_hash_index(key) % table->max_count;

   while(!obj_key_is_empty(table->keys[index]) && obj_key_less(table->keys[index], key))
      index = (index + 1) % table->max_count;

   if(obj_key_equals(table->keys[index], key))
      return table->values[index];

   return ~0u;
}

static vk_shader_module spv_hash_lookup(spv_hash_table* table, const char* key)
{
   u32 index = hash(key) % table->max_count;
   u32 old_index = index;

   while(table->keys[index] && strcmp(table->keys[index], key) < 0)
   {
      index = (index + 1) % table->max_count;
      if(index == old_index) break; // wrap around
   }

   assert(index == old_index || !table->keys[index] || strcmp(table->keys[index], key) >= 0);

   if(table->keys[index] && strcmp(table->keys[index], key) == 0)
      return table->values[index];

   return (vk_shader_module){};
}

static void spv_hash_log(spv_hash_table* table)
{
   u32 index = 0;
   u32 count = 0;

   while(count != table->count)
   {
      if(table->keys[index])
      {
         const char* name = table->keys[index];
         VkShaderModule module = table->values[index].module;
         printf("Shader module '%s': \t'%p'\n", name, module);
         ++count;
      }
      index = (index + 1) % table->max_count;
   }
}

static void spv_hash_insert(spv_hash_table* table, const char* key, vk_shader_module value)
{
   if(table->count == table->max_count)
      return;

   u32 index = hash(key) % table->max_count;

   while(table->keys[index])
   {
      if(strcmp(table->keys[index], key) > 0)
      {
         const char* tmp_key = table->keys[index];
         vk_shader_module tmp_value = table->values[index];

         table->keys[index] = key;
         table->values[index] = value;

         key = tmp_key;
         value = tmp_value;
      }
      else if(strcmp(table->keys[index], key) == 0)
      {
         table->values[index] = value;
         return;
      }

      index = (index + 1) % table->max_count;
   }

   table->keys[index] = key;
   table->values[index] = value;
   table->count++;
}

static void spv_hash_function(vk_context* context, spv_hash_table* table, void(*p)(vk_device* devices, vk_shader_module shader))
{
   u32 index = 0;
   u32 count = 0;

   while(count != table->count)
   {
      if(table->keys[index])
      {
         p(&context->devices, table->values[index]);
         ++count;
      }
      index = (index + 1) % table->max_count;
   }
}
