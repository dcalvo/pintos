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

/* Frame table initialization. */
void
frame_table_init (void)
{
    lock_init (&frame_table_lock);
    hash_init (&frame_table, frame_hash, frame_less, NULL);
}

/* Allocates a frame for the given page. */
struct frame_table_entry*
frame_alloc (struct page_table_entry *pte)
{
    void *kpage = palloc_get_page (PAL_USER | PAL_ZERO);
    if (!kpage)
        kpage = page_evict ();
    struct frame_table_entry *fte = malloc (sizeof *fte);
    if (!fte)
        return NULL;
    
    fte->addr = kpage; // store frame addr
    fte->owner = thread_current (); // store frame owner
    fte->pte = pte; // store frame pte
    lock_init (&fte->lock);
    if (!hash_insert (&frame_table, &fte->elem))
        return fte;

    frame_free (fte);
    return NULL;
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

/* Find a frame to evict from the frame table. 
    Uses a two-handed clock algorithm.
    The hands of the clock are placed randomly somewhere in the frame table.
    Distance between two hands is total # of frames / HAND_SPREAD pages.*/
#define HAND_SPREAD (4)
static struct page_table_entry *
frame_evict (void)
{
    lock_acquire (&frame_table_lock);

    /* Dirty runtime initialization of frame_table_size. */
    static int frame_table_size = -1;
    if (frame_table_size == -1)
        frame_table_size = hash_size (&frame_table);
    
    struct frame_table_entry *first_fte = NULL;
    struct frame_table_entry *second_fte = NULL;
    struct hash_iterator it;
    
    hash_first (&it, &frame_table);
    second_fte = hash_entry (hash_cur (&it), struct frame_table_entry, elem);
    for (int i = 0; i < frame_table_size / HAND_SPREAD; i++)
        hash_next (&it);
    first_fte = hash_entry (hash_cur (&it), struct frame_table_entry, elem);
    if (!first_fte || !second_fte)
        PANIC ("FRAME EVICTION FAILURE");

    /* FRAME_TABLE_SIZE should be equal to total # of frames. */
    /* FIRST_FTE should be (FRAME_TABLE_SIZE / HAND_SPREAD) pages in front of 
        SECOND_FTE. */

    struct frame_table_entry *victim = NULL;
    while (!victim) {
        if (first_fte->pte->accessed)
            first_fte->pte->accessed = false;
        if (!second_fte->pte->accessed)
            victim = second_fte;
        if (!hash_next (&it))
            hash_first (&it, &frame_table);

        second_fte = first_fte;
        first_fte = hash_entry (hash_cur (&it), struct frame_table_entry, elem);
    }

    lock_release (&frame_table_lock);
    struct page_table_entry *evicted_pte = victim->pte;
    frame_free (victim); // goodbye friend

    return evicted_pte;
}

void
frame_free (struct frame_table_entry *fte)
{
    frame_acquire (fte);
    lock_acquire (&frame_table_lock);

    hash_delete (&frame_table, &fte->elem);
    palloc_free_page (fte->addr);
    free (fte);

    lock_release (&frame_table_lock);
    // no need to release destroyed fte's lock
}