#include "threads/malloc.h"
#include "vm/frame.h"
#include "vm/page.h"

/* Frame table initialization. */
void
frame_table_init (void)
{
  hash_init (&frame_table, frame_hash, frame_less, NULL);
  lock_init (&frame_table_lock);
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
  if (!kpage) {
    page_evict (NULL);
    kpage = palloc_get_page (PAL_USER | PAL_ZERO);
    if (!kpage)
      PANIC ("PAGE EVICTION FAILED");
  }
  struct frame_table_entry *fte = malloc (sizeof *fte);
  if (!fte)
    return NULL;
  fte->kpage = kpage; // store frame kpage
  fte->thread = thread_current (); // store frame thread
  fte->pte = pte; // store frame pte
  lock_init (&fte->lock);

  lock_acquire (&frame_table_lock);
  if (hash_insert (&frame_table, &fte->hash_elem)) {
    free (fte);
    pte->fte = NULL;
    palloc_free_page (kpage);
    lock_release (&frame_table_lock);
    return NULL;
  }
  lock_release (&frame_table_lock);
  return fte;
}

void
frame_free (struct frame_table_entry *fte)
{
  ASSERT (lock_held_by_current_thread (&fte->lock));
  lock_acquire (&frame_table_lock);

  hash_delete (&frame_table, &fte->hash_elem);
  palloc_free_page (fte->kpage);
  free (fte);

  lock_release (&frame_table_lock);
  // no need to release destroyed fte's lock
}

/* Find a frame to evict from the frame table. 
    Uses a two-handed clock algorithm.
    The hands of the clock are placed randomly somewhere in the frame table.
    Distance between two hands is total # of frames / HAND_SPREAD pages.*/
#define HAND_SPREAD (4)
struct page_table_entry *
frame_victim (void)
{
  lock_acquire (&frame_table_lock);

  /* Dirty runtime initialization of frame_table_size. */
  static int frame_table_size = -1;
  if (frame_table_size == -1)
    frame_table_size = hash_size (&frame_table);

  struct hash_iterator it;
  hash_first (&it, &frame_table);
  struct frame_table_entry *fte_2 = hash_entry (hash_next (&it),
                                                struct frame_table_entry,
                                                hash_elem);
  for (int i = 0; i < frame_table_size / HAND_SPREAD; i++)
    hash_next (&it);
  struct frame_table_entry *fte_1 = hash_entry (hash_cur (&it),
                                                struct frame_table_entry,
                                                hash_elem);
  if (!fte_1 || !fte_2)
    PANIC ("FRAME EVICTION FAILURE");

  /* FRAME_TABLE_SIZE should be equal to total # of frames. */
  /* FTE_1 should be (FRAME_TABLE_SIZE / HAND_SPREAD) pages in front of 
      FTE_2. */

  struct frame_table_entry *fte_victim = NULL;
  while (!fte_victim) {
    if (fte_1->pte->accessed)
      fte_1->pte->accessed = false;
    if (!fte_2->pte->accessed)
      fte_victim = fte_2;
    if (!hash_next (&it))
      hash_first (&it, &frame_table);

    fte_2 = fte_1;
    fte_1 = hash_entry (hash_cur (&it), struct frame_table_entry, hash_elem);
  }

  lock_release (&frame_table_lock);

  return fte_victim->pte;
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
