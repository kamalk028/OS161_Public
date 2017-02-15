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
 * Driver code is in kern/tests/synchprobs.c We will replace that file. This
 * file is yours to modify as you see fit.
 *
 * You should implement your solution to the stoplight problem below. The
 * quadrant and direction mappings for reference: (although the problem is, of
 * course, stable under rotation)
 *
 *   |0 |
 * -     --
 *    01  1
 * 3  32
 * --    --
 *   | 2|
 *
 * As way to think about it, assuming cars drive on the right: a car entering
 * the intersection from direction X will enter intersection quadrant X first.
 * The semantics of the problem are that once a car enters any quadrant it has
 * to be somewhere in the intersection until it call leaveIntersection(),
 * which it should call while in the final quadrant.
 *
 * As an example, let's say a car approaches the intersection and needs to
 * pass through quadrants 0, 3 and 2. Once you call inQuadrant(0), the car is
 * considered in quadrant 0 until you call inQuadrant(3). After you call
 * inQuadrant(2), the car is considered in quadrant 2 until you call
 * leaveIntersection().
 *
 * You will probably want to write some helper functions to assist with the
 * mappings. Modular arithmetic can help, e.g. a car passing straight through
 * the intersection entering from direction X will leave to direction (X + 2)
 * % 4 and pass through quadrants X and (X + 3) % 4.  Boo-yah.
 *
 * Your solutions below should call the inQuadrant() and leaveIntersection()
 * functions in synchprobs.c to record their progress.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

/*
   static variable declaration
   to be used in solving intersection problem
*/

static struct lock *quad_lock[4];
static struct cv *cv;
static struct lock *cv_lock;
static struct spinlock spinlock;
static volatile int car_in_quad[4];




/*
 * Called by the driver during initialization.
 */

void destroy_quadlocks()
{
	int i;
	for(i=3; i<=0; i--)
	{
		lock_destroy(quad_lock[i]);
	}
}

void
stoplight_init() {
	int i;
	for(i=0; i<4; i++)
	{
		quad_lock[i] = lock_create("quadrant lock");
		if(quad_lock == NULL)
		{
			int j = i;
			i=i-1;
			while(i<=0)
			{
				lock_destroy(quad_lock[i]);
				i--;	
			}
			panic("lock_create returned NULL, while creating lock for quadrant number: %d\n", j);
			return;
		}
	}
	cv = cv_create("Stoplight cv");
	if(cv == NULL)
	{
		destroy_quadlocks();
		panic("cv_create returned NULL\n");
	}
	cv_lock = lock_create("cv_lock");
	if(cv_lock == NULL)
	{
		destroy_quadlocks();
		cv_destroy(cv);
		panic("lock_create returned NULL while creating cv_lock\n");
	}
	spinlock_init(&spinlock);
	for(i=0; i<4; i++)
	{
		car_in_quad[i] = -1;
	}
	return;
}

/*
 * Called by the driver during teardown.
 */

void stoplight_cleanup() {
	destroy_quadlocks();
	cv_destroy(cv);
	lock_destroy(cv_lock);
	spinlock_cleanup(&spinlock);
	return;
}

