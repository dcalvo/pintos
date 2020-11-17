#include <string.h>
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

/* Max user stack size. 8MB. */
#define USER_STACK (8 * 1024 * 1024)

static void page_init (struct page_table_entry *pte);
static bool page_read (struct page_table_entry *pte);
static void page_write (struct page_table_entry *pte);

/* NOTE The following two functions (page_hash and page_less) were taken from
the class project guide! Specifically from A.8.5 Hash Table Examples. */
/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct page_table_entry *p = hash_entry (p_, struct page_table_entry,
                                                 hash_elem);
  return hash_bytes (&p->upage, sizeof p->upage);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct page_table_entry *a = hash_entry (a_, struct page_table_entry,
                                                 hash_elem);
  const struct page_table_entry *b = hash_entry (b_, struct page_table_entry,
                                                 hash_elem);
  return a->upage < b->upage;
}

/* Given an address, load the page into memory and return success,
otherwise return a load failure and kill thread. */
struct page_table_entry *
page_load (const void *fault_addr)
{
  if (!fault_addr)
    return NULL;

  /* Retrieve (or allocate) the page. */
  struct page_table_entry *pte = page_get (fault_addr, true);
  if (!pte)
    return NULL;

  if (pte->fte)
    return pte; // page is already installed

  /* Allocate a frame. */
  struct frame_table_entry *fte = frame_alloc (pte);
  if (!fte) {
    return NULL;
  }

  /* Load data into the page. */
  pte->fte = fte;
  if (!page_read (pte)) {
    frame_free (fte);
    pte->fte = NULL;
    return NULL;
  }

  /* Install the page into frame. */
  frame_acquire (fte);
  if (!install_page (pte->upage, fte->kpage, pte->writable)) {
    frame_release (fte);
    frame_free (fte);
    pte->fte = NULL;
    return NULL;
  }
  frame_release (fte);

  pte->accessed = true;
  return pte;
}

/* Given an address, get the page associated with it or return NULL.
Allocates new pages as necessary, if stack is true. */
struct page_table_entry *
page_get (const void *vaddr, bool stack)
{
  if (!is_user_vaddr (vaddr))
    return NULL;

  struct thread *t = thread_current();
  struct page_table_entry pte;
  pte.upage = pg_round_down (vaddr);
  struct hash_elem *elem = hash_find (&t->page_table, &pte.hash_elem);
  if (elem)
    return hash_entry (elem, struct page_table_entry, hash_elem);
  /* Checking that the page address is inside max stack size
   and at most 32 bytes away. */
  else if (stack && PHYS_BASE - USER_STACK <= pte.upage
           && t->esp - 32 <= vaddr)
    return page_alloc (pte.upage, true);
  else
    return NULL;
}

/* Given an address, allocate an entry in the page table (without loading) */
struct page_table_entry *
page_alloc (const void *vaddr, bool writable)
{
  struct page_table_entry *pte = malloc (sizeof *pte);
  if (!pte)
    return NULL;
  page_init (pte);
  pte->upage = pg_round_down (vaddr);
  pte->writable = writable;
  if (hash_insert (&thread_current ()->page_table, &pte->hash_elem)) {
    free (pte);
    return NULL;
  }
  return pte;
}

/* Page init. */
static void
page_init (struct page_table_entry *pte)
{
  pte->thread = thread_current ();

  pte->accessed = false;
  pte->dirty = false;

  pte->swapped = false;
  pte->sector = -1;

  pte->file = NULL;
  pte->file_ofs = 0;
  pte->file_bytes = 0;

  pte->mapped = false;

  pte->fte = NULL;
}

/* Read stored data into pages. */
static bool
page_read (struct page_table_entry *pte)
{
  struct frame_table_entry *fte = pte->fte;
  frame_acquire (fte);
  if (pte->swapped)
    swap_read (fte);
  else if (pte->file) {
    /* Load from file. */
    if (file_read_at (pte->file, fte->kpage, pte->file_bytes, pte->file_ofs)
        != (int) pte->file_bytes) {
      frame_release (fte);
      return false;
    }
    memset (fte->kpage + pte->file_bytes, 0, (PGSIZE - pte->file_bytes));
  } else
    memset (fte->kpage, 0, PGSIZE);
  frame_release (fte);
  return true;
}

/* Evict a page and save it to swap. */
void
page_evict (struct page_table_entry *pte)
{
    if (!pte->fte)
        return;

  /* Locate the frame victim. */
  if (!pte)
    pte = frame_victim ();

  /* Write to swap if necessary. */
  if (!pte->dirty)
    pte->dirty = pagedir_is_dirty (pte->thread->pagedir, pte->upage);
  if (pte->dirty && !pte->swapped)
    page_write (pte);

  /* Re-enable page faults for this address. */
  pagedir_clear_page (pte->thread->pagedir, pte->upage);

  /* Uninstall the frame. */
  if (pte->fte)
    frame_free (pte->fte);
  pte->fte = NULL;
}

/* Write data to swap. */
static void
page_write (struct page_table_entry *pte)
{
  frame_acquire (pte->fte);
  if (pte->mapped && pte->file) {
    file_write_at (pte->file, pte->fte->kpage, pte->file_bytes, pte->file_ofs);
    pte->mapped = false;
  } else
    swap_write (pte->fte);
  frame_release (pte->fte);
}
