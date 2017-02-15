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

/*
 * Called by the driver during initialization.
 */

void whalemating_init() {
	//I am not sure if we should modify this function call.
	cv_create(cv_males);
	lock_create(lock_males);
	cv_create(cv_females);
	lock_create(lock_females);
	cv_create(cv_mmakers);
	lock_create(lock_mmakers);
	return;//I'm not sure if these objects are maintained after exiting this function.
}

/*
 * Called by the driver during teardown.
 */
//REMEMBER TO ADJUST synchprobs.c BEFORE TESTING IF FUNCTION ARGS ARE ADDED!

void
whalemating_cleanup() {
	cv_destroy(cv_males);
	lock_destroy(lock_males);
	cv_destroy(cv_females);
	lock_destroy(lock_females);
	cv_destroy(cv_mmakers);
	lock_destroy(lock_mmakers);
	return;
}

void
male(uint32_t index)
{
	(void)index;
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
	if(!cv_isempty(cv_female) && !cv_isempty(cv_mmaker))
	{
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
	
	male_start(index);
	male_end(index);
	return;
}

void
female(uint32_t index)
{
	(void)index;
	/*
	 * Implement this function by calling female_start and female_end when
	 * appropriate.
	 */
	lock_acquire(lock_male);
	lock_acquire(lock_mmaker);
	//The locks are acquired right now so that no two threads end up
	//  satisfying the condition at once. Otherwise, one thread may
	//  empty out a cv while the other thinks it's full!
	if(!cv_isempty(cv_male) && !cv_isempty(cv_mmaker))
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
	
	female_start(index);
	female_end(index);
	return;
}

void
matchmaker(uint32_t index)
{
	(void)index;
	/*
	 * Implement this function by calling matchmaker_start and matchmaker_end
	 * when appropriate.
	 */
	lock_acquire(lock_female);
	lock_acquire(lock_male);
	//The locks are acquired right now so that no two threads end up
	//  satisfying the condition at once. Otherwise, one thread may
	//  empty out a cv while the other thinks it's full!
	if(!cv_isempty(cv_female) && !cv_isempty(cv_male))
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
	
	mmaker_start(index);
	mmaker_end(index);
	return;
}

