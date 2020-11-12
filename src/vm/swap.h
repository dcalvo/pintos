#ifndef SWAP_H
#define SWAP_H

#include <bitmap.h>
#include "devices/block.h"
#include "vm/frame.h"

static struct block *swap_block;
static struct bitmap *swap_map; // false is swap space available

void swap_init (void);
void swap_read (struct frame_table_entry *fte);
void swap_write (struct frame_table_entry *fte);

#endif