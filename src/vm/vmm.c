#include "vm/vmm.h"

#include "filesys/file.h"
#include "stddef.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "user/syscall.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/mmap.h"
#include "vm/swap.h"

#include <hash.h>
#include <list.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define STACK_GROW_LIMIT 32
#define STACK_MAXSIZE (8 << 20)

/* Add a non-mapped user page `upage` to page table. */
static bool
install_page_stub (void *upage, bool writable)
{
  struct thread *cur = thread_current ();

  return (pagedir_get_page (cur->pagedir, upage) == NULL
          && pagedir_set_page_stub (cur->pagedir, upage, writable));
}

/* Initialize virtual memory manager. */
bool
vmm_init (void)
{
  struct thread *cur;

  cur = thread_current ();

  list_init (&cur->frames);
  list_init (&cur->mmap_blocks);
  return hash_init (&cur->mmaps, mmap_info_hash, mmap_info_less, NULL);
}

/* Destroy VMM related data structures. */
void
vmm_destroy (void)
{
  struct thread *cur;
  struct list_elem *el;
  struct frame *frame;

  swap_acquire_lock ();

  cur = thread_current ();
  while (!list_empty (&cur->frames))
    {
      el = list_pop_front (&cur->frames);
      frame = list_entry (el, struct frame, elem);
      if (frame->kpage != NULL)
        {
          palloc_free_page (frame->kpage);
          swap_unregister_frame (frame);
        }
      else if (frame->is_swapped_out)
        swap_free_frame (frame);
      free (frame);
    }

  swap_release_lock ();

  hash_destroy (&cur->mmaps, mmap_info_destruct);
}

/* Link `info` to thread `cur`. */
bool
vmm_link_mapping_to_thread (struct thread *cur, struct mmap_info *info)
{
  if (hash_find (&cur->mmaps, &info->map_elem))
    return false;
  hash_insert (&cur->mmaps, &info->map_elem);

  return install_page_stub (info->upage, info->writable);
}

/* Unlink `info` from thread `cur`. */
bool
vmm_unlink_mapping_from_thread (struct thread *cur, struct mmap_info *info)
{
  if (!hash_find (&cur->mmaps, &info->map_elem))
    return false;
  hash_delete (&cur->mmaps, &info->map_elem);

  pagedir_clear_page (info->pd, info->upage);
  return true;
}

/* Create a new frame for `cur`. */
struct frame *
vmm_create_frame (struct thread *cur)
{
  struct frame *frame;

  frame = malloc (sizeof (struct frame));
  if (frame == NULL)
    return NULL;
  frame_init (frame);

  list_push_back (&cur->frames, &frame->elem);
  return frame;
}

/* Destroy a frame. */
void
vmm_destroy_frame (struct frame *frame)
{
  list_remove (&frame->elem);
  free (frame);
}

/* Map `info` to `frame`. */
void
vmm_map_to_frame (struct mmap_info *info, struct frame *frame)
{
  list_push_back (&frame->mappings, &info->elem);
  info->frame = frame;
}

/* Map `info` from its frame. */
void
vmm_unmap_from_frame (struct mmap_info *info)
{
  list_remove (&info->elem);
  info->frame = NULL;
}

/* Create an anonymous mapping for `upage`. */
struct mmap_info *
vmm_create_anonymous (void *upage, bool writable)
{
  struct mmap_info *info;
  struct frame *frame;
  struct thread *cur;

  ASSERT (pg_ofs (upage) == 0);

  info = malloc (sizeof (struct mmap_info));
  if (info == NULL)
    return NULL;
  mmap_init_anonymous (info, upage, writable);

  cur = thread_current ();
  if (!vmm_link_mapping_to_thread (cur, info))
    {
      free (info);
      return NULL;
    }

  frame = vmm_create_frame (cur);
  if (frame == NULL)
    {
      free (info);
      return NULL;
    }

  vmm_map_to_frame (info, frame);
  return info;
}

