#include "vm/page.h"
#include "vm/frame.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include <string.h>

/* Max user stack size. 8MB. */
#define USER_STACK (8 * 1024 * 1024)

static struct page_table_entry* page_get (void *address);
static bool page_read (struct page_table_entry *pte);

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


/* Given an address, load the page into memory and return success,
otherwise return a load failure and kill thread. */
struct page_table_entry*
page_load (void *fault_addr)
{
    struct thread *t = thread_current();
    struct page_table_entry *pte;
    struct frame_table_entry *fte;

    if (!&t->page_table || !fault_addr)
        return false;
    
    /* Retrieve (or allocate) the page. */
    pte = page_get (fault_addr);
    if (!pte)
        return false;
    
    /* Allocate a frame. */
    fte = frame_alloc (pte);
    if (!fte)
        return false;
    
    /* Load data into the page. */
    pte->fte = fte;
    if (!page_read (pte))
        return false;
    
    /* Install the page into frame. */
    if(!install_page (pte->addr, fte->addr, pte->writable))
        return false;
    
    return pte;
}

/* Read stored data into pages. */
static bool
page_read (struct page_table_entry *pte)
{
    struct frame_table_entry *fte = pte->fte;
    frame_acquire (fte);
    if (0) {
        return false; // TODO swap hook
    } else if (pte->file) {
        /* Load from file. */
        if (file_read_at (pte->file, fte->addr, pte->file_bytes, pte->file_ofs)
            != (int) pte->file_bytes) {
                frame_release (fte);
                return false; 
            }
        memset (fte->addr + pte->file_bytes, 0, (PGSIZE - pte->file_bytes));
    } else {
        memset (fte->addr, 0, PGSIZE);
    }
    frame_release (fte);
    return true;
}

/* Given an address, get the page associated with it or return NULL.
Allocates new pages as necessary. */
static struct page_table_entry*
page_get (void *address)
{
    if (address >= PHYS_BASE)
        return NULL;
    
    struct thread *t = thread_current();
    struct page_table_entry pte;
    struct hash_elem *elem;

    pte.addr = (void *) pg_round_down (address);
    elem = hash_find (&t->page_table, &pte.hash_elem);
    if (elem)
        return hash_entry(elem, struct page_table_entry, hash_elem);
    else {
        /* Checking that the page address is inside max stack size
         and at most 32 bytes away. */
        if (pte.addr > PHYS_BASE - USER_STACK && 
            address >= t->esp - 32)
            return page_alloc (pte.addr, true);
    }

    return NULL;
}

/* Given an address, allocate an entry in the page table (without loading) */
struct page_table_entry*
page_alloc (void *address, bool writable)
{
    void *upage = pg_round_down (address);
    struct page_table_entry *pte = malloc (sizeof *pte);
    if (pte) {
        pte->addr = upage;
        pte->writable = writable;
        if (!hash_insert (&thread_current ()->page_table, &pte->hash_elem)) {
            return pte;
        }
        free (pte);
    }
    return NULL;
}

/* Frees the page associated with the given address. */
void page_free (void *address)
{
    struct page_table_entry *pte = page_get (address);
    ASSERT (pte)

    // TODO free page and frame
}
