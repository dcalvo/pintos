/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

void sema_push_waiting_thread (struct list *waiters, struct list_elem *elem);
void cond_push_waiting_thread (struct list *waiters, struct list_elem *elem);
bool cond_greater_comp (const struct list_elem *s1, const struct list_elem *s2, void *aux UNUSED);
bool lock_greater_comp (const struct list_elem *l1, const struct list_elem *l2, void *aux UNUSED);
void lock_update_priority (struct lock *lock);



/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      struct thread *t = thread_current();
      sema_push_waiting_thread (&sema->waiters, &t->elem);
      // if the thread is requesting a lock, update the lock and lock holder's priority
      if (t->requesting != NULL)
        {
          struct lock *lock = t->requesting;
          ASSERT (&lock->semaphore == sema); // make sure this lock actually owns the sema, no funny business
          while (lock != NULL)
            {
              lock_update_priority (lock);
              holder_update_priority (lock->holder);
              lock = lock->holder->requesting;
            }
        }
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  sema->value++;
  if (!list_empty (&sema->waiters)) 
  {
    list_sort(&sema->waiters, &priority_greater_comp, NULL);
    thread_unblock (list_entry (list_pop_front (&sema->waiters),
                                struct thread, elem));
  }
  intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
  lock->priority = 0;
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  thread_current ()->requesting = lock; // officially request the lock

  sema_down (&lock->semaphore); // wait for acquiring lock

  list_insert_ordered(&thread_current ()->locks, &lock->elem, &lock_greater_comp, NULL); // add lock to held locks
  lock->holder = thread_current ();
  thread_current ()->requesting = NULL;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  list_remove(&lock->elem); // remove from thread's list of held locks
  holder_update_priority(lock->holder);
  lock->holder = NULL;
  lock_update_priority(lock); // drop former holder's priority if necessary
  sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
    int priority;                       /* Priority. */
  };

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  waiter.priority = thread_current ()->priority;
  cond_push_waiting_thread (&cond->waiters, &waiter.elem);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)) 
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                          struct semaphore_elem, elem)->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}

/* Wrapper for list_insert_ordered to maintain priority queue ordering into a sempahore.*/
void
sema_push_waiting_thread (struct list *waiters, struct list_elem *elem)
{
  list_insert_ordered (waiters, elem, &priority_greater_comp, NULL);
}

/* Compares two thread's priorities for use in list_sort.
   Take very careful note of the > in the return. list takes a less compare,
   but we intentionally give it a greater compare.
   This is so we sort from highest priority to lowest, and also ensure 
   that equivalent priority threads are rotated out in round-robin style. */
bool
cond_greater_comp (const struct list_elem *s1, const struct list_elem *s2, void *aux UNUSED)
{
  struct semaphore_elem *s1_struct = (list_entry (s1, struct semaphore_elem, elem));
  struct semaphore_elem *s2_struct = (list_entry (s2, struct semaphore_elem, elem));
  int s1_priority = s1_struct->priority;
  int s2_priority = s2_struct->priority;
  return s1_priority > s2_priority;
}

/* See above. */
bool
lock_greater_comp (const struct list_elem *l1, const struct list_elem *l2, void *aux UNUSED)
{
  struct lock *l1_struct = (list_entry (l1, struct lock, elem));
  struct lock *l2_struct = (list_entry (l2, struct lock, elem));
  int l1_priority = l1_struct->priority;
  int l2_priority = l2_struct->priority;
  return l1_priority > l2_priority;
}


/* Wrapper for list_insert_ordered to maintain priority queue ordering into a cond.*/
void
cond_push_waiting_thread (struct list *waiters, struct list_elem *elem)
{
  list_insert_ordered (waiters, elem, &cond_greater_comp, NULL);
}

/* Update locks's priority to its highest waiter priority. */
void
lock_update_priority (struct lock *lock)
{
  struct semaphore *sema = &lock->semaphore;
  if (!list_empty(&sema->waiters))
    {
      list_sort(&sema->waiters, &priority_greater_comp, NULL);
      lock->priority = list_entry (list_front (&sema->waiters), struct thread, elem)->priority;
    }
  else
    lock->priority = PRI_MIN; // ensure released locks don't raise priorities
}

/* Update lock holder's priority to its highest lock priority (or resets it). */
void
holder_update_priority (struct thread *t)
{
  if (!list_empty(&t->locks))
    {
      list_sort(&t->locks, &lock_greater_comp, NULL);
      int highest_held_priority = list_entry (list_front (&t->locks), struct lock, elem)->priority;
      if (highest_held_priority >= t->base_priority)
        t->priority = highest_held_priority; 
    }
  else
    t->priority = t->base_priority;
}

bool
waiter_blocked (struct condition *cond)
{
  struct list *s_l = &cond->waiters;
  if (list_empty (s_l))
    return false;
  struct list_elem *s_l_e = list_front (s_l);
  struct semaphore_elem *s_e = list_entry (s_l_e, struct semaphore_elem, elem);
  struct semaphore *s = &s_e->semaphore;
  struct list *t_l = &s->waiters;
  if (list_empty (t_l))
    return false;
  struct list_elem *t_l_e = list_front (t_l);
  struct thread *t = list_entry (t_l_e, struct thread, elem);
  return t->status == THREAD_BLOCKED;
}