/* Create a file mapping for `file` to `upage`. */
struct mmap_info *
vmm_create_file_map (void *upage, struct file *file, bool writable,
                     bool exe_mapping, off_t offset, uint32_t size)
{
  struct mmap_info *info;
  struct frame *frame;
  struct thread *cur;

  ASSERT (pg_ofs (upage) == 0);

  info = malloc (sizeof (struct mmap_info));
  if (info == NULL)
    return NULL;
  mmap_init_file_map (info, upage, file, writable, exe_mapping, offset, size);

  cur = thread_current ();
  if (!vmm_link_mapping_to_thread (cur, info))
    {
      free (info);
      return NULL;
    }

  frame = vmm_create_frame (cur);
  if (frame == NULL)
    {
      free (info);
      return NULL;
    }

  vmm_map_to_frame (info, frame);
  return info;
}

/* Find page frame corresponding to `upage`. */
struct frame *
vmm_lookup_frame (void *upage)
{
  struct mmap_info info;
  struct hash_elem *el;
  struct thread *cur;

  info.upage = upage;

  cur = thread_current ();
  el = hash_find (&cur->mmaps, &info.map_elem);

  return el != NULL ? hash_entry (el, struct mmap_info, map_elem)->frame : NULL;
}

/* Deserialize frame from disk. */
bool
vmm_activate_frame (struct frame *frame, void *kpage)
{
  struct list_elem *el;
  struct mmap_info *info;
  bool read_from_file;
  size_t zero_bytes;

  frame->kpage = kpage;
  if (frame->is_swapped_out)
    {
      swap_read_frame (frame);
      for (el = list_begin (&frame->mappings);
           el != list_end (&frame->mappings); el = list_next (el))
        {
          info = list_entry (el, struct mmap_info, elem);
          if (!pagedir_set_page (info->pd, info->upage, kpage, info->writable))
            return false;
        }
    }
  else
    {
      read_from_file = false;
      for (el = list_begin (&frame->mappings);
           el != list_end (&frame->mappings); el = list_next (el))
        {
          info = list_entry (el, struct mmap_info, elem);
          if (!pagedir_set_page (info->pd, info->upage, kpage, info->writable))
            return false;
          if (info->file != NULL)
            {
              ASSERT (!read_from_file);

              file_seek (info->file, info->offset);
              file_read (info->file, kpage, info->mapped_size);
              zero_bytes = PGSIZE - info->mapped_size;
              memset (kpage + info->mapped_size, 0, zero_bytes);
              read_from_file = true;
            }
        }

      if (frame->is_stub && !read_from_file)
        memset (kpage, 0, PGSIZE);
    }

  frame->is_stub = false;
  frame->is_swapped_out = false;
  swap_register_frame (frame);

  return true;
}

/* Handle page faults caused by non-present page access. */
bool
vmm_handle_not_present (void *fault_addr)
{
  void *kpage, *upage;
  struct frame *frame, *victim;
  bool success;

  upage = pg_round_down (fault_addr);
  frame = vmm_lookup_frame (upage);
  if (frame == NULL)
    return false;

  swap_acquire_lock ();
  thread_fs_lock_acquire ();

  kpage = palloc_get_page (PAL_USER);
  if (kpage == NULL)
    {
      victim = swap_find_victim ();
      vmm_deactivate_frame (victim);
      kpage = palloc_get_page (PAL_USER);
    }

  success = vmm_activate_frame (frame, kpage);

  thread_fs_lock_release ();
  swap_release_lock ();

  return success;
}

/* Write frame content to disk. */
void
vmm_deactivate_frame (struct frame *frame)
{
  struct list_elem *el;
  struct mmap_info *info;
  bool written_to_file, readonly, exe_mapping;

  if (frame->is_stub || frame->is_swapped_out || frame->kpage == NULL)
    return;

  written_to_file = false;
  readonly = true;
  exe_mapping = false;
  for (el = list_begin (&frame->mappings); el != list_end (&frame->mappings);
       el = list_next (el))
    {
      info = list_entry (el, struct mmap_info, elem);
      readonly &= !info->writable;
      exe_mapping |= info->exe_mapping;
      pagedir_clear_page (info->pd, info->upage);
      if (info->file != NULL && !info->exe_mapping
          && pagedir_is_dirty (info->pd, info->upage))
        {
          ASSERT (!written_to_file);

          file_seek (info->file, info->offset);
          file_write (info->file, frame->kpage, info->mapped_size);
          written_to_file = true;
        }
    }

  if (!written_to_file && !(readonly && exe_mapping))
    {
      swap_write_frame (frame);
      frame->is_swapped_out = true;
    }
  else
    frame->is_swapped_out = false;

  palloc_free_page (frame->kpage);
  frame->kpage = NULL;
  swap_unregister_frame (frame);
}