void
turnright(uint32_t direction, uint32_t index)
{
	/*
	 * The car will enter only one quadrant.
	 * The quadrant number will be the same as the direction.
	 */

	KASSERT(direction<=3);
	int q = direction;
	int idx = index;
	while(1)
	{
		spinlock_acquire(&spinlock);
		if(car_in_quad[q] < 0)
		{
			lock_acquire(quad_lock[q]);
			KASSERT(lock_do_i_hold(quad_lock[q]));
			car_in_quad[q] = idx;
			spinlock_release(&spinlock);
			
			inQuadrant(q,index);
			leaveIntersection(index);
			
			KASSERT(car_in_quad[q] == idx);
			
			spinlock_acquire(&spinlock);
			car_in_quad[q] = -1;
			lock_release(quad_lock[q]);
			spinlock_release(&spinlock);
			
			break;
		}
		else
		{
			spinlock_release(&spinlock);
			lock_acquire(cv_lock);
			cv_wait(cv, cv_lock);
			lock_release(cv_lock);
		}
	}
	lock_acquire(cv_lock);
	cv_broadcast(cv, cv_lock);
	lock_release(cv_lock);




	//(void)direction;
	//(void)index;
	/*
	 * Implement this function.
	 */
	return;
}
void
gostraight(uint32_t direction, uint32_t index)
{
	
	/*
	 * The car will enter two quadrants.
	 * First quadrant will be same as direction. q=direction
	 * Second quadrant will be ((direction+3)%4). q1= ((direction+3)%4) 
	 */

	KASSERT(direction<=3);
	int q = direction;
	int q1 = ((q+3)%4);
	int idx = index;

	while(1)
	{
		spinlock_acquire(&spinlock);
		if(car_in_quad[q] < 0 && car_in_quad[q1] < 0)
		{
			lock_acquire(quad_lock[q]);
			lock_acquire(quad_lock[q1]);
			KASSERT(lock_do_i_hold(quad_lock[q]));
			KASSERT(lock_do_i_hold(quad_lock[q1]));
			car_in_quad[q] = idx;
			car_in_quad[q1] = idx;
			spinlock_release(&spinlock);
			
			
			inQuadrant(q,index);
			inQuadrant(q1,index);
			leaveIntersection(index);
			
			
			KASSERT(car_in_quad[q]==idx);
			KASSERT(car_in_quad[q1]==idx);
			
			
			spinlock_acquire(&spinlock);
			car_in_quad[q] = -1;
			car_in_quad[q1] = -1;
			lock_release(quad_lock[q]);
			lock_release(quad_lock[q1]);
			spinlock_release(&spinlock);

			break;

		}
		else
		{
			spinlock_release(&spinlock);
			lock_acquire(cv_lock);
			cv_wait(cv, cv_lock);
			lock_release(cv_lock);
		}
		
	}

	lock_acquire(cv_lock);                                                                
	cv_broadcast(cv, cv_lock);                                                            
	lock_release(cv_lock); 
	
	
	//(void)direction;
	//(void)index;
	/*
	 * Implement this function.
	 */
	return;
}
void
turnleft(uint32_t direction, uint32_t index)
{

	/*
	 * The car will enter three quadrants, so these 3 quad_locks must be free
	 * First quadrant is same as the direction. q = direction
	 * Second quadrant is (direction+3)%4. q1 = (direction+3)%4;
	 * Third quadrant will be calculated from the second quadrant. It will be (q1+3)%4. q2 = (q1+3)%4
	 */

	KASSERT(direction <= 3);
	int q = direction;
	int q1 = (q+3)%4;
	int q2 = (q1+3)%4;
	int idx = index;


	while(1)
	{
	spinlock_acquire(&spinlock);
	if(car_in_quad[q] < 0 && car_in_quad[q1] < 0 && car_in_quad[q2] < 0)
	{
		lock_acquire(quad_lock[q]);
		lock_acquire(quad_lock[q1]);
		lock_acquire(quad_lock[q2]);
		KASSERT(lock_do_i_hold(quad_lock[q]));
		KASSERT(lock_do_i_hold(quad_lock[q1]));
		KASSERT(lock_do_i_hold(quad_lock[q2]));
		car_in_quad[q] = idx;
		car_in_quad[q1] = idx;
		car_in_quad[q2] = idx;
		spinlock_release(&spinlock);


		inQuadrant(q,index);
		inQuadrant(q1,index);
		inQuadrant(q2,index);
		leaveIntersection(index);

		KASSERT(car_in_quad[q]==idx && car_in_quad[q1]==idx && car_in_quad[q2]==idx);

		spinlock_acquire(&spinlock);
		car_in_quad[q] = -1;
		car_in_quad[q1] = -1;
		car_in_quad[q2] = -1;
		lock_release(quad_lock[q]);
		lock_release(quad_lock[q1]);
		lock_release(quad_lock[q2]);
		spinlock_release(&spinlock);

		break;

	}
	else
	{
		spinlock_release(&spinlock);
		lock_acquire(cv_lock);
		cv_wait(cv, cv_lock);
		lock_release(cv_lock);
	}
	}

	lock_acquire(cv_lock);
	cv_broadcast(cv, cv_lock);
	lock_release(cv_lock);


//	(void)direction;
//	(void)index;
	/*
	 * Implement this function.
	 */
	return;
}
