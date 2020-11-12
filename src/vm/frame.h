#ifndef FRAME_H
#define FRAME_H

#include <hash.h>
#include "vm/page.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/palloc.h"

struct hash frame_table;

struct frame_table_entry 
{
  void *kpage;                    /* Frame. */
  struct thread *thread;          /* Owner thread of the frame. */
  struct page_table_entry *pte;   /* Page table entry. */

  struct hash_elem elem;          /* Element for frame table. */
  struct lock lock;               /* Lock. */
};

void frame_table_init (void);
unsigned frame_hash (const struct hash_elem *f_, void *aux);
bool frame_less (const struct hash_elem *a_, const struct hash_elem *b_,
                 void *aux);

struct frame_table_entry* frame_alloc (struct page_table_entry *pte);
void frame_acquire (struct frame_table_entry *pte);
void frame_release (struct frame_table_entry *fte);

#endif
