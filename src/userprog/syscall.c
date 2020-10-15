#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"

struct fd
  {
    int fd;
    struct file *file;
    struct list_elem elem;
  };

static void syscall_handler (struct intr_frame *);
static void open (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  uint32_t syscall_num = *(uint32_t*)f->esp;
  switch (syscall_num) {
    case SYS_OPEN:
      open (f);
      break;
    case SYS_WRITE:
      printf("fd : %d | Length : %d\n",*(p+5),*(p+7));
		  printf("buffer: %s\n",*(p+6));
      break;
    case SYS_READDIR:
      break;
    case SYS_ISDIR:
      break;
    default:
      printf ("system call!\n");
      thread_exit ();
  }
}

static void
open (struct intr_frame *f)
{
  char *name = *(char**)(f->esp + 4);
  struct file *file = filesys_open (name);
  struct list *fds = &thread_current ()->fds;
  struct fd *fd = palloc_get_page (PAL_ZERO);
  if (list_empty (fds))
    fd->fd = 2;
  else {
    struct fd *fd_2 = list_entry (list_back (fds), struct fd, elem);
    fd->fd = fd_2->fd + 1;
  }
  fd->file = file;
  list_push_back (fds, &fd->elem);
  f->eax = fd->fd;
}
