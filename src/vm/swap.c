#include "vm/swap.h"
#include "threads/vaddr.h"

#define SECTORS (PGSIZE / BLOCK_SECTOR_SIZE)

void
swap_init (void)
{
    swap_block = block_get_role (BLOCK_SWAP);
}

void
swap_read (struct frame_table_entry *fte)
{
    ASSERT (fte);
    ASSERT (lock_held_by_current_thread (&fte->lock));

    for (int i = 0; i < SECTORS; i++) {
        block_read (swap_block, i, fte->addr + (i * BLOCK_SECTOR_SIZE));
    }

    fte->pte->swapped = false;
}

void
swap_write (struct frame_table_entry *fte)
{
    ASSERT (fte);
    ASSERT (lock_held_by_current_thread (&fte->lock));

    for (int i = 0; i < SECTORS; i++) {
        block_write (swap_block, i, fte->addr + (i * BLOCK_SECTOR_SIZE));
    }

    fte->pte->swapped = true;
    fte->pte->file = NULL;
}