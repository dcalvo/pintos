#include <hash.h>

bool page_load (void *fault_addr);
unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED);

struct page
{
    void *addr;                     /* Virtual address. */
    struct hash_elem hash_elem;     /* Hash element for page table. */
};