/*
 * Copyright (c) 2001, 2002, 2009
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
 * Driver code is in kern/tests/synchprobs.c We will
 * replace that file. This file is yours to modify as you see fit.
 *
 * You should implement your solution to the whalemating problem below.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

static struct cv *cv_male;
static struct cv *cv_female;
static struct cv *cv_mmaker;
static struct lock *lock_male;
static struct lock *lock_female;
static struct lock *lock_mmaker;

/*
 * Called by the driver during initialization.
 */

void whalemating_init() {
	cv_male = cv_create("cv for males");
	if (cv_male == NULL)
	{
		panic("cv_males creation failed!\n");
	}
	
	lock_male = lock_create("lock for male cv");
	if (lock_male == NULL)
	{
		panic("lock_males creation failed!\n");
	}
	
	cv_female = cv_create("cv for females");
	if (cv_female == NULL)
	{
		panic("cv_females creation failed!\n");
	}
	
	lock_female = lock_create("lock for female cv");
	if (lock_female == NULL)
	{
		panic("lock_females creation failed!\n");
	}
	
	cv_mmaker = cv_create("cv for match makers");
	if (cv_mmaker == NULL)
	{
		panic("cv_mmakers creation failed!\n");
	}
	
	lock_mmaker = lock_create("lock for mmaker cv");
	if (lock_mmaker == NULL)
	{
		panic("lock_mmakers creation failed!\n");
	}
	
	return;
}

/*
 * Called by the driver during teardown.
 */

void
whalemating_cleanup() {
	cv_destroy(cv_male);
	lock_destroy(lock_male);
	cv_destroy(cv_female);
	lock_destroy(lock_female);
	cv_destroy(cv_mmaker);
	lock_destroy(lock_mmaker);
	return;
}

void
male(uint32_t index)
{
	(void)index;
	male_start(index);
	//uint32_t is an unsigned int managed by the driver.
	//Just pass the start and end functions the index variable.
	/*
	 * Implement this function by calling male_start and male_end when
	 * appropriate.
	 */
	
	//MAY NEED TO INITIALIZE OBJECTS! This code will be purely to get the idea down.
	/*
	We have a CV for each role. The first thing each function must do is
	place a thread in the respective CV, to sleep until the other two CVs
	also have a thread sleeping. Once all three CVs have one or more
	threads, call cv_signal on all of them. Then each thread calls its
	start and end function.
	
	WOULD WC'S BE BETTER? Maybe, WC's have the wchan_isempty()
	function. But, we can test our CV implementation this way.

	HOW DO WE KNOW WHEN THE CV'S HAVE THREADS?!
	I wrote a cv_isempty function to resolve this.
	
	CAN MORE THAN ONE GROUP OF WHALES MATE AT A TIME?
	Probably, since it's called a synch problem, and we want it to run fast.
	However, I believe by calling male_start(index), a lock is obtained,
	preventing any further mating. As long as the number of threads
	waiting on each of those locks stays the same as one another...
	
	Note that all three functions need access to all three CVs with
	how I plan to use an if statement.
	*/
	
	lock_acquire(lock_female);
	lock_acquire(lock_mmaker);
	//The locks are acquired right now so that no two threads end up
	//  satisfying the condition at once. Otherwise, one thread may
	//  empty out a cv while the other thinks it's full!
	if(!cv_isempty(cv_female, lock_female) && !cv_isempty(cv_mmaker, lock_mmaker))
	{//THERE MUST HAVE BEEN A DEADLOCK!
	//I DON'T KNOW HOW! Whenever mmaker_lock is held, the holder will stay awake and finish their task. Then release it.
	//HOWEVER, threads waking from cv_wait will need their locks to continue. Can those locks get stolen?
		cv_signal(cv_female, lock_female);
		cv_signal(cv_mmaker, lock_mmaker);

		lock_release(lock_female);
		lock_release(lock_mmaker);
	}
	else
	{
		lock_release(lock_female);
		lock_release(lock_mmaker);

		lock_acquire(lock_male);
		//Lock released within cv_wait, so no deadlocking here.
		cv_wait(cv_male, lock_male);
		lock_release(lock_male);
	}
	
	male_end(index);
	return;
}

void
female(uint32_t index)
{
	(void)index;
	female_start(index);
	/*
	 * Implement this function by calling female_start and female_end when
	 * appropriate.
	 */
	lock_acquire(lock_male);
	lock_acquire(lock_mmaker);
	
	if(!cv_isempty(cv_male, lock_male) && !cv_isempty(cv_mmaker, lock_mmaker))
	{
		cv_signal(cv_male, lock_male);
		cv_signal(cv_mmaker, lock_mmaker);

		lock_release(lock_male);
		lock_release(lock_mmaker);
	}
	else
	{
		lock_release(lock_male);
		lock_release(lock_mmaker);

		lock_acquire(lock_female);
		//Lock released within cv_wait, so no deadlocking here.
		cv_wait(cv_female, lock_female);
		lock_release(lock_female);
	}
	
	female_end(index);
	return;
}

void
matchmaker(uint32_t index)
{
	(void)index;
	matchmaker_start(index);
	/*
	 * Implement this function by calling matchmaker_start and matchmaker_end
	 * when appropriate.
	 */
	lock_acquire(lock_female);
	lock_acquire(lock_male);
	
	if(!cv_isempty(cv_female, lock_female) && !cv_isempty(cv_male, lock_male))
	{
		cv_signal(cv_female, lock_female);
		cv_signal(cv_male, lock_male);

		lock_release(lock_female);
		lock_release(lock_male);
	}
	else
	{
		lock_release(lock_female);
		lock_release(lock_male);

		lock_acquire(lock_mmaker);
		//Lock released within cv_wait, so no deadlocking here.
		cv_wait(cv_mmaker, lock_mmaker);
		lock_release(lock_mmaker);
	}
	
	matchmaker_end(index);
	return;
}

