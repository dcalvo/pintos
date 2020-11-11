#include <hash.h>

bool page_load (void *fault_addr);

struct page
{
    void *addr;                     /* Virtual address. */
    struct hash_elem hash_elem;     /* Hash element for page table. */
};