#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
#include "pagedir.h"

struct fd
  {
    int fd;
    struct file *file;
    struct list_elem elem;
  };

static void fetch_args (struct intr_frame *f, int *argv, int num);
static void validate_addr (const void *addr);
static void syscall_handler (struct intr_frame *);
static void open (struct intr_frame *);
static void sys_write (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int argv[3]; // we expect at most 3 args and define as such to users
  uint32_t syscall_num = *(uint32_t*)f->esp;
  switch (syscall_num) {
    case SYS_HALT:
      shutdown_power_off ();
      NOT_REACHED ();
      break;
    case SYS_EXIT:
      fetch_args(f, argv, 1);
      sys_exit (argv[0]);
      NOT_REACHED ();
      break;
    case SYS_OPEN:
      open (f);
      break;
    case SYS_WRITE:
      fetch_args(f, argv, 3); // fd, buffer, size
      f->eax = sys_write (argv[0], (const void *) argv[1], (unsigned) argv[2]);
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

/* Implementation of SYS_EXIT syscall. */
static void
sys_exit (int status)
{
  printf ("%s: exit(%d)\n", thread_current()->name, status);
  
  thread_current ()->parent->exiting = true; //TODO fix this
  
  thread_current ()->exit_code = status;
  thread_exit ();
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


/* Implementation of SYS_WRITE syscall. */
static int
sys_write (int fd, const void *buffer, unsigned size)
{
  int wrote = 0;

  if (fd == 1) {
    putbuf(buffer, size);
    wrote = size;
  }

  return wrote;
}

/* Safely fetch register values from F and store it into ARGV array. Reads up to NUM args. */
static void
fetch_args (struct intr_frame *f, int *argv, int num)
{
  for (int i = 1; i <= num; i++)
  {
    int *arg = (int *) f->esp + i;
    validate_addr((const void *) arg);
    argv[i - 1] = *arg;
  }
}

/* Check if an address is valid using the methods described on the project page. */
static void
validate_addr (const void *addr)
{
  if (!is_user_vaddr(addr) || !pagedir_get_page(thread_current()->pagedir, addr))
    sys_exit(-1); // -1 for memory violations
}