#include "threads/malloc.h"
#include "vm/frame.h"
#include "vm/page.h"

void
frame_table_init (void)
{
  hash_init (&frame_table, frame_hash, frame_less, NULL);
}

/* Returns a hash value for frame f. */
unsigned
frame_hash (const struct hash_elem *f_, void *aux UNUSED)
{
  const struct frame_table_entry *f = hash_entry (f_, struct frame_table_entry,
                                                  hash_elem);
  return hash_bytes (&f->kpage, sizeof f->kpage);
}

/* Returns true if frame a precedes frame b. */
bool
frame_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct frame_table_entry *a = hash_entry (a_, struct frame_table_entry,
                                                  hash_elem);
  const struct frame_table_entry *b = hash_entry (b_, struct frame_table_entry,
                                                  hash_elem);
  return a->kpage < b->kpage;
}

/* Allocates a frame for the given page. */
struct frame_table_entry*
frame_alloc (struct page_table_entry *pte)
{
  void *kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (!kpage)
    page_evict ();
  struct frame_table_entry *fte = malloc (sizeof *fte);
  if (!fte)
    return NULL;
  fte->kpage = kpage; // store frame kpage
  fte->thread = thread_current (); // store frame thread
  fte->pte = pte; // store frame pte
  lock_init (&fte->lock);
  if (hash_insert (&frame_table, &fte->hash_elem)) {
    free (fte);
    free (kpage);
    return NULL;
  }
  return fte;
}

/* Acquire frame lock. */
void
frame_acquire (struct frame_table_entry *fte)
{
  lock_acquire (&fte->lock);
}

/* Release frame lock. */
void
frame_release (struct frame_table_entry *fte)
{
  lock_release (&fte->lock);
}
