#include "userprog/usermem.h"

#include <stdbool.h>
#include <stdint.h>
#include "stddef.h"
#include "threads/vaddr.h"

/* Check if `uptr` is a valid pointer pointing to user space memory. */
static bool
is_valid_uptr (const void *uptr)
{
  return uptr < PHYS_BASE;
}

/* Check if `n`-byte data at `uptr` is contained in user space memory. */
static bool
contained_in_user (const void *uptr, size_t n)
{
  return is_valid_uptr (uptr) && is_valid_uptr ((const uint8_t *)uptr + n - 1);
}

/* Read a byte from `usrc`. Return -1 if page fault occurred while copying. */
int
usermem_copy_byte_from_user (const uint8_t *usrc)
{
  int res;

  if (!is_valid_uptr (usrc))
    return -1;

  asm ("movl $1f, %0\n\t"
       "movzbl %1, %0\n\t"
       "1:"
       : "=&a"(res)
       : "m"(*usrc));
  return res;
}

/* Write a byte to `udst`. Return true if success, false if failure. */
bool
usermem_copy_byte_to_user (uint8_t *udst, uint8_t byte)
{
  int error_code;

  if (!is_valid_uptr (udst))
    return false;

  error_code = 0;
  asm ("movl $1f, %0\n\t"
       "movb %b2, %1\n\t"
       "1:"
       : "=&a"(error_code), "=m"(*udst)
       : "q"(byte));
  return error_code != -1;
}

/* Copy `n` bytes from `usrc` to `dst`. `usrc` must be a pointer to user space
   memory. Returns `dst` on success, NULL on failure. */
void *
usermem_memcpy_from_user (void *dst, const void *usrc, size_t n)
{
  uint8_t *dst_byte;
  const uint8_t *src_byte;
  int byte;

  if (!contained_in_user (usrc, n))
    return NULL;

  dst_byte = dst;
  src_byte = usrc;
  for (size_t i = 0; i < n; i++)
    {
      byte = usermem_copy_byte_from_user (src_byte);
      if (byte == -1)
        return NULL;
      *dst_byte = byte;
      dst_byte++;
      src_byte++;
    }

  return dst;
}

/* Copy `n` bytes from `src` to `udst`. `udst` must be a pointer to user space
   memory. Returns `udst` on success, NULL on failure. */
void *
usermem_memcpy_to_user (void *udst, const void *src, size_t n)
{
  uint8_t *dst_byte;
  const uint8_t *src_byte;
  bool res;

  if (!contained_in_user (udst, n))
    return NULL;

  dst_byte = udst;
  src_byte = src;
  for (size_t i = 0; i < n; i++)
    {
      res = usermem_copy_byte_to_user (dst_byte, *src_byte);
      if (!res)
        return NULL;
      dst_byte++;
      src_byte++;
    }

  return udst;
}

/* Calculate length of the `ustr`. Returns -1 on error. */
int
usermem_strlen (const char *ustr)
{
  const char *p;
  int byte;

  for (p = ustr; (byte = usermem_copy_byte_from_user ((const void *)p)) != '\0';
       p++)
    {
      if (byte == -1)
        return -1;
      continue;
    }
  return p - ustr;
}

/* Copy NULL terminated string from `usrc` to `dst`. Returns copied length on
   success, -1 on failure. Copies at most `n` bytes. */
int
usermem_strlcpy_from_user (char *dst, const char *usrc, size_t n)
{
  int src_len;
  void *res;

  src_len = usermem_strlen (usrc);
  if (src_len == -1)
    return -1;
  if (n > 0)
    {
      size_t dst_len = n - 1;
      if ((size_t)src_len < dst_len)
        dst_len = src_len;
      res = usermem_memcpy_from_user (dst, usrc, dst_len);

      if (res == NULL)
        return -1;
      dst[dst_len] = '\0';
    }

  return src_len;
}