/* Check if the page fault in `fault_addr` is caused by insufficient stack size
   using the given `esp`, and grow the stack if possible. */
bool
vmm_grow_stack (void *fault_addr, void *esp)
{
  if (esp - fault_addr > STACK_GROW_LIMIT)
    return false;
  if (fault_addr < PHYS_BASE - STACK_MAXSIZE)
    return false;

  return vmm_create_anonymous (pg_round_down (fault_addr), true);
}

/* Get unused mapping id of current process. */
mapid_t
vmm_get_free_mapid (void)
{
  struct thread *cur;
  mapid_t id;
  struct list_elem *el;
  struct mmap_user_block *block;

  cur = thread_current ();
  id = 0;
  for (el = list_begin (&cur->mmap_blocks); el != list_end (&cur->mmap_blocks);
       el = list_next (el))
    {
      block = list_entry (el, struct mmap_user_block, elem);
      if (block->id != id)
        return id;
      id++;
    }

  return id;
}

/* Get `mmap_user_block` object of current process by map id. */
struct mmap_user_block *
vmm_get_mmap_user_block (mapid_t id)
{
  struct thread *cur;
  struct list_elem *el;
  struct mmap_user_block *block;

  cur = thread_current ();
  for (el = list_begin (&cur->mmap_blocks); el != list_end (&cur->mmap_blocks);
       el = list_next (el))
    {
      block = list_entry (el, struct mmap_user_block, elem);
      if (block->id == id)
        return block;
    }

  return NULL;
}

/* Populate the chunk list of `block` by mapping file contents to `upage`. */
bool
vmm_setup_user_block (struct mmap_user_block *block, void *upage)
{
  unsigned length, read_bytes, mmap_size, bytes_left;
  struct mmap_info *info;
  struct list_elem *el;
  struct thread *cur;

  if (pg_ofs (upage) != 0)
    return false;

  length = file_length (block->file);
  cur = thread_current ();
  for (read_bytes = 0; read_bytes < length; read_bytes += PGSIZE)
    {
      bytes_left = length - read_bytes;
      mmap_size = bytes_left < PGSIZE ? bytes_left : PGSIZE;
      info = vmm_create_file_map (upage + read_bytes, block->file, true, false,
                                  read_bytes, mmap_size);
      if (info == NULL)
        {
          while (!list_empty (&block->chunks))
            {
              el = list_pop_front (&block->chunks);
              info = list_entry (el, struct mmap_info, chunk_elem);
              vmm_unmap_from_frame (info);
              vmm_unlink_mapping_from_thread (cur, info);
            }
          return false;
        }
      list_push_back (&block->chunks, &info->chunk_elem);
    }

  return true;
}

/* Clean up `mmap_user_block` object. */
void
vmm_cleanup_user_block (struct mmap_user_block *block)
{
  struct list_elem *el;
  struct mmap_info *info;
  struct frame *frame;
  struct thread *cur;

  cur = thread_current ();
  while (!list_empty (&block->chunks))
    {
      el = list_pop_front (&block->chunks);
      info = list_entry (el, struct mmap_info, chunk_elem);
      vmm_deactivate_frame (info->frame);

      frame = info->frame;
      vmm_unmap_from_frame (info);
      vmm_unlink_mapping_from_thread (cur, info);
      if (list_empty (&frame->mappings))
        vmm_destroy_frame (frame);
    }
  list_remove (&block->elem);
  free (block);
}
