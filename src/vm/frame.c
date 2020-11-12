#include "vm/frame.h"
#include "vm/page.h"
#include "threads/malloc.h"

/* Returns a hash value for frame f. */
unsigned
frame_hash (const struct hash_elem *f_, void *aux UNUSED)
{
  const struct frame_table_entry *f = hash_entry (f_, struct frame_table_entry, elem);
  return hash_bytes (&f->addr, sizeof f->addr);
}

/* Returns true if frame a precedes frame b. */
bool
frame_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct frame_table_entry *a = hash_entry (a_, struct frame_table_entry, elem);
  const struct frame_table_entry *b = hash_entry (b_, struct frame_table_entry, elem);

  return a->addr < b->addr;
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
    fte->addr = kpage; // store frame addr
    fte->owner = thread_current (); // store frame owner
    fte->pte = pte; // store frame pte
    lock_init (&fte->lock);
    if (hash_insert (&frame_table, &fte->elem)) {
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

void
frame_table_init (void)
{
    hash_init (&frame_table, frame_hash, frame_less, NULL);
}
