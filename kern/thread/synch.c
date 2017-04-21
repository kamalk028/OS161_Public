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
	KASSERT(lock != NULL);
	KASSERT(lock->lock_thread==NULL);

	//kfree();
	//lock->lock_thread = NULL;
	spinlock_cleanup(&lock->lock_splk);
	wchan_destroy(lock->lock_wchan);
	kfree(lock->lk_name);
	kfree(lock);

}

void
lock_acquire(struct lock *lock)
{

	// Write this
	KASSERT(lock != NULL);
	KASSERT(curthread->t_in_interrupt == false);
	
	if(lock_do_i_hold(lock))
	{
		return;
	}
	
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
	spinlock_acquire(&cv->cv_splk);
	KASSERT(wchan_isempty(cv->cv_wchan, &cv->cv_splk));
	spinlock_release(&cv->cv_splk);
	spinlock_cleanup(&cv->cv_splk);
	wchan_destroy(cv->cv_wchan);
	// We never asserted that the channel is empty when destroyed. Should we?
	
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
	//Debug line that ensures the wchan isn't empty.
	/*if (wchan_isempty(cv->cv_wchan, &cv->cv_splk))
	{
		panic("cv_signal called on empty cv.\n");
	}*/
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
	//Debug line that ensures the wchan isn't empty.
	/*if (wchan_isempty(cv->cv_wchan, &cv->cv_splk))
	{
		panic("cv_broadcast called on empty cv.\n");
	}*/
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
	spinlock_acquire(&cv->cv_splk);
	ret = wchan_isempty(cv->cv_wchan, &cv->cv_splk);
	spinlock_release(&cv->cv_splk);
	return ret;
}

struct rwlock * rwlock_create(const char *name)
{
	struct rwlock *rwlock;
	rwlock = kmalloc(sizeof(*rwlock));
	if(rwlock == NULL)
	{
		return NULL;
	}

	rwlock->rwlock_name = kstrdup(name);
	if(rwlock->rwlock_name == NULL)
	{
		kfree(rwlock);
		return NULL;
	}

	rwlock->cvariable = cv_create(name);
	if(rwlock->cvariable == NULL)
	{
		kfree(rwlock->rwlock_name);
		kfree(rwlock);
		return NULL;
	}

	rwlock->sleep_lock = lock_create(name);
	if(rwlock->sleep_lock == NULL)
	{
		cv_destroy(rwlock->cvariable);
		kfree(rwlock->rwlock_name);
		kfree(rwlock);
		return NULL;
	}

	/*rwlock->data parameters:
	* 0 = Nobody holds.
	* 1 = Any number of readers hold.
	*   rwlock->read_threadcount tracks how many readers.
	* 2 = Writer holds.
	*/
	rwlock->data=0;
	rwlock->write_thread=NULL;
	rwlock->read_threadcount=0;
	rwlock->is_anyone_waiting=false;

	return rwlock;

}

void rwlock_destroy(struct rwlock *rwlock)
{
	KASSERT(rwlock != NULL);
	KASSERT(rwlock->data==0);
	KASSERT(!rwlock->is_anyone_waiting);
	rwlock->write_thread = NULL;
	//spinlock_cleanup(&rwlock->splk);
	//array_cleanup(rwlock->read_threads);
	//threadlist_cleanup(&rwlock->read_threadlist);
	cv_destroy(rwlock->cvariable);
	lock_destroy(rwlock->sleep_lock);
	kfree(rwlock->rwlock_name);
	kfree(rwlock);
}

void rwlock_acquire_read(struct rwlock *rwlock)
{
	KASSERT(rwlock != NULL);
	
	lock_acquire(rwlock->sleep_lock);
	while(rwlock->data>1 || rwlock->is_anyone_waiting)
	{
		rwlock->is_anyone_waiting = true;
		cv_wait(rwlock->cvariable, rwlock->sleep_lock);
	}
	KASSERT(rwlock->data<=1);
	//KASSERT(!rwlock->is_anyone_waiting);
	rwlock->data=1;
	rwlock->read_threadcount+=1;
	//array_add(rwlock->read_threads, curthread, NULL);
	//threadlist_addtail(&rwlock->read_threadlist, curthread);
	lock_release(rwlock->sleep_lock);
	
}

void rwlock_release_read(struct rwlock *rwlock)
{
	KASSERT(rwlock != NULL);
	KASSERT(rwlock->data == 1);
	KASSERT(rwlock->write_thread==NULL);
	//KASSERT(do_i_hold_read(rwlock));
	lock_acquire(rwlock->sleep_lock);
	//unsigned num = array_num(rwlock->read_threads);
	if(rwlock->read_threadcount > 1)
	{
		rwlock->read_threadcount-=1;
	}
	else
	{
		KASSERT(rwlock->read_threadcount==1);
		rwlock->data=0;
		rwlock->is_anyone_waiting=false;
		rwlock->read_threadcount=0;
		cv_broadcast(rwlock->cvariable, rwlock->sleep_lock);
	}
	lock_release(rwlock->sleep_lock);
}

void rwlock_acquire_write(struct rwlock *rwlock)
{
	KASSERT(rwlock != NULL);
	lock_acquire(rwlock->sleep_lock);
	while(rwlock->data!=0)
	{
		rwlock->is_anyone_waiting = true;
		cv_wait(rwlock->cvariable, rwlock->sleep_lock);
	}
	KASSERT(rwlock->data==0);
	KASSERT(rwlock->read_threadcount==0);
	rwlock->data=2;
	rwlock->write_thread = curthread;
	lock_release(rwlock->sleep_lock);

}

void rwlock_release_write(struct rwlock *rwlock)
{
	KASSERT(rwlock != NULL);
	KASSERT(rwlock->data==2);
	//KASSERT(do_i_hold_write(rwlock));
	lock_acquire(rwlock->sleep_lock);
	rwlock->data=0;
	rwlock->is_anyone_waiting=false;
	rwlock->write_thread=NULL;
	cv_broadcast(rwlock->cvariable, rwlock->sleep_lock);
	lock_release(rwlock->sleep_lock);

}

