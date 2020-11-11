#ifndef PAGE_H
#define PAGE_H
#include <hash.h>

unsigned page_hash (const struct hash_elem *p_, void *aux);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux);

bool page_load (void *fault_addr);

struct page
{
    void *addr;                     /* Virtual address. */
    struct hash_elem hash_elem;     /* Hash element for page table. */
};
#endif