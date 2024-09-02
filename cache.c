#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cache.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;

int cache_create(int num_entries)
{
  if (!cache && num_entries >= 2 && num_entries <= 4096)
  {
    cache_size = num_entries;
    cache = calloc(cache_size, sizeof(cache_entry_t));
    return 1;
  }
  return -1;
}

int cache_destroy(void)
{
  if (!cache)
  {
    return -1;
  }
  free(cache);
  cache = NULL;
  cache_size = 0;
  return 1;
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf)
{
  num_queries++;

  if (!cache)
  {
    return -1;
  }
  if (buf == NULL)
  {
    return -1;
  }
  for (int i = 0; i < cache_size; i++)
  {
    if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num)
    {
      memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE);
      num_hits++;
      clock++;
      cache[i].access_time = clock;
      return 1;
    }
  }
  return -1;
}

void cache_update(int disk_num, int block_num, const uint8_t *buf)
{

  for (int i = 0; i < cache_size; i++)
  {
    if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num)
    {
      memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
      clock++;
      cache[i].access_time = clock;
    }
  }
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf)
{
  if (!cache)
  {
    return -1;
  }
  if (buf == NULL)
  {
    return -1;
  }
  if (disk_num > JBOD_NUM_DISKS || disk_num < 0)
  {
    return -1;
  }
  if (block_num > JBOD_DISK_SIZE || block_num < 0)
  {
    return -1;
  }

  cache_entry_t new_cache;
  new_cache.disk_num = disk_num;
  new_cache.block_num = block_num;

  for (int i = 0; i < cache_size; i++)
  {
    if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num)
    {
      return -1;
    }
  }

  for (int i = 0; i < cache_size; i++)
  {
    if (!cache[i].valid)
    {
      memcpy(new_cache.block, buf, JBOD_BLOCK_SIZE);
      cache[i] = new_cache;
      clock++;
      cache[i].access_time = clock;
      cache[i].valid = true;
      return 1;
    }
  }

  // find smallest access_time and overwrite it
  int smallest_access_time = cache[0].access_time;
  int lsu = 0;
  for (int i = 0; i < cache_size; i++)
  {
    if (smallest_access_time > cache[i].access_time)
    {
      smallest_access_time = cache[i].access_time; // smallest access_time
      lsu = i;
    }
  }
  memcpy(cache[lsu].block, buf, JBOD_BLOCK_SIZE);
  cache[lsu].disk_num = disk_num;
  cache[lsu].block_num = block_num;
  cache[lsu].valid = true;
  clock++;
  cache[lsu].access_time = clock;
  return 1;
}

bool cache_enabled(void)
{
  if (cache)
  {
    return true;
  }
  return false;
}

void cache_print_hit_rate(void)
{
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float)num_hits / num_queries);
}