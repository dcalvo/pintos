#ifndef FRAME_H
#define FRAME_H

#include <hash.h>
#include "vm/page.h"

struct hash frame_table;

struct frame_table_entry 
{
    void *addr;                     /* Frame. */
    struct thread *owner;           /* Owner thread of the frame. */
    struct page_table_entry *pte;   /* Page table entry. */

    struct hash_elem elem;          /* Element for frame table. */
};

unsigned frame_hash (const struct hash_elem *f_, void *aux);
bool frame_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux);

struct hash_elem * frame_install (struct frame_table_entry *fte,
         struct page_table_entry *pte, void *kpage);

#endif