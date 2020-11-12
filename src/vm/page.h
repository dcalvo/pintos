#ifndef PAGE_H
#define PAGE_H
#include <hash.h>
#include "vm/frame.h"
#include "filesys/off_t.h"

unsigned page_hash (const struct hash_elem *p_, void *aux);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux);

struct page_table_entry* page_load (void *fault_addr);
struct page_table_entry* page_alloc (void *address, bool writable);
void page_free (void *address);

struct page_table_entry
{
    void *addr;                     /* Virtual address. */
    bool dirty;                     /* Dirty bit. Set when write. */
    bool accessed;                  /* Accessed bit. Set when read/write. */
    bool writable;                  /* Writable bit. Set at allocation. */

    struct file *file;              /* File page pointer. */
    off_t file_ofs;                 /* File access offset. */
    size_t file_bytes;              /* File bytes to read. */

    struct frame_table_entry *fte;   /* Associated frame table entry. */
    struct hash_elem hash_elem;     /* Hash element for page table. */
};
#endif