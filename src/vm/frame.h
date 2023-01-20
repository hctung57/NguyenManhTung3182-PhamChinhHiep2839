#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "threads/palloc.h"
#include "threads/thread.h"

/* Frame. */
struct frame
  {
    struct thread *thread;              /* Thread. */
    void *addr;                         /* Kernel virtual address. */
    void *upage;                        /* User virtual address. */
    struct list_elem elem;              /* List element. */
  };

void vm_frame_init (void);
void *vm_frame_alloc (void *upage, enum palloc_flags);
void vm_frame_free (void *page);
void *vm_frame_evict (enum palloc_flags);
void vm_frame_acquire (void);
void vm_frame_release (void);

#endif /* vm/frame.h */
