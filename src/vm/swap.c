#include "vm/swap.h"

#include "devices/block.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/mmap.h"

#include <bitmap.h>
#include <debug.h>
#include <list.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

static bool swap_present;
static struct lock swap_lock;
static struct list active_frames;
static struct block *swap_block_dev;
static struct bitmap *swap_block_map;
static struct list_elem *clock_hand;

/* Initialize swap manager. */
void
swap_init (void)
{
  block_sector_t swap_size;

  lock_init (&swap_lock);
  list_init (&active_frames);
  clock_hand = NULL;
  swap_present = false;

  swap_block_dev = block_get_role (BLOCK_SWAP);
  if (swap_block_dev == NULL)
    return;
  swap_present = true;

  swap_size = block_size (swap_block_dev);
  swap_block_map = bitmap_create (swap_size);
}

void
swap_acquire_lock (void)
{
  lock_acquire (&swap_lock);
}

void
swap_release_lock (void)
{
  lock_release (&swap_lock);
}

void swap_release_lock (void);

/* Register frame to swap manager. */
void
swap_register_frame (struct frame *frame)
{
  list_push_back (&active_frames, &frame->global_elem);
  if (clock_hand == NULL)
    clock_hand = &frame->global_elem;
}

/* Unregister frame from swap manager. */
void
swap_unregister_frame (struct frame *frame)
{
  if (clock_hand == &frame->global_elem)
    {
      clock_hand = list_next (clock_hand);
      if (clock_hand == list_end (&active_frames))
        clock_hand = NULL;
    }
  list_remove (&frame->global_elem);
}

static bool
check_and_clear_accessed_bit (struct frame *frame)
{
  struct thread *cur;
  struct list_elem *el;
  struct mmap_info *info;
  bool accessed;

  cur = thread_current ();
  accessed = false;
  for (el = list_begin (&frame->mappings); el != list_end (&frame->mappings);
       el = list_next (el))
    {
      info = list_entry (el, struct mmap_info, elem);
      if (!pagedir_is_accessed (cur->pagedir, info->upage))
        continue;

      accessed = true;
      pagedir_set_accessed (cur->pagedir, info->upage, false);
    }
  return accessed;
}

/* Find victim frame. */
struct frame *
swap_find_victim (void)
{
  ASSERT (swap_present);

  if (list_empty (&active_frames))
    return NULL;

  while (check_and_clear_accessed_bit (
      list_entry (clock_hand, struct frame, global_elem)))
    {
      clock_hand = list_next (clock_hand);
      if (clock_hand == list_end (&active_frames))
        clock_hand = list_begin (&active_frames);
    }

  return list_entry (clock_hand, struct frame, global_elem);
}

/* Write frame to swap space. */
void
swap_write_frame (struct frame *frame)
{
  size_t sector;
  int i;

  ASSERT (swap_present);

  sector = bitmap_scan_and_flip (swap_block_map, 0, SECTORS_PER_PAGE, false);
  ASSERT (sector != BITMAP_ERROR);
  frame->swap_sector = sector;

  for (i = 0; i < SECTORS_PER_PAGE; i++)
    block_write (swap_block_dev, sector + i,
                 (uint8_t *)frame->kpage + i * BLOCK_SECTOR_SIZE);
}

/* Read frame from swap space. */
void
swap_read_frame (struct frame *frame)
{
  int i;

  ASSERT (swap_present);

  ASSERT (
      bitmap_count (swap_block_map, frame->swap_sector, SECTORS_PER_PAGE, true)
      == SECTORS_PER_PAGE);
  for (i = 0; i < SECTORS_PER_PAGE; i++)
    block_read (swap_block_dev, frame->swap_sector + i,
                (uint8_t *)frame->kpage + i * BLOCK_SECTOR_SIZE);

  bitmap_set_multiple (swap_block_map, frame->swap_sector, SECTORS_PER_PAGE,
                       false);
  frame->swap_sector = -1;
}

/* Free frame from swap space. */
void
swap_free_frame (struct frame *frame)
{
  if (swap_present)
    bitmap_set_multiple (swap_block_map, frame->swap_sector, SECTORS_PER_PAGE,
                         false);
  frame->swap_sector = -1;
}
