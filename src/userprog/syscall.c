#include "userprog/syscall.h"
#include <stdio.h>
#include <list.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include <user/syscall.h>
#include "userprog/process.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#ifdef VM
#include "filesys/off_t.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/page.h"
#endif
static void syscall_handler (struct intr_frame *);
static struct file *thread_fd_get (int fd);
#ifdef VM
static mapid_t sys_mmap (int fd, void *addr);
static void sys_munmap (mapid_t mapid);
#endif

static struct lock filesys_lock;
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int syscall_nr;
  void *arg1 = (int *) f->esp + 1;
  void *arg2 = (int *) f->esp + 2;
  void *arg3 = (int *) f->esp + 3;
  int status;
  const char *file;

  int fd;
  void *rbuffer;
  const void *wbuffer;
#ifdef VM
  void *addr;
  mapid_t mapid;
#endif

  if (!is_user_vaddr ((int *) f->esp))
    sys_exit (-1);
  syscall_nr = *(int *) f->esp;
  switch (syscall_nr)
    {
#ifdef VM
    case SYS_MMAP:
      if (!is_user_vaddr (arg2))
        sys_exit (-1);
      fd = *(int *) arg1;
      addr = *(void **) arg2;
      f->eax = sys_mmap (fd, addr);
      break;
    case SYS_MUNMAP:
      if (!is_user_vaddr (arg1))
        sys_exit (-1);
      mapid = *(mapid_t *) arg1;
      sys_munmap (mapid);
      break;
#endif
    }
}
#ifdef VM
static mapid_t
sys_mmap (int fd, void *addr)
{
  struct thread *curr = thread_current ();
  struct page *page;
  struct file *file;
  off_t read_bytes, current_read_bytes;
  off_t current_ofs = 0;
  mapid_t mapid;

  /* File descriptors 0 and 1 are not mappable. */
  file = thread_fd_get (fd);
  if (file == NULL)
    return MAP_FAILED;

  /* ADDR should be page-aligned.
     Virtual page 0 is not mapped. */
  if (pg_ofs (addr) != 0 || addr == 0)
    return MAP_FAILED;

  /* File should have positive length. */
  filesys_acquire ();
  read_bytes = file_length (file);
  filesys_release ();
  if (read_bytes == 0)
    return MAP_FAILED;
  current_read_bytes = read_bytes;

  vm_frame_acquire ();
  mapid = curr->max_mapid++;
  while (current_read_bytes > 0)
    {
      if (vm_page_insert ((uint8_t *) addr + current_ofs) != NULL)
        {
          curr->max_mapid--;
          while (current_ofs > 0)
            {
              current_ofs -= PGSIZE;
              page = list_entry (list_pop_back (&curr->mmap_list),
                                 struct page, elem);
              hash_delete (&curr->page_table, &page->hash_elem);
              free (page);
            }
          vm_frame_release ();
          return MAP_FAILED;
        }
      page = vm_page_find (&curr->page_table, (uint8_t *) addr + current_ofs);
      page->loaded = false;
      page->mapid = mapid;
      page->file = file_reopen (file);
      page->file_ofs = current_ofs;
      page->file_read_bytes = (current_read_bytes < PGSIZE
                               ? current_read_bytes : PGSIZE);
      page->file_writable = true;
      list_push_back (&curr->mmap_list, &page->elem);

      current_read_bytes -= page->file_read_bytes;
      current_ofs += PGSIZE;
    }
  vm_frame_release ();

  return mapid;
}
#endif

#ifdef VM
static void
sys_munmap (mapid_t mapid)
{
  struct thread *curr = thread_current ();
  struct list_elem *e;
  struct page *page;
  void *kpage;

  vm_frame_acquire ();
  if (!list_empty (&curr->mmap_list))
    {
      e = list_front (&curr->mmap_list);
      while (e != list_end (&curr->mmap_list))
        {
          page = list_entry (e, struct page, elem);
          if (page->mapid < mapid)
            {
              e = list_next (e);
              continue;
            }
          else if (page->mapid > mapid)
            break;

          e = list_next (e);
          list_remove (&page->elem);
          kpage = pagedir_get_page (curr->pagedir, page->addr);
          if (kpage == NULL)
            {
              hash_delete (&curr->page_table, &page->hash_elem);
              free (page);
              continue;
            }

          if (pagedir_is_dirty (curr->pagedir, page->addr))
            {
              filesys_acquire ();
              file_write_at (page->file, page->addr, page->file_read_bytes,
                             page->file_ofs);
              filesys_release ();
            }

          pagedir_clear_page (curr->pagedir, page->addr);
          ASSERT (hash_delete (&curr->page_table, &page->hash_elem) != NULL);
          free (page);
          vm_frame_free (kpage);
        }
    }
  vm_frame_release ();
}
#endif
/* Returns the file pointer with given FD. */
static struct file *
thread_fd_get (int fd)
{
  struct thread *curr = thread_current ();
  struct thread_fd *tfd;
  struct list_elem *e;

  if (fd < 2 || fd >= curr->max_fd)
    return NULL;
  for (e = list_begin (&curr->fd_list); e != list_end (&curr->fd_list);
       e = list_next (e))
    {
      tfd = list_entry (e, struct thread_fd, elem);
      if (tfd->fd == fd)
        return tfd->file;
    }
  return NULL;
}
/* Acquire the filesys_lock to usage file system. */
void
filesys_acquire (void)
{
  lock_acquire (&filesys_lock);
}

/* Release the filesys_lock. */
void
filesys_release (void)
{
  lock_release (&filesys_lock);
}
void
sys_exit (int status)
{
  struct thread *curr = thread_current ();
  const char *name = thread_name ();
  size_t size = strlen (name) + 1;
  char *name_copy = (char *) malloc (size * sizeof (char));
  char *token, *save_ptr;

#if PRINT_DEBUG
  printf ("SYS_EXIT: status: %d\n", status);
#endif

  strlcpy (name_copy, name, size);
  token = strtok_r (name_copy, " ", &save_ptr);
  printf ("%s: exit(%d)\n", token, status);
  free (name_copy);

  curr->exit_status = status;
  thread_exit ();
}
