#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "list.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

#ifdef VM
#include "vm/mmap.h"
#include "vm/vmm.h"
#endif

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static int parse_args (char *cmdline, char **argv);
static void push_args (int argc, char **argv, void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name)
{
  char *fn_copy, prog_name[16];
  int i;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Create a new thread to execute FILE_NAME. */
  for (i = 0; file_name[i] != ' ' && file_name[i] != '\0' && i < 15; i++)
    prog_name[i] = file_name[i];
  prog_name[i] = '\0';
  tid = thread_create (prog_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy);
  return tid;
}

/* Parse command line arguments from `cmdline` and save the args to `argv`.
   Returns the number of arguments. */
static int
parse_args (char *cmdline, char **argv)
{
  int argc;
  char *token, *last;

  argc = 0;
  for (token = strtok_r (cmdline, " ", &last); token;
       token = strtok_r (NULL, " ", &last))
    {
      argv[argc] = token;
      argc++;
    }
  return argc;
}

/* Push `argc` arguments in `argv` to process stack starting from `sp`. */
static void
push_args (int argc, char **argv, void **esp)
{
  int i, arg_len, *argc_ptr;
  char *arg_ptr, **arg_addrs, **arg_addr_ptr, **argv_addr_ptr;
  void (**retaddr_ptr) (void);
  ptrdiff_t padding_size;

  arg_ptr = *esp;
  arg_addrs = palloc_get_page (0);
  for (i = argc - 1; i >= 0; i--)
    {
      arg_len = strlen (argv[i]);
      arg_ptr -= arg_len + 1;
      strlcpy (arg_ptr, argv[i], arg_len + 1);
      arg_addrs[i] = arg_ptr;
    }
  *esp = arg_ptr;

  padding_size = (uintptr_t)(*esp) % sizeof (char *);
  *esp = (void *)((uintptr_t)(*esp) - padding_size);
  arg_addr_ptr = *esp;
  arg_addr_ptr--;
  *arg_addr_ptr = NULL;
  for (i = argc - 1; i >= 0; i--)
    {
      arg_addr_ptr--;
      *arg_addr_ptr = arg_addrs[i];
    }
  *esp = arg_addr_ptr;

  argv_addr_ptr = *esp;
  argv_addr_ptr--;

  /* I know this looks like code smell, but this is OK... */
  *argv_addr_ptr = (void *)arg_addr_ptr;
  *esp = argv_addr_ptr;

  argc_ptr = *esp;
  argc_ptr--;
  *argc_ptr = argc;
  *esp = argc_ptr;

  retaddr_ptr = *esp;
  retaddr_ptr--;
  *retaddr_ptr = NULL;
  *esp = retaddr_ptr;

  palloc_free_page (arg_addrs);
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_, **argv;
  struct intr_frame if_;
  bool success;
  int argc;
  struct thread *cur;
  struct file_descriptor *stdin_fd, *stdout_fd;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  argv = palloc_get_page (0);
  if (argv == NULL)
    {
      palloc_free_page (file_name);
      thread_exit ();
    }

#ifdef VM
  success = vmm_init ();
  if (!success)
    {
      palloc_free_page (file_name);
      palloc_free_page (argv);
      thread_exit ();
    }
#endif

  argc = parse_args (file_name, argv);
  success = load (argv[0], &if_.eip, &if_.esp);

  /* If load failed, quit. */
  if (!success)
    {
      palloc_free_page (file_name);
      palloc_free_page (argv);
      thread_exit ();
    }

  push_args (argc, argv, &if_.esp);
  palloc_free_page (file_name);
  palloc_free_page (argv);

  cur = thread_current ();
  stdin_fd = palloc_get_page (PAL_ZERO);
  if (stdin_fd == NULL)
    thread_exit ();

  stdin_fd->id = 0;
  stdin_fd->keyboard_in = true;

  stdout_fd = palloc_get_page (PAL_ZERO);
  if (stdout_fd == NULL)
    {
      palloc_free_page (stdin_fd);
      thread_exit ();
    }

  stdout_fd->id = 1;
  stdout_fd->screen_out = true;
  list_push_back (&cur->pcb->file_descriptor_list, &stdin_fd->elem);
  list_push_back (&cur->pcb->file_descriptor_list, &stdout_fd->elem);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid)
{
  struct pcb *child_pcb;
  int exit_code;

  child_pcb = process_child_by_pid (child_tid);
  if (child_pcb == NULL)
    return -1;

  sema_down (&child_pcb->exit_sema);
  exit_code = child_pcb->exit_code;

  list_remove (&child_pcb->child_pcb_elem);
  palloc_free_page (child_pcb);
  return exit_code;
}

