#ifndef SWAP_H
#define SWAP_H

#include <bitmap.h>
#include "devices/block.h"
#include "vm/frame.h"

void swap_init (void);
void swap_read (struct frame_table_entry *fte);
void swap_write (struct frame_table_entry *fte);

#endif