#include "vulkan_shader_module.h"

typedef struct hash_key
{
   i32 vi, vti, vni;
} hash_key;

// Ordered open addressing with linear probing
typedef u32 hash_value;
typedef struct index_hash_table
{
   u32* values;
   hash_key* keys;
   size max_count;
   size count;
} index_hash_table;

#define index_hash_table(T) struct index_hash_table##T {   u32* values; T* keys; size max_count; size count; }

typedef struct 
{
   vk_shader_modules* values;
   const char** keys;
   size max_count;
   size count;
} spv_hash_table;

static bool key_equals(hash_key a, hash_key b)
{
   return memcmp(&a, &b, sizeof(hash_key)) == 0;
}

static bool key_less(hash_key a, hash_key b)
{
   return memcmp(&a, &b, sizeof(hash_key)) < 0;
}

static inline bool key_is_empty(hash_key k)
{
   hash_key empty = {-1, -1, -1};
   return memcmp(&k, &empty, sizeof(hash_key)) == 0;
}

static u32 hash_index(hash_key k)
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

static u32 spv_hash(const char* key)
{
   uint32_t hash = 2166136261U;
   while(*key)
   {
      hash *= 16777619U;
      hash ^= (uint8_t)(*key);
      key++;
   }

   return hash;
}

static void hash_insert(index_hash_table* table, hash_key key, hash_value value)
{
   if(table->count == table->max_count)
      return;

   u32 index = hash_index(key) % table->max_count;

   while(!key_is_empty(table->keys[index]))
   {
      if(key_equals(table->keys[index], key))
      {
         table->values[index] = value; // update
         return;
      }
      if(key_less(key, table->keys[index]))
      {
         hash_key tmp_key = table->keys[index];
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

static hash_value hash_lookup(index_hash_table* table, hash_key key)
{
   u32 index = hash_index(key) % table->max_count;

   while(!key_is_empty(table->keys[index]) && key_less(table->keys[index], key))
      index = (index + 1) % table->max_count;

   if(key_equals(table->keys[index], key))
      return table->values[index];

   return ~0u;
}

static vk_shader_modules spv_hash_lookup(spv_hash_table* table, const char* key)
{
   u32 index = spv_hash(key) % table->max_count;

   while(table->keys[index] && strcmp(table->keys[index], key) < 0)
      index = (index + 1) % table->max_count;

   if(table->keys[index] && strcmp(table->keys[index], key) == 0)
      return table->values[index];

   return (vk_shader_modules){};
}

static void spv_hash_insert(spv_hash_table* table, const char* key, vk_shader_modules value)
{
   if(table->count == table->max_count)
      return;

   u32 index = spv_hash(key) % table->max_count;

   while(table->keys[index])
   {
      if(strcmp(table->keys[index], key) > 0)
      {
         const char* tmp_key = table->keys[index];
         vk_shader_modules tmp_value = table->values[index];

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
