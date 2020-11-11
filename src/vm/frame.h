#ifndef FRAME_H
#define FRAME_H

#include <list.h>

struct list frame_table;

struct frame_table_entry 
{
    void *addr;             /* Frame. */
    struct thread *owner;   /* Owner thread of the frame. */
    void *pte;              /* Page table entry. */

    struct list_elem elem;  /* Element for frame table. */
};

#endif