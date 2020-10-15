#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"

struct fd
  {
    int fd;
    struct file *file;
    struct list_elem elem;
  };

static void memfetch (void *dest, void *src, size_t size);
static void syscall_handler (struct intr_frame *);
static void open (struct intr_frame *);
static void sys_write (struct intr_frame *);

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
    case SYS_HALT:
      shutdown_power_off ();
      NOT_REACHED ();
      break;
    case SYS_EXIT:
      thread_current ()->parent->exiting = true;
      thread_exit ();
    case SYS_OPEN:
      open (f);
      break;
    case SYS_WRITE:
      sys_write (f);
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

static void
sys_write (struct intr_frame *f)
{
  int wrote = 0;
  int fd, size;
  const void *buf;

  memfetch (&fd, f->esp + 4, sizeof (fd));
  memfetch (&buf, f->esp + 8, sizeof (buf));
  memfetch (&size, f->esp + 12, sizeof (size));

  if (fd == 1) {
    putbuf(buf, size);
  }
}

/* Safely fetch register value from SRC and store it into DEST. Reads up to SIZE bytes. */
static void
memfetch (void *dest, void *src, size_t size)
{
  int val;
  for (size_t i = 0; i < size; i++)
  {
    val = get_user (src + i);
    if (val == -1)
    {
      PANIC("implement unsafe access error handling");
    }

    *((char *)(dest + i)) = val & 0xff;
  }
}

/* From the project page. */
/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  if (PHYS_BASE > (void *) uaddr) return -1;

  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* From the project page. */
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  if (PHYS_BASE > (void *) udst) return false;
  
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}