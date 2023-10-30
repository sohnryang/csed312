#include "userprog/syscall.h"
#include "devices/shutdown.h"
#include "threads/interrupt.h"
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

  pop_arg (const char *, filename, esp);

  // TODO: implement
}

/* Wait for child process. */
static int
wait (void *esp)
{
  int pid;

  pop_arg (int, pid, esp);

  // TODO: implement
}

/* Create file. */
static int
create (void *esp)
{
  const char *filename;
  unsigned initial_size;

  pop_arg (const char *, filename, esp);
  pop_arg (unsigned, initial_size, esp);

  // TODO: implement
}

/* Remove file. */
static int
remove (void *esp)
{
  const char *filename;

  pop_arg (const char *, filename, esp);

  // TODO: implement
}

/* Open file. */
static int
open (void *esp)
{
  const char *filename;

  pop_arg (const char *, filename, esp);

  // TODO: implement
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
