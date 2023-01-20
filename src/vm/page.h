#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <hash.h>
#include <list.h>
#include <user/syscall.h>
#include "filesys/file.h"
#include "filesys/off_t.h"

/* Page. */
struct page
  {
    void *addr;                         /* Virtual address. */
    bool loaded;                        /* Page is loaded. */
    mapid_t mapid;                      /* Mapping identifier. */
    struct file *file;                  /* Loaded file. */
    off_t file_ofs;                     /* Offset of the file. */
    uint32_t file_read_bytes;           /* Number of read bytes from file. */
    bool file_writable;                 /* File is writable. */
    bool valid;                         /* Frame is not swapped out. */
    size_t swap_idx;                    /* Swap index of the frame. */
    struct hash_elem hash_elem;         /* Hash table element. */
    struct list_elem elem;              /* List element. */
  };

bool vm_page_init (struct hash *page_table);
struct page *vm_page_insert (const void *address);
struct page *vm_page_find (struct hash *page_table, const void *address);
void vm_page_destroy (struct hash *page_table);
bool vm_page_load_swap (struct page *page);
bool vm_page_load_file (struct page *page);
bool vm_page_load_zero (struct page *page);

#endif /* vm/page.h */
