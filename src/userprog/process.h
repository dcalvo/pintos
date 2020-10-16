#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct child_thread {
    tid_t pid;                      /* PID. */
    struct list_elem elem;          /* List element. */
    bool waited;                    /* Is there a thread already waiting on this one? */
    bool exiting;                   /* If thread is trying to exit. */
    int exit_code;                  /* Integer representing how the process exited. */
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
