#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "vm/page.h"

/* Max user stack size. 8MB. */
#define USER_STACK (8 * 1024 * 1024)

/* NOTE The following two functions (page_hash and page_less) were taken from
the class project guide! Specifically from A.8.5 Hash Table Examples. */
/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct page *p = hash_entry (p_, struct page, hash_elem);
  return hash_bytes (&p->addr, sizeof p->addr);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct page *a = hash_entry (a_, struct page, hash_elem);
  const struct page *b = hash_entry (b_, struct page, hash_elem);

  return a->addr < b->addr;
}


/* Given an address, load the page into memory and return success,
otherwise return a load failure and kill thread. */
bool
page_load (void *fault_addr)
{
    struct thread *t = thread_current();
    struct page *p;

    if (&t->pagetable == NULL || hash_empty (&t->pagetable))
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
static struct page*
page_get (void *address)
{
    if (address >= PHYS_BASE)
        return NULL;
    
    struct thread *t = thread_current();
    struct page p;
    struct hash_elem *elem;

    p.addr = (void *) pg_round_down (address);
    elem = hash_find (&t->pagetable, &p.hash_elem);
    if (elem)
        return hash_entry(elem, struct page, hash_elem);
    else {
        /* Checking that the page address is inside max stack size
         and at most 32 bytes away. */
        if (p.addr > (void *) PHYS_BASE - USER_STACK && 
            p.addr <= (void *) t->esp + 32)
            return page_alloc (p.addr, true);
    }

    return NULL;
}

/* Given an address, attempt to install a frame to act as a page for caller. */
static struct page*
page_alloc (void *address, bool writable)
{
    void *kpage = palloc_get_page (PAL_USER | PAL_ZERO);
    void *upage = pg_round_down (address);
    if (install_page (upage, kpage, writable)) {
        struct page *p = malloc (sizeof *p);
        if (p) {
            p->addr = upage;
            if (!hash_insert (&thread_current ()->pagetable, &p->hash_elem))
                return p;
            free (p);
        }
    }
    return NULL;
}