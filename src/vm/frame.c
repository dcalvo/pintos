#include "vm/frame.h"

/* Returns a hash value for frame f. */
unsigned
page_hash (const struct hash_elem *f_, void *aux UNUSED)
{
  const struct frame_table_entry *f = hash_entry (f_, struct frame_table_entry, elem);
  return hash_bytes (&f->addr, sizeof f->addr);
}

/* Returns true if frame a precedes frame b. */
bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct frame_table_entry *a = hash_entry (a_, struct frame_table_entry, elem);
  const struct frame_table_entry *b = hash_entry (b_, struct frame_table_entry, elem);

  return a->addr < b->addr;
}

struct hash_elem *
frame_install (struct frame_table_entry *fte, struct page_table_entry *pte,
             void *kpage)
{
    fte->addr = kpage; // store frame addr
    fte->owner = thread_current (); // store frame owner
    fte->pte = pte; // store frame pte
    return hash_insert (&frame_table, &fte->elem); // store fte
}