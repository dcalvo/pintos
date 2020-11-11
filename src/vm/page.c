#include "vm/page.h"
#include "vm/frame.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "userprog/process.h"

/* Max user stack size. 8MB. */
#define USER_STACK (8 * 1024 * 1024)

static struct page_table_entry* page_get (void *address);
static struct page_table_entry* page_alloc (void *address, bool writable);

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
bool
page_load (void *fault_addr)
{
    struct thread *t = thread_current();
    struct page_table_entry *p;

    if (&t->page_table == NULL || hash_empty (&t->page_table))
        return false;
    if (fault_addr == NULL)
        return false;
    
    p = page_get (fault_addr);
    if (p == NULL)
        return false;
    
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
        if (pte.addr > (void *) PHYS_BASE - USER_STACK && 
            pte.addr <= (void *) t->esp + 32)
            return page_alloc (pte.addr, true);
    }

    return NULL;
}

/* Given an address, attempt to install a frame to act as a page for caller. */
static struct page_table_entry*
page_alloc (void *address, bool writable)
{
    void *kpage = palloc_get_page (PAL_USER | PAL_ZERO);
    void *upage = pg_round_down (address);
    if (install_page (upage, kpage, writable)) {
        struct page_table_entry *pte = malloc (sizeof *pte);
        struct frame_table_entry *fte = malloc (sizeof *fte);
        if (pte && fte) {
            pte->addr = upage;
            if (!hash_insert (&thread_current ()->page_table, &pte->hash_elem)) {
                fte->addr = kpage; // store frame addr
                fte->owner = thread_current (); // store frame owner
                fte->pte = pte; // store frame pte
                list_insert (&frame_table, &fte->elem); // store fte
                return pte;
            }
            free (pte);
            free (fte);
        }
    }
    return NULL;
}