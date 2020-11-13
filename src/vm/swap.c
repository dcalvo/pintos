#include "vm/swap.h"
#include "threads/vaddr.h"

static struct block *swap_block;
static struct bitmap *swap_map; // false is swap space available

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

void
swap_init (void)
{
    swap_block = block_get_role (BLOCK_SWAP);
    swap_map = bitmap_create (block_size (swap_block) / SECTORS_PER_PAGE);
}

void
swap_read (struct frame_table_entry *fte)
{
    ASSERT (fte != NULL);
    ASSERT (lock_held_by_current_thread (&fte->lock));
    ASSERT (fte->pte->sector != -1);

    for (int i = 0; i < SECTORS_PER_PAGE; i++) {
        block_read (swap_block, i + fte->pte->sector,
                    fte->addr + (i * BLOCK_SECTOR_SIZE));
    }
    bitmap_set (swap_map, fte->pte->sector, false);

    fte->pte->swapped = false;
    fte->pte->sector = -1;
}

void
swap_write (struct frame_table_entry *fte)
{
    ASSERT (fte != NULL);
    ASSERT (lock_held_by_current_thread (&fte->lock));

    int sector = bitmap_scan_and_flip (swap_map, 0, 1, false);
    if ((unsigned) sector == BITMAP_ERROR)
        PANIC ("NO SWAP SLOT AVAILABLE");
        
    for (int i = 0; i < SECTORS_PER_PAGE; i++) {
        block_write (swap_block, i + sector,
                    fte->addr + (i * BLOCK_SECTOR_SIZE));
    }

    fte->pte->swapped = true;
    fte->pte->sector = sector;
}