/* Exit the process with given exit code. */
void
process_trigger_exit (int exit_code)
{
  struct thread *cur = thread_current ();
  struct list_elem *el;
  struct file_descriptor *fd;

  printf ("%s: exit(%d)\n", cur->name, exit_code);

  while (!list_empty (&cur->pcb->file_descriptor_list))
    {
      el = list_front (&cur->pcb->file_descriptor_list);
      fd = list_entry (el, struct file_descriptor, elem);
      process_cleanup_fd (fd);
    }

  cur->pcb->exit_code = exit_code;
  sema_up (&cur->pcb->exit_sema);
  file_close (cur->pcb->exe_file);
  thread_exit ();

  NOT_REACHED ();
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
#ifdef VM
  struct list_elem *el;
#endif

#ifdef VM
  thread_fs_lock_acquire ();
  while (!list_empty (&cur->mmap_blocks))
    {
      struct mmap_user_block *block;
      el = list_pop_front (&cur->mmap_blocks);
      block = list_entry (el, struct mmap_user_block, elem);
      vmm_cleanup_user_block (block);
    }
  thread_fs_lock_release ();
  vmm_destroy ();
#endif

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* Get child process's PCB from pid. */
struct pcb *
process_child_by_pid (tid_t pid)
{
  struct list_elem *el;
  struct thread *cur;
  struct pcb *res;

  cur = thread_current ();
  for (el = list_begin (&cur->children_pcb_list);
       el != list_end (&cur->children_pcb_list); el = list_next (el))
    {
      res = list_entry (el, struct pcb, child_pcb_elem);
      if (res->pid == pid)
        return res;
    }

  return NULL;
}

/* Get first free fd number in fd list. */
int
process_get_first_free_fd_num (void)
{
  struct list_elem *el;
  struct thread *cur;
  struct file_descriptor *fd;
  int free_fd_id;

  cur = thread_current ();
  free_fd_id = 0;
  for (el = list_begin (&cur->pcb->file_descriptor_list);
       el != list_end (&cur->pcb->file_descriptor_list); el = list_next (el))
    {
      fd = list_entry (el, struct file_descriptor, elem);
      if (free_fd_id != fd->id)
        return free_fd_id;
      free_fd_id++;
    }

  return free_fd_id;
}

/* Get file descriptor object from id. */
struct file_descriptor *
process_get_fd (int id)
{
  struct list_elem *el;
  struct thread *cur;
  struct file_descriptor *fd;

  cur = thread_current ();
  for (el = list_begin (&cur->pcb->file_descriptor_list);
       el != list_end (&cur->pcb->file_descriptor_list); el = list_next (el))
    {
      fd = list_entry (el, struct file_descriptor, elem);
      if (fd->id == id)
        return fd;
    }

  return NULL;
}

bool
process_compare_fd_id (const struct list_elem *a, const struct list_elem *b,
                       void *aux UNUSED)
{
  struct file_descriptor *fd_a, *fd_b;
  fd_a = list_entry (a, struct file_descriptor, elem);
  fd_b = list_entry (b, struct file_descriptor, elem);
  return fd_a->id < fd_b->id;
}

/* Clean up file descriptor. */
void
process_cleanup_fd (struct file_descriptor *fd)
{
  ASSERT (fd != NULL);

  if (fd->file != NULL)
    {
      thread_fs_lock_acquire ();
      file_close (fd->file);
      thread_fs_lock_release ();
    }

  list_remove (&fd->elem);
  palloc_free_page (fd);
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
{
  unsigned char e_ident[16];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
{
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0           /* Ignore. */
#define PT_LOAD 1           /* Loadable segment. */
#define PT_DYNAMIC 2        /* Dynamic linking info. */
#define PT_INTERP 3         /* Name of dynamic loader. */
#define PT_NOTE 4           /* Auxiliary info. */
#define PT_SHLIB 5          /* Reserved. */
#define PT_PHDR 6           /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp)
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();

  /* Open executable file. */
  thread_fs_lock_acquire ();
  file = filesys_open (file_name);
  if (file == NULL)
    {
      printf ("load: %s: open failed\n", file_name);
      goto done;
    }
  t->pcb->exe_file = file;

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2
      || ehdr.e_machine != 3 || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr) || ehdr.e_phnum > 1024)
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done;
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file))
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *)mem_page, read_bytes,
                                 zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void))ehdr.e_entry;

  success = true;
  file_deny_write (file);

done:
  /* We arrive here whether the load is successful or not. */
  if (!success)
    file_close (file);
  thread_fs_lock_release ();
  t->pcb->load_success = success;
  sema_up (&t->pcb->load_sema);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off)file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *)phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *)(phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes,
              uint32_t zero_bytes, bool writable)
{
#ifdef VM
  uint32_t pos;
#endif

  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

#ifdef VM
  pos = 0;
#else
  file_seek (file, ofs);
#endif
  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

#ifdef VM
      if (!vmm_create_file_map (upage, file, writable, true, ofs + pos,
                                page_read_bytes))
        return false;
      pos += page_read_bytes;
#else
      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int)page_read_bytes)
        {
          palloc_free_page (kpage);
          return false;
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable))
        {
          palloc_free_page (kpage);
          return false;
        }
#endif

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp)
{
  bool success = false;
#ifdef VM
  success = vmm_create_anonymous (((uint8_t *)PHYS_BASE) - PGSIZE, true);
  if (!success)
    return false;
  else
    *esp = PHYS_BASE;
#else
  uint8_t *kpage;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL)
    {
      upage = ((uint8_t *)PHYS_BASE) - PGSIZE;
      success = install_page (upage, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
#endif

  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
