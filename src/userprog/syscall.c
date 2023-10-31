#include "userprog/syscall.h"

#include <string.h>
#include "bitmap.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "list.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/usermem.h"

static void syscall_handler (struct intr_frame *);

typedef int syscall_func (void *esp);
static syscall_func halt NO_RETURN;
static syscall_func exit NO_RETURN;
static syscall_func exec;
static syscall_func wait;
static syscall_func create;
static syscall_func remove;
static syscall_func open;
static syscall_func filesize;
static syscall_func read;
static syscall_func write;
static syscall_func seek;
static syscall_func tell;
static syscall_func close;

#define pop_arg(TYPE, OUT, SP)                                                 \
  ({                                                                           \
    TYPE *arg_ptr;                                                             \
    void *dst;                                                                 \
    arg_ptr = (TYPE *)(SP);                                                    \
    dst = usermem_memcpy_from_user (&OUT, arg_ptr, sizeof (TYPE));             \
    if (dst == NULL)                                                           \
      process_trigger_exit (-1);                                               \
    arg_ptr++;                                                                 \
    SP = arg_ptr;                                                              \
  })

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f)
{
  int syscall_id;
  syscall_func *syscall_table[]
      = { halt,     exit, exec,  wait, create, remove, open,
          filesize, read, write, seek, tell,   close };

  void *esp = f->esp;
  pop_arg (int, syscall_id, esp);
  if (syscall_id < 0 || syscall_id >= 13)
    process_trigger_exit (-1);

  f->eax = syscall_table[syscall_id](esp);
}

/* Halt the system. */
static int
halt (void *esp UNUSED)
{
  shutdown_power_off ();
  NOT_REACHED ();
}

static int
exit (void *esp)
{
  int status;

  pop_arg (int, status, esp);

  process_trigger_exit (status);
  NOT_REACHED ();
}

/* Run child process. */
static int
exec (void *esp)
{
  const char *filename;
  char *filename_copy;
  int child_pid;
  struct pcb *child_pcb;

  pop_arg (const char *, filename, esp);

  filename_copy = usermem_strdup_from_user (filename);
  if (filename_copy == NULL)
    process_trigger_exit (-1);

  child_pid = process_execute (filename_copy);
  palloc_free_page (filename_copy);
  if (child_pid == -1)
    return -1;

  child_pcb = process_child_by_pid (child_pid);
  sema_down (&child_pcb->load_sema);
  if (!child_pcb->load_success)
    return -1;

  return child_pid;
}

/* Wait for child process. */
static int
wait (void *esp)
{
  int pid;

  pop_arg (int, pid, esp);

  return process_wait (pid);
}

/* Create file. */
static int
create (void *esp)
{
  const char *filename;
  char *filename_copy;
  unsigned initial_size;
  bool success;

  pop_arg (const char *, filename, esp);
  pop_arg (unsigned, initial_size, esp);

  filename_copy = usermem_strdup_from_user (filename);
  if (filename_copy == NULL)
    process_trigger_exit (-1);

  if (strlen (filename_copy) == 0)
    {
      palloc_free_page (filename_copy);
      return false;
    }

  thread_fs_lock_acquire ();
  success = filesys_create (filename_copy, initial_size);
  thread_fs_lock_release ();

  palloc_free_page (filename_copy);
  return success;
}

/* Remove file. */
static int
remove (void *esp)
{
  const char *filename;
  char *filename_copy;
  bool success;

  pop_arg (const char *, filename, esp);

  filename_copy = usermem_strdup_from_user (filename);
  if (filename_copy == NULL)
    process_trigger_exit (-1);

  thread_fs_lock_acquire ();
  success = filesys_remove (filename_copy);
  thread_fs_lock_release ();

  palloc_free_page (filename_copy);
  return success;
}

/* Open file. */
static int
open (void *esp)
{
  const char *filename;
  char *filename_copy;
  struct file_descriptor *fd;
  struct file *file;

  pop_arg (const char *, filename, esp);

  filename_copy = usermem_strdup_from_user (filename);
  if (filename_copy == NULL)
    process_trigger_exit (-1);

  thread_fs_lock_acquire ();
  file = filesys_open (filename_copy);
  thread_fs_lock_release ();
  palloc_free_page (filename_copy);
  if (file == NULL)
    return -1;

  fd = palloc_get_page (PAL_ZERO);
  fd->id = process_get_first_free_fd_num ();
  fd->file = file;
  list_insert_ordered (&thread_current ()->pcb->file_descriptor_list, &fd->elem,
                       process_compare_fd_id, NULL);

  return fd->id;
}

/* Get size of file. */
static int
filesize (void *esp)
{
  int fd;

  pop_arg (int, fd, esp);

  // todo: implement
}

/* Read from file descriptor. */
static int
read (void *esp)
{
  int fd;
  void *buffer;
  unsigned length;

  pop_arg (int, fd, esp);
  pop_arg (void *, buffer, esp);
  pop_arg (unsigned, length, esp);

  // TODO: implement
}

/* Write to file descriptor. */
static int
write (void *esp)
{
  int fd;
  void *buffer;
  unsigned length;

  pop_arg (int, fd, esp);
  pop_arg (void *, buffer, esp);
  pop_arg (unsigned, length, esp);

  // TODO: implement
}

/* Move to specific position of file. */
static int
seek (void *esp)
{
  int fd;
  unsigned position;

  pop_arg (int, fd, esp);
  pop_arg (unsigned, position, esp);

  // TODO: implement
}

/* Get position position of file. */
static int
tell (void *esp)
{
  int fd;

  pop_arg (int, fd, esp);

  // TODO: implement
}

/* Close file. */
static int
close (void *esp)
{
  int fd;

  pop_arg (int, fd, esp);

  // TODO: implement
}
