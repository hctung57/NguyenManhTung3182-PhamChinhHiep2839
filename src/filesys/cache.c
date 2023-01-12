#include <debug.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/synch.h"

#define BUFFER_CACHE_SIZE 64

struct buffer_cache_entry
{
    bool used; // true if entry in use

    block_sector_t disk_sector;
    uint8_t buffer[BLOCK_SECTOR_SIZE];

    bool dirty;  // dirty bit
    bool access; // reference bit, for clock algorithm
};

/* Buffer cache entries. */
static struct buffer_cache_entry cache[BUFFER_CACHE_SIZE];

/* A global lock for synchronizing buffer cache operations. */
static struct lock buffer_cache_lock;

void buffer_cache_init()
{
    lock_init(&buffer_cache_lock);
    size_t i;
    for (i = 0; i < BUFFER_CACHE_SIZE; ++i)
    {
        cache[i].used = false;
    }
}

void write_buffer_cache_to_disk(struct buffer_cache_entry *entry)
{
    ASSERT(lock_held_by_current_thread(&buffer_cache_lock));
    ASSERT(entry != NULL && entry->used == true);
    if (entry->dirty == true)
    {
        block_write(fs_device, entry->disk_sector, entry->buffer);
        entry->dirty = false;
    }
} 

void buffer_cache_close ()
{
  // flush buffer cache entries
  lock_acquire (&buffer_cache_lock);

  size_t i;
  for (i = 0; i < BUFFER_CACHE_SIZE; ++ i)
  {
    if (cache[i].used == false) continue;
    write_buffer_cache_to_disk( &(cache[i]) );
  }

  lock_release (&buffer_cache_lock);
}

static struct buffer_cache_entry *buffer_cache_lookup(block_sector_t sector)
{
    size_t i;
    for (i = 0; i < BUFFER_CACHE_SIZE; ++i)
    {
        if (cache[i].used == false)
            continue;
        if (cache[i].disk_sector == sector)
        {
            return &cache[i];
        }
    }
    return NULL;
}

static struct buffer_cache_entry *buffer_cache_get_slot()
{
    ASSERT(lock_held_by_current_thread(&buffer_cache_lock));

    // clock algorithm
    static size_t clock = 0;
    while (true)
    {
        if (cache[clock].used == false)
        {
            return &(cache[clock]);
        }
        if (cache[clock].access == true)
        {
            // give a second
            cache[clock].access = false;
        }
        else
            break;
        clock++;
        clock %= BUFFER_CACHE_SIZE;
    }

    // clean cache[clock] in use and not access
    struct buffer_cache_entry *slot = &cache[clock];
    if (slot->dirty == true)
    {
        write_buffer_cache_to_disk(slot);
    }
    slot->used = false;
    return slot;
}

void buffer_cache_read(block_sector_t sector, void *target)
{
    lock_acquire(&buffer_cache_lock);
    struct buffer_cache_entry *sector_block = buffer_cache_lookup(sector);
    if (sector_block == NULL)
    {
        sector_block = buffer_cache_get_slot();
        ASSERT(sector_block != NULL && sector_block->used == false);
        sector_block->used = true;
        sector_block->disk_sector = sector;
        sector_block->dirty = false;
        block_read(fs_device, sector, sector_block->buffer);
    }

    sector_block->access = true;
    memcpy(target, sector_block->buffer, BLOCK_SECTOR_SIZE);
    lock_release(&buffer_cache_lock);
}

void buffer_cache_write (block_sector_t sector, const void *source)
{
  lock_acquire (&buffer_cache_lock);

  struct buffer_cache_entry *sector_block = buffer_cache_lookup (sector);
  if (sector_block == NULL) {
    // cache miss: need eviction.
    sector_block = buffer_cache_evict ();
    ASSERT (sector_block != NULL && sector_block->used == false);

    // fill in the cache entry.
    sector_block->used = true;
    sector_block->disk_sector = sector;
    sector_block->dirty = false;
    block_read (fs_device, sector, sector_block->buffer);
  }

  // copy the data form memory into the buffer cache.
  sector_block->access = true;
  sector_block->dirty = true;
  memcpy (sector_block->buffer, source, BLOCK_SECTOR_SIZE);

  lock_release (&buffer_cache_lock);
}