/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
	struct semaphore *sem;

	sem = kmalloc(sizeof(*sem));
	if (sem == NULL) {
		return NULL;
	}

	sem->sem_name = kstrdup(name);
	if (sem->sem_name == NULL) {
		kfree(sem);
		return NULL;
	}

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
	sem->sem_count = initial_count;

	return sem;
}

void
sem_destroy(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
	kfree(sem->sem_name);
	kfree(sem);
}

void
P(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
	while (sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
	}
	KASSERT(sem->sem_count > 0);
	sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

	sem->sem_count++;
	KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
	struct lock *lock;

	lock = kmalloc(sizeof(*lock));
	if (lock == NULL) {
		return NULL;
	}

	lock->lk_name = kstrdup(name);
	if (lock->lk_name == NULL) {
		kfree(lock);
		return NULL;
	}

	lock->lock_wchan = wchan_create(lock->lk_name);
	if(lock->lock_wchan == NULL )
	{
		kfree(lock->lk_name);
		kfree(lock);
		return NULL;
	}


	spinlock_init(&lock->lock_splk);
	lock->lock_data=1;//1 equals resource is free
	lock->lock_thread=NULL;
	HANGMAN_LOCKABLEINIT(&lock->lk_hangman, lock->lk_name);

	// add stuff here as needed

	return lock;
}

void
lock_destroy(struct lock *lock)
{
	KASSERT(lock->lock_thread==NULL);
	// add stuff here as needed : Have added some stuff

	//kfree();
	lock->lock_thread = NULL;
	spinlock_cleanup(&lock->lock_splk);
	wchan_destroy(lock->lock_wchan);
	kfree(lock->lk_name);
	kfree(lock);
	KASSERT(lock != NULL);
}

void
lock_acquire(struct lock *lock)
{

	// Write this
	KASSERT(lock != NULL);
	KASSERT(curthread->t_in_interrupt == false);
	
	spinlock_acquire(&lock->lock_splk);

	/* Call this (atomically) before waiting for a lock */
	//For debugging deadlocks, hopefully I put it in the right place.
	HANGMAN_WAIT(&curthread->t_hangman, &lock->lk_hangman);
	while(lock->lock_data == 0)
	{
		wchan_sleep(lock->lock_wchan, &lock->lock_splk);
	}
	KASSERT(lock->lock_data == 1);
	lock->lock_data = 0;
	
	/* Call this (atomically) once the lock is acquired */
	//For debugging deadlocks, hopefully I put it in the right place.
	HANGMAN_ACQUIRE(&curthread->t_hangman, &lock->lk_hangman);
	
	//We want to have the current thread be an object which lock can access.
	lock->lock_thread = curthread;
	spinlock_release(&lock->lock_splk);
	//(void)lock;  // suppress warning until code gets written

}

void
lock_release(struct lock *lock)
{

	// Write this
	KASSERT(lock != NULL);
	KASSERT(lock_do_i_hold(lock));
	
	spinlock_acquire(&lock->lock_splk);
	KASSERT(lock->lock_data == 0);
	lock->lock_thread=NULL;
	lock->lock_data = 1;
	
	/* Call this (atomically) when the lock is released */
	HANGMAN_RELEASE(&curthread->t_hangman, &lock->lk_hangman);
	
	wchan_wakeone(lock->lock_wchan, &lock->lock_splk);
	spinlock_release(&lock->lock_splk);
	//(void)lock;  // suppress warning until code gets written
}

bool
lock_do_i_hold(struct lock *lock)
{
	// Write this
	KASSERT(lock != NULL);
	//We have two methods of returning true. This may generate unexpected results.
	//if (!CURCPU_EXISTS()) {
	//	return true;
	//}
	if(lock->lock_thread!=NULL && curthread == lock->lock_thread)
	{
		return true;
	}
	else
	{
		return false;
	}
	//(void)lock;  // suppress warning until code gets written

	//return true; // dummy until code gets written
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
	struct cv *cv;

	cv = kmalloc(sizeof(*cv));
	if (cv == NULL) {
		return NULL;
	}

	cv->cv_name = kstrdup(name);
	if (cv->cv_name==NULL) {
		kfree(cv);
		return NULL;
	}

	cv->cv_wchan = wchan_create(cv->cv_name);
	if(cv->cv_wchan == NULL)
	{
		kfree(cv->cv_name);
		kfree(cv);
		return NULL;
	}
	spinlock_init(&cv->cv_splk);

	// add stuff here as needed
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	KASSERT(cv != NULL);
	spinlock_cleanup(&cv->cv_splk);
	wchan_destroy(cv->cv_wchan);
	// add stuff here as needed
	
	//lock_destroy(lock);//Not needed b/c locks are passed to CV.
	kfree(cv->cv_name);
	kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	// Write this
	KASSERT(cv != NULL);
	KASSERT(lock != NULL);
	KASSERT(curthread->t_in_interrupt == false);
	
	//This spinlock_acquire does not cause a deadlock b/c wchan_sleep
	//  will open the lock again.
	spinlock_acquire(&cv->cv_splk);

	//The kernel might panic if this lock isn't being held.
	//Driver code must obtain this lock before calling this function.
	//It is released to prevent deadlocking.
	lock_release(lock);

	//This if statement is here to debug only.
	/*if(!wchan_isempty(cv->cv_wchan, &cv->cv_splk))
	{
		wchan_wakeone(cv->cv_wchan, &cv->cv_splk);
	}*/
	wchan_sleep(cv->cv_wchan, &cv->cv_splk);
	spinlock_release(&cv->cv_splk);
	lock_acquire(lock);
//	(void)cv;    // suppress warning until code gets written
//	(void)lock;  // suppress warning until code gets written
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	// Write this
	KASSERT(cv != NULL);
	KASSERT(lock != NULL);
	KASSERT(curthread->t_in_interrupt == false);
	//Again, driver code MUST obtain this lock before calling this!
	KASSERT(lock_do_i_hold(lock));
	spinlock_acquire(&cv->cv_splk);
	//Need to add a debug line that ensures the wchan isn't empty.
	wchan_wakeone(cv->cv_wchan, &cv->cv_splk);
	spinlock_release(&cv->cv_splk);
	
//	(void)cv;    // suppress warning until code gets written
//	(void)lock;  // suppress warning until code gets written
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	// Write this
	KASSERT(cv != NULL);
	KASSERT(lock != NULL);
	KASSERT(curthread->t_in_interrupt == false);
	KASSERT(lock_do_i_hold(lock));
	spinlock_acquire(&cv->cv_splk);
	//Need same debug line.
	wchan_wakeall(cv->cv_wchan, &cv->cv_splk);
	spinlock_release(&cv->cv_splk);
//	(void)cv;    // suppress warning until code gets written
//	(void)lock;  // suppress warning until code gets written
}

bool
cv_isempty(struct cv *cv, struct lock *lock)
{
	bool ret;
	//Lock should be passed in. Check to see if it is held.
	KASSERT(lock_do_i_hold(lock));
	ret = wchan_isempty(cv->cv_wchan, &cv->cv_splk);
	return ret;
}

//////////////////////////////////////////////////////////////
//
//RW-Locks

struct rwlock *
rwlock_create(const char *name)
{
	struct rwlock *rwlock;

	rwlock = kmalloc(sizeof(*rwlock));
	if (rwlock == NULL) {
		return NULL;
	}

	rwlock->lk_name = kstrdup(name);
	if (rwlock->rwlk_name == NULL) {
		kfree(rwlock);
		return NULL;
	}

	rwlock->rwlock_write_wchan = wchan_create(rwlock->rwlk_name);
	if(rwlock->rwlock_write_wchan == NULL )
	{
		kfree(rwlock->rwlk_name);
		kfree(rwlock);
		return NULL;
	}
	
	//Two different wait channels for each type of lock acquire/release
	rwlock->rwlock_read_wchan = wchan_create(rwlock->rwlk_name);
	if(rwlock->rwlock_read_wchan == NULL )
	{
		kfree(rwlock->rwlk_name);
		kfree(rwlock);
		return NULL;
	}


	spinlock_init(&rwlock->rwlock_splk);
	
	//rwlock_data is a signed int. -1 means obtained by writer.
	//  0 means free. 1 or more means obtained by readers.
	//  There should never be enough threads to cause overflow.
	rwlock->rwlock_data=0;
	//rwlock->rwlock_thread=NULL;

	// add stuff here as needed

	return rwlock;
}

