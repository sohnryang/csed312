#include "userprog/syscall.h"

#include <stdint.h>
#include <string.h>
#include "bitmap.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/off_t.h"
#include "list.h"
#include "stdio.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/usermem.h"

static void syscall_handler (struct intr_frame *);

typedef int syscall_func (void *esp);
static syscall_func halt_ NO_RETURN;
static syscall_func exit_ NO_RETURN;
static syscall_func exec_;
static syscall_func wait_;
static syscall_func create_;
static syscall_func remove_;
static syscall_func open_;
static syscall_func filesize_;
static syscall_func read_;
static syscall_func write_;
static syscall_func seek_;
static syscall_func tell_;
static syscall_func close_;

#define FILE_IO_BUFSIZE 1024

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
      = { halt_,     exit_, exec_,  wait_, create_, remove_, open_,
          filesize_, read_, write_, seek_, tell_,   close_ };

  void *esp = f->esp;

#ifdef VM
  struct thread *cur = thread_current ();

  cur->esp_before_syscall = f->esp;
#endif

  pop_arg (int, syscall_id, esp);
  if (syscall_id < 0 || syscall_id >= 13)
    process_trigger_exit (-1);

  f->eax = syscall_table[syscall_id](esp);

#ifdef VM
  cur->esp_before_syscall = NULL;
#endif
}

/* Halt the system. */
static int
halt_ (void *esp UNUSED)
{
  shutdown_power_off ();
  NOT_REACHED ();
}

static int
exit_ (void *esp)
{
  int status;

  pop_arg (int, status, esp);

  process_trigger_exit (status);
  NOT_REACHED ();
}

/* Run child process. */
static int
exec_ (void *esp)
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
wait_ (void *esp)
{
  int pid;

  pop_arg (int, pid, esp);

  return process_wait (pid);
}

/* Create file. */
static int
create_ (void *esp)
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
remove_ (void *esp)
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
open_ (void *esp)
{
  const char *filename;
  char *filename_copy;
  struct file_descriptor *fd;
  struct file *file;

  pop_arg (const char *, filename, esp);

  if (usermem_strlen (filename) < 0)
    process_trigger_exit (-1);

  filename_copy = usermem_strdup_from_user (filename);
  if (filename_copy == NULL)
    return -1;

  thread_fs_lock_acquire ();
  file = filesys_open (filename_copy);
  thread_fs_lock_release ();
  palloc_free_page (filename_copy);
  if (file == NULL)
    return -1;

  fd = palloc_get_page (PAL_ZERO);
  if (fd == NULL)
    return -1;

  fd->id = process_get_first_free_fd_num ();
  fd->file = file;
  list_insert_ordered (&thread_current ()->pcb->file_descriptor_list, &fd->elem,
                       process_compare_fd_id, NULL);

  return fd->id;
}

/* Get size of file. */
static int
filesize_ (void *esp)
{
  int fd;
  struct file_descriptor *fd_object;
  off_t res;

  pop_arg (int, fd, esp);

  fd_object = process_get_fd (fd);
  if (fd_object == NULL)
    process_trigger_exit (-1);

  thread_fs_lock_acquire ();
  res = file_length (fd_object->file);
  thread_fs_lock_release ();

  return res;
}

