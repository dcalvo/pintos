#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include <string.h>
#include "userprog/pagedir.h"

/* Max user stack size. 8MB. */
#define USER_STACK (8 * 1024 * 1024)

static struct page_table_entry* page_get (void *address);
static bool page_read (struct page_table_entry *pte);
static void page_write (struct page_table_entry *pte);
static void page_init (struct page_table_entry *pte);

/* NOTE The following two functions (page_hash and page_less) were taken from
the class project guide! Specifically from A.8.5 Hash Table Examples. */
/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct page_table_entry *p = hash_entry (p_, struct page_table_entry, hash_elem);
  return hash_bytes (&p->addr, sizeof p->addr);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct page_table_entry *a = hash_entry (a_, struct page_table_entry, hash_elem);
  const struct page_table_entry *b = hash_entry (b_, struct page_table_entry, hash_elem);

  return a->addr < b->addr;
}

/* Evict a page and save it to swap. */
void
page_evict (void)
{
    struct page_table_entry *pte;
    // TODO eviction algo
    
    if (pte->dirty)
        page_write (pte);
    
    pagedir_clear_page (pte->fte->thread->pagedir, pte->addr);
    free (pte->fte);
    pte->fte = NULL;
}

/* Write data to swap. */
static void
page_write (struct page_table_entry *pte)
{
    frame_acquire (pte->fte);
    swap_write (pte->fte);
    frame_release (pte->fte);
}

/* Given an address, load the page into memory and return success,
otherwise return a load failure and kill thread. */
bool
page_load (void *fault_addr)
{
    if (!fault_addr)
        return false;

    /* Retrieve (or allocate) the page. */
    struct page_table_entry *pte = page_get (fault_addr);
    if (!pte)
        return false;
    
    /* Allocate a frame. */
    struct frame_table_entry *fte = frame_alloc (pte);
    if (!fte)
        return false;
    
    /* Load data into the page. */
    pte->fte = fte;
    if (!page_read (pte))
        return false;
    
    /* Install the page into frame. */
    if(!install_page (pte->addr, fte->kpage, pte->writable))
        return false;
    
    pte->accessed = true;
    return true;
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

/* Given an address, get the page associated with it or return NULL.
Allocates new pages as necessary. */
static struct page_table_entry*
page_get (void *vaddr)
{
    if (!is_user_vaddr (vaddr))
        return NULL;

    struct thread *t = thread_current();
    struct page_table_entry pte;
    pte.addr = pg_round_down (vaddr);
    struct hash_elem *elem = hash_find (&t->page_table, &pte.hash_elem);
    if (elem)
        return hash_entry (elem, struct page_table_entry, hash_elem);
    /* Checking that the page address is inside max stack size
     and at most 32 bytes away. */
    else if (PHYS_BASE - USER_STACK <= pte.addr && t->esp - 32 <= vaddr)
        return page_alloc (pte.addr, true);
    else
        return NULL;
}

/* Given an address, allocate an entry in the page table (without loading) */
struct page_table_entry*
page_alloc (void *vaddr, bool writable)
{
    struct page_table_entry *pte = malloc (sizeof *pte);
    if (!pte)
        return NULL;
    page_init (pte);
    pte->addr = pg_round_down (vaddr);
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
    pte->dirty = false;
    pte->accessed = false;
    
    pte->file = NULL;
    pte->file_ofs = 0;
    pte->file_bytes = 0;

    pte->swapped = false;
    pte->sector = -1;
}

/* Frees the page associated with the given address. */
void page_free (void *address)
{
    struct page_table_entry *pte = page_get (address);
    ASSERT (pte)

    // TODO free page and frame
}