void
rwlock_destroy(struct rwlock *rwlock)
{
	KASSERT(rwlock->rwlock_thread==NULL);
	// add stuff here as needed : Have added some stuff

	//kfree();
	//rwlock->rwlock_thread = NULL;
	spinlock_cleanup(&lock->lock_splk);
	wchan_destroy(rwlock->rwlock_wirte_wchan);
	wchan_destriy(rwlock->rwlock_read_wchan);
	kfree(rwlock->rwlk_name);
	kfree(rwlock);
	KASSERT(rwlock != NULL);
}

void
rwlock_acquire_read(struct rwlock *rwlock)
{
	KASSERT(rwlock != NULL);
	KASSERT(curthread->t_in_interrupt == false);
	
	spinlock_acquire(&lock->lock_splk);

	while(rwlock->rwlock_data == -1)
	{
		wchan_sleep(rwlock->rwlock_read_wchan, &rwlock->rwlock_splk);
	}
	//The only time a reader thread cannot acquire the lock is when a writer has it.
	KASSERT(rwlock->rwlock_data >= 0);
	rwlock->rwlock_data++;
	
	//If we later implement do_i_hold, we'll want an array for this.
	//rwlock->rwlock_thread = curthread;
	spinlock_release(&rwlock->rwlock_splk);
}

void
rwwlock_release_read(struct rwlock *rwlock)
{
	KASSERT(rwlock != NULL);
	//KASSERT(rwlock_do_i_hold(lock));
	
	spinlock_acquire(&rwlock->rwlock_splk);
	KASSERT(rwlock->rwlock_data > 0);
	//rwlock->rwlock_thread=NULL;
	rwlock->rwlock_data--;
	//Wake a thread in the write_wchan, if one exists.
	if(!wchan_isempty(rwlock->rwlock_write_wchan, &rwlock->rwlock_splk))
	{
		wchan_wakeone(rwlock->rwlock_write_wchan, &rwlock->rwlock_splk);
	}
	//No need to wake threads in read channel, since they would alread be active.
	
	spinlock_release(&rwlock->rwlock_splk);

}

void
rwlock_acquire_write(struct rwlock *rwlock)
{
	KASSERT(rwlock != NULL);
	KASSERT(curthread->t_in_interrupt == false);
	
	spinlock_acquire(&lock->lock_splk);

	while(rwlock->rwlock_data != 0)
	{
		wchan_sleep(rwlock->rwlock_write_wchan, &rwlock->rwlock_splk);
	}
	//A writer thread should never acquire the lock unless nothing else has it.
	KASSERT(rwlock->rwlock_data == 0);
	rwlock->rwlock_data = -1;
	
	//If we later implement do_i_hold, we'll want this.
	//rwlock->rwlock_thread = curthread;
	spinlock_release(&rwlock->rwlock_splk);
}

void
rwlock_release_write(struct lock *lock)
{
	KASSERT(rwlock != NULL);
	//KASSERT(rwlock_do_i_hold(lock));
	
	spinlock_acquire(&rwlock->rwlock_splk);
	KASSERT(rwlock->rwlock_data == -1);
	//rwlock->rwlock_thread=NULL;
	rwlock->rwlock_data = 0;
	if(!wchan_isempty(rwlock->rwlock_read_wchan, &rwlock->rwlock_splk))
	{
		wchan_wakeall(rwlock->rwlock_read_wchan, &rwlock->rwlock_splk);
	}
	else if(!wchan_isempty(rwlock->rwlock_write_wchan, &rwlock->rwlock_splk))
	{
		wchan_wakeone(rwlock->rwlock_write_wchan, &rwlock->rwlock_splk);
	}
	spinlock_release(&rwlock->rwlock_splk);
}

