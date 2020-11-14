#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

#include "threads/malloc.h"
#include "vm/mapid_t.h"
#include "vm/page.h"
#include "vm/frame.h"

struct lock filesys;

struct fd
  {
    int fd;
    struct file *file;
    struct list_elem elem;
  };

struct mapping
  {
    mapid_t mapid;
    struct file *file;
    struct list mapped_pages;
    struct list_elem elem;
  };

static void syscall_handler (struct intr_frame *);
static void fetch_args (struct intr_frame *f, int *argv, int num);
struct file* fetch_file (int fd_to_find);
static void* validate_addr (const void *addr);
static void free_mapping (struct mapping *mapping);

/* Syscall implementations. */
static int sys_exec (const char *cmdline);
static bool sys_create (const char *file_name, unsigned size);
static bool sys_remove (const char *file_name);
static int sys_open (const char *file_name);
static int sys_filesize (int fd);
static int sys_read (int fd, void *buffer, unsigned size);
static int sys_write (int fd, const void *buffer, unsigned size);
static void sys_seek (int fd, unsigned position);
static unsigned sys_tell (int fd);
static void sys_close (int fd);
static mapid_t sys_mmap (int fd, void *addr);
static void sys_munmap (mapid_t mapid);

void
syscall_init (void) 
{
  lock_init (&filesys);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  validate_addr (f->esp);
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
    case SYS_REMOVE:
      fetch_args (f, argv, 1);
      f->eax = sys_remove ((const char*)argv[0]);
      break;
    case SYS_OPEN:
      fetch_args (f, argv, 1);
      f->eax = sys_open ((const char *) argv[0]);
      break;
    case SYS_FILESIZE:
      fetch_args (f, argv, 1);
      f->eax = sys_filesize (argv[0]);
      break;
    case SYS_READ:
      fetch_args (f, argv, 3);
      f->eax = sys_read (argv[0], (void *) argv[1], (unsigned) argv[2]);
      break;
    case SYS_WRITE:
      fetch_args (f, argv, 3); // fd, buffer, size
      f->eax = sys_write (argv[0], (const void *) argv[1], (unsigned) argv[2]);
      break;
    case SYS_SEEK:
      fetch_args (f, argv, 2);
      sys_seek (argv[0], (unsigned)argv[1]);
      break;
    case SYS_TELL:
      fetch_args (f, argv, 1);
      f->eax = sys_tell (argv[0]);
      break;
    case SYS_CLOSE:
      fetch_args (f, argv, 1);
      sys_close (argv[0]);
      break;
    case SYS_MMAP:
      fetch_args (f, argv, 2);
      f->eax = sys_mmap (argv[0], (void *) argv[1]);
      break;
    case SYS_MUNMAP:
      fetch_args (f, argv, 1);
      sys_munmap (argv[0]);
      break;
    case SYS_CHDIR:
      break;
    case SYS_MKDIR:
      break;
    case SYS_READDIR:
      break;
    case SYS_ISDIR:
      break;
    case SYS_INUMBER:
      break;
    default:
      printf ("system call!\n");
      thread_exit ();
  }
}

/* Implementation of SYS_EXIT syscall. */
void
sys_exit (int status)
{
  struct thread *t = thread_current ();
  printf ("%s: exit(%d)\n", t->name, status);
  
  struct shared_info *shared_info = t->shared_info;
  if (shared_info)
  {
    shared_info->has_exited = true;
    shared_info->exit_code = status;
  }
  // while (!list_empty (&t->mappings))
  // {
  //   free_mapping (list_entry (list_pop_front (&t->mappings), struct mapping,
  //     elem));
  // }

  struct hash_iterator it;

  hash_first (&it, &thread_current ()->page_table);
  while (hash_next (&it))
  {
    struct page_table_entry *pte = hash_entry (hash_cur (&it),
      struct page_table_entry, hash_elem);
    page_evict (pte);
  }

  thread_exit ();
}

