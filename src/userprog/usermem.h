#ifndef USERPROG_USERMEM_H
#define USERPROG_USERMEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int usermem_copy_byte_from_user (const uint8_t *);
bool usermem_copy_byte_to_user (uint8_t *, uint8_t);

void *usermem_memcpy_from_user (void *, const void *, size_t);
void *usermem_memcpy_to_user (void *, const void *, size_t);

int usermem_strlen (const char *);
int usermem_strlcpy_from_user (char *, const char *, size_t);
char *usermem_strdup_from_user (const char *);

#endif
