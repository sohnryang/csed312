#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "list.h"
#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_trigger_exit (int) NO_RETURN;
void process_exit (void);
void process_activate (void);

struct pcb *process_child_by_pid (tid_t);
int process_get_first_free_fd_num (void);
struct file_descriptor *process_get_fd (int);
list_less_func process_compare_fd_id;

#endif /* userprog/process.h */
