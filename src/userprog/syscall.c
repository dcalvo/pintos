#include "userprog/syscall.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"

struct lock filesys;

struct fd
  {
    int fd;
    struct file *file;
    struct list_elem elem;
  };

static void syscall_handler (struct intr_frame *);
static void fetch_args (struct intr_frame *f, int *argv, int num);
struct file* fetch_file (int fd_to_find);
static void validate_addr (const void *addr);

/* Syscall implementations. */
static void sys_exit (int status);
static int sys_exec (const char *cmdline);
static bool sys_create (const char *file_name, unsigned size);
static int sys_open (const char *);
static int sys_filesize (int fd);
static int sys_read (int fd, void *buffer, unsigned size);
static int sys_write (int fd, const void *buffer, unsigned size);
static void sys_close (int fd);

void
syscall_init (void) 
{
  lock_init (&filesys);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  validate_addr(f->esp);
  int argv[3]; // we expect at most 3 args and define as such to users
  uint32_t syscall_num = *(uint32_t*)f->esp;
  switch (syscall_num) {
    case SYS_HALT:
      shutdown_power_off ();
      NOT_REACHED ();
      break;
    case SYS_EXIT:
      fetch_args (f, argv, 1);
      sys_exit (argv[0]);
      NOT_REACHED ();
      break;
    case SYS_EXEC:
      fetch_args (f, argv, 1);
      f->eax = sys_exec ((const char *) argv[0]);
      break;
    case SYS_WAIT:
      fetch_args (f, argv, 1);
      f->eax = process_wait (argv[0]);
      break;
    case SYS_CREATE:
      fetch_args (f, argv, 2);
      f->eax = sys_create ((const char *) argv[0], (unsigned) argv[1]);
      break;
    case SYS_OPEN:
      fetch_args (f, argv, 1);
      f->eax = sys_open ((const char *) argv[0]);
      break;
    case SYS_FILESIZE:
      fetch_args (f, argv, 1);
      sys_filesize (argv[0]);
      break;
    case SYS_READ:
      fetch_args (f, argv, 3);
      f->eax = sys_read (argv[0], (void *) argv[1], (unsigned) argv[2]);
      break;
    case SYS_WRITE:
      fetch_args (f, argv, 3); // fd, buffer, size
      f->eax = sys_write (argv[0], (const void *) argv[1], (unsigned) argv[2]);
      break;
    case SYS_CLOSE:
      fetch_args (f, argv, 1);
      sys_close (argv[0]);
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
  struct thread *t = thread_current ();
  struct child_thread *info = t->info;
  printf ("%s: exit(%d)\n", t->name, status);
  info->exiting = true;
  info->exit_code = status;
  thread_exit ();
}

/* Implementation of SYS_EXEC syscall. */
static int
sys_exec (const char *cmdline)
{
  validate_addr(cmdline);
  lock_acquire(&filesys);
  int pid = process_execute (cmdline);
  lock_release(&filesys);
  return pid;
}

/* Implementation of SYS_CREATE syscall. */
static bool
sys_create (const char *file_name, unsigned size)
{
  validate_addr (file_name);
  bool success = false;
  lock_acquire (&filesys);
  success = filesys_create (file_name, size);
  lock_release (&filesys);
  return success;
}

/* Implementation of SYS_OPEN syscall. */
static int
sys_open (const char *name)
{
  validate_addr (name);

  lock_acquire (&filesys);
  struct file *file = filesys_open (name);
  struct list *fds = &thread_current ()->fds;
  struct fd *fd = palloc_get_page (PAL_ZERO);
  
  if (!file)
  {
    lock_release (&filesys);
    return -1;
  }
  
  if (list_empty (fds))
    fd->fd = 2;
  else {
    struct fd *prev_fd = list_entry (list_back (fds), struct fd, elem);
    fd->fd = prev_fd->fd + 1;
  }

  fd->file = file;
  list_push_back (fds, &fd->elem);
  lock_release (&filesys);
  return fd->fd;
}

/* Implementation of SYS_FILESIZE syscall. */
static int sys_filesize (int fd)
{
  lock_acquire (&filesys);
  struct file *file = fetch_file (fd);
  if (!file)
  {
    lock_release (&filesys);
    return -1;
  }
  int size = file_length (file);
  lock_release (&filesys);
  return size;
}

/* Implementation of SYS_READ syscall. */
static int sys_read (int fd, void *buffer, unsigned size)
{
  validate_addr (buffer);

  if (fd == 0) // read from stdinput
  {
    for (unsigned i = 0; i < size; i++)
    {
      ((uint8_t *) buffer)[i] = input_getc();
    }
    return size;
  }
  
  // reading from file
  lock_acquire (&filesys);
  struct file *file = fetch_file (fd);
  if (!file)
  {
    lock_release (&filesys);
    return -1;
  }
  int read = file_read (file, buffer, size);
  lock_release (&filesys);
  return read;
}

/* Implementation of SYS_WRITE syscall. */
static int
sys_write (int fd, const void *buffer, unsigned size)
{
  validate_addr (buffer);
  int wrote = 0;

  if (fd == 1) {
    putbuf(buffer, size);
    wrote = size;
  }

  return wrote;
}

/* Implementation of SYS_CLOSE syscall. */
static void
sys_close (int fd_to_close)
{
  struct list *fds = &(thread_current ()->fds);
  
  lock_acquire (&filesys);
  if (!list_empty(fds))
  {
    for (struct list_elem *it = list_front (fds); it != list_end (fds); it = list_next (it))
    {
      struct fd *fd = list_entry (it, struct fd, elem);
      if (fd->fd == fd_to_close)
      {
        file_close (fd->file);
        list_remove (&fd->elem);
        palloc_free_page (fd);
        break; // closed requested file
      }
    }
  }
  lock_release (&filesys);
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

/* Fetches a file handle from the current thread given a file descriptor. */
struct file*
fetch_file (int fd_to_find)
{
  struct list *fds = &(thread_current ()->fds);
  
  if (!list_empty(fds))
  {
    for (struct list_elem *it = list_front (fds); it != list_end (fds); it = list_next (it))
    {
      struct fd *fd = list_entry (it, struct fd, elem);
      if (fd->fd == fd_to_find)
        return fd->file; // found requested file
    }
  }

  return NULL;
}

/* Check if an address is valid using the methods described on the project page. */
static void
validate_addr (const void *addr)
{
  char *ptr = (char*)(addr); // increment through the addres one byte at a time
  for (unsigned i = 0; i < sizeof (addr); i++)
  {
    if (!is_user_vaddr(ptr) || !pagedir_get_page(thread_current()->pagedir, ptr))
      sys_exit(-1); // -1 for memory violations
    ++ptr;
  }
}