/* Read from file descriptor. */
static int
read_ (void *esp)
{
  int fd;
  void *buffer, *dst;
  unsigned length, actually_read, read_bytes, bytes_to_read, bytes_left;
  struct file_descriptor *fd_object;
  char *read_buffer, keybaord_char;
  bool res;

  pop_arg (int, fd, esp);
  pop_arg (void *, buffer, esp);
  pop_arg (unsigned, length, esp);

  fd_object = process_get_fd (fd);
  if (fd_object == NULL || fd_object->screen_out)
    process_trigger_exit (-1);

  actually_read = 0;
  if (fd_object->keyboard_in)
    while (actually_read < length)
      {
        keybaord_char = input_getc ();
        res = usermem_copy_byte_to_user ((uint8_t *)buffer + actually_read,
                                         keybaord_char);
        if (!res)
          process_trigger_exit (-1);
        actually_read++;
      }
  else
    {
      read_buffer = palloc_get_page (0);
      if (read_buffer == NULL)
        return 0;

      while (actually_read < length)
        {
          bytes_left = length - actually_read;
          bytes_to_read
              = bytes_left > FILE_IO_BUFSIZE ? FILE_IO_BUFSIZE : bytes_left;

          thread_fs_lock_acquire ();
          read_bytes = file_read (fd_object->file, read_buffer, bytes_to_read);
          thread_fs_lock_release ();

          if (read_bytes == 0)
            break;

          dst = usermem_memcpy_to_user (buffer + actually_read, read_buffer,
                                        read_bytes);
          if (dst == NULL)
            {
              palloc_free_page (read_buffer);
              process_trigger_exit (-1);
            }
          actually_read += read_bytes;
        }
      palloc_free_page (read_buffer);
    }

  return actually_read;
}

/* Write to file descriptor. */
static int
write_ (void *esp)
{
  int fd;
  void *buffer, *dst;
  unsigned length, actually_written, written_bytes, bytes_to_write, bytes_left;
  struct file_descriptor *fd_object;
  char *write_buffer;

  pop_arg (int, fd, esp);
  pop_arg (void *, buffer, esp);
  pop_arg (unsigned, length, esp);

  fd_object = process_get_fd (fd);
  if (fd_object == NULL || fd_object->keyboard_in)
    process_trigger_exit (-1);

  actually_written = 0;
  write_buffer = palloc_get_page (0);
  if (write_buffer == NULL)
    return 0;

  while (actually_written < length)
    {
      bytes_left = length - actually_written;
      bytes_to_write
          = bytes_left > FILE_IO_BUFSIZE ? FILE_IO_BUFSIZE : bytes_left;

      dst = usermem_memcpy_from_user (write_buffer, buffer + actually_written,
                                      bytes_to_write);
      if (dst == NULL)
        {
          palloc_free_page (write_buffer);
          process_trigger_exit (-1);
        }

      if (fd_object->screen_out)
        {
          putbuf (write_buffer, bytes_to_write);
          actually_written += bytes_to_write;
        }
      else
        {
          thread_fs_lock_acquire ();
          written_bytes
              = file_write (fd_object->file, write_buffer, bytes_to_write);
          thread_fs_lock_release ();

          if (written_bytes == 0)
            break;

          actually_written += written_bytes;
        }
    }
  palloc_free_page (write_buffer);

  return actually_written;
}

/* Move to specific position of file. */
static int
seek_ (void *esp)
{
  int fd;
  unsigned position;
  struct file_descriptor *fd_object;

  pop_arg (int, fd, esp);
  pop_arg (unsigned, position, esp);

  fd_object = process_get_fd (fd);
  if (fd_object == NULL || fd_object->file == NULL)
    process_trigger_exit (-1);

  thread_fs_lock_acquire ();
  file_seek (fd_object->file, position);
  thread_fs_lock_release ();

  return 0;
}

/* Get position position of file. */
static int
tell_ (void *esp)
{
  int fd;
  struct file_descriptor *fd_object;
  unsigned res;

  pop_arg (int, fd, esp);

  fd_object = process_get_fd (fd);
  if (fd_object == NULL || fd_object->file == NULL)
    process_trigger_exit (-1);

  thread_fs_lock_acquire ();
  res = file_tell (fd_object->file);
  thread_fs_lock_release ();

  return res;
}

/* Close file. */
static int
close_ (void *esp)
{
  int fd;
  struct file_descriptor *fd_object;

  pop_arg (int, fd, esp);

  fd_object = process_get_fd (fd);
  if (fd_object == NULL)
    process_trigger_exit (-1);

  process_cleanup_fd (fd_object);

  return 0;
}