/* Implementation of SYS_EXEC syscall. */
static int
sys_exec (const char *cmdline)
{
  validate_addr (cmdline);
  lock_acquire (&filesys);
  int pid = process_execute (cmdline);
  lock_release (&filesys);
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

/* Implementation of SYS_REMOVE syscall. */
static bool
sys_remove (const char *file_name)
{
  validate_addr (file_name);
  lock_acquire (&filesys);
  bool success = filesys_remove (file_name);
  lock_release (&filesys);
  return success;
}

/* Implementation of SYS_OPEN syscall. */
static int
sys_open (const char *file_name)
{
  validate_addr (file_name);

  lock_acquire (&filesys);
  struct file *file = filesys_open (file_name);
  struct list *fds = &thread_current ()->fds;
  struct fd *fd = malloc (sizeof *fd);
  
  if (!file || !fd)
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
  buffer = validate_addr (buffer);

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

  if (fd == 1) {
    putbuf (buffer, size);
    return size;
  }

  // writing to file
  lock_acquire (&filesys);
  struct file *file = fetch_file (fd);
  if (!file)
  {
    lock_release (&filesys);
    return -1;
  }
  int wrote = file_write (file, buffer, size);
  lock_release (&filesys);
  return wrote;
}

/* Implementation of SYS_SEEK syscall. */
static void
sys_seek (int fd, unsigned position)
{
  struct file *file = fetch_file (fd);
  lock_acquire (&filesys);
  file_seek (file, position);
  lock_release (&filesys);
}

/* Implementation of SYS_TELL syscall. */
static unsigned
sys_tell (int fd)
{
  struct file *file = fetch_file (fd);
  lock_acquire (&filesys);
  unsigned position = file_tell (file);
  lock_release (&filesys);
  return position;
}

/* Implementation of SYS_CLOSE syscall. */
static void
sys_close (int fd_to_close)
{
  struct list *fds = &thread_current ()->fds;
  
  lock_acquire (&filesys);
  if (!list_empty (fds))
  {
    for (struct list_elem *it = list_front (fds); it != list_end (fds); it = list_next (it))
    {
      struct fd *fd = list_entry (it, struct fd, elem);
      if (fd->fd == fd_to_close)
      {
        file_close (fd->file);
        list_remove (&fd->elem);
        free (fd);
        break; // closed requested file
      }
    }
  }
  lock_release (&filesys);
}

/* Implementation of SYS_MMAP syscall. */
static mapid_t
sys_mmap (int fd, void *addr)
{
  if (fd == 0 || fd == 1) // 0 and 1 reserved for stdio
    return MAP_FAILED;
  
  if (!addr || pg_ofs(addr) % PGSIZE != 0)
    return MAP_FAILED;

  /* Get file statistics. */
  struct file *file = fetch_file (fd);
  if (!file)
    return MAP_FAILED;
  file = file_reopen (file);
  uint32_t read_bytes = file_length (file);
  if (read_bytes <= 0)
    return MAP_FAILED;
  bool writable = file_writable (file);

  /* Set up bookkeeping for mapped memory. */
  struct list *mappings = &thread_current ()->mappings;
  struct mapping *mapping = malloc (sizeof *mapping);
  mapping->mapid = list_empty (mappings) ? 0 
    : list_entry (list_back (mappings), struct mapping, elem)->mapid + 1;
  mapping->file = file;
  list_init (&mapping->mapped_pages);
  list_push_back (&thread_current ()->mappings, &mapping->elem);

  uint8_t *upage = addr;
  off_t ofs = 0;
  while (read_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;

      struct page_table_entry *pte = page_alloc (upage, writable);
      if (!pte)
        return MAP_FAILED;

      /* Populate page table entry. */
      pte->file = file;
      pte->file_ofs = ofs;
      pte->file_bytes = page_read_bytes;
      pte->mapped = true;
      list_push_back (&mapping->mapped_pages, &pte->list_elem);

      /* Advance. */
      read_bytes -= page_read_bytes;
      upage += PGSIZE;
      ofs += PGSIZE;
    }

  return mapping->mapid;
}

/* Implementation of SYS_MUNMAP syscall. */
static void
sys_munmap (mapid_t mapid)
{
  struct list *mappings = &thread_current ()->mappings;

  if (list_empty (mappings))
    return;

  for (struct list_elem *it = list_front (mappings); it != list_end (mappings);
       it = list_next (it))
  {
    struct mapping *mapping = list_entry (it, struct mapping, elem);
    if (mapping->mapid == mapid)
    {
      free_mapping (mapping);
      break;
    }
  }
}

/* Safely fetch register values from F and store it into ARGV array. Reads up to NUM args. */
static void
fetch_args (struct intr_frame *f, int *argv, int num)
{
  for (int i = 1; i <= num; i++)
  {
    int *arg = (int *) f->esp + i;
    validate_addr ((const void *) arg);
    argv[i - 1] = *arg;
  }
}

/* Fetches a file handle from the current thread given a file descriptor. */
struct file*
fetch_file (int fd_to_find)
{
  struct list *fds = &thread_current ()->fds;
  
  if (!list_empty (fds))
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
static void *
validate_addr (const void *addr)
{
  char *ptr = (char*)(addr); // increment through the address one byte at a time
  for (unsigned i = 0; i < sizeof (addr); i++)
  {
    if (!is_user_vaddr (ptr) || !pagedir_get_page (thread_current()->pagedir, ptr))
      sys_exit (-1); // -1 for memory violations
    ++ptr;
  }
  return pagedir_get_page (thread_current()->pagedir, addr);
}

// /* Converts a file name in upage space to a file name in kpage space. */
// static char *
// get_kfile_name (const char *ufile_name)
// {
//   struct page_table_entry *pte = page_load ((void *) ufile_name);
//   if (!pte)
//     sys_exit (-1); // bad file name pointer
  
//   size_t size = strlen (ufile_name) < PGSIZE ? strlen (ufile_name) : PGSIZE;
//   frame_acquire (&pte->fte->lock);
//   strlcpy (pte->fte->kpage, ufile_name, size);
//   frame_release (&pte->fte->lock);
// }

static void
free_mapping (struct mapping *mapping)
{
  struct list *mapped_pages = &mapping->mapped_pages;

  while (!list_empty (mapped_pages)) {
    struct page_table_entry *pte = list_entry (list_pop_front (mapped_pages),
                                               struct page_table_entry,
                                               list_elem);
      page_evict (pte); // write to swap, remove from pd, uninstall the frame
      free (pte); // delete supplemental pte
  }

  free (mapping);
}
