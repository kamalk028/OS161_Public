/*
 * All the contents of this file are overwritten during automated
 * testing. Please consider this before changing anything in this file.
 */

#include <types.h>
#include <lib.h>
#include <clock.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <kern/test161.h>
#include <spinlock.h>

/*#define CREATELOOPS		8
#define NSEMLOOPS     63
#define NLOCKLOOPS    120
#define NCVLOOPS      5
#define NTHREADS      32
#define SYNCHTEST_YIELDER_MAX 16

static volatile unsigned long testval1;
static volatile unsigned long testval2;
static volatile unsigned long testval3;
static volatile int32_t testval4;

static struct semaphore *testsem = NULL;
static struct semaphore *testsem2 = NULL;
static struct rwlock *testlock = NULL;
static struct rwlock *testlock2 = NULL;
static struct cv *testcv = NULL;
static struct semaphore *donesem = NULL;

struct spinlock status_lock;
static bool test_status = TEST161_FAIL;

static unsigned long semtest_current;*/

/*
 * Use these stubs to test your reader-writer locks.
 */

/*static
void
locktestthread(void *junk, unsigned long num)
{
	(void)junk;

	int i;

	for (i=0; i<NLOCKLOOPS; i++) {
		kprintf_t(".");
		lock_acquire(testlock);
		random_yielder(4);

		testval1 = num;
		testval2 = num*num;
		testval3 = num%3;

		if (testval2 != testval1*testval1) {
			goto fail;
		}
		random_yielder(4);

		if (testval2%3 != (testval3*testval3)%3) {
			goto fail;
		}
		random_yielder(4);

		if (testval3 != testval1%3) {
			goto fail;
		}
		random_yielder(4);

		if (testval1 != num) {
			goto fail;
		}
		random_yielder(4);

		if (testval2 != num*num) {
			goto fail;
		}
		random_yielder(4);

		if (testval3 != num%3) {
			goto fail;
		}
		random_yielder(4);

		if (!(lock_do_i_hold(testlock))) {
			goto fail;
		}
		random_yielder(4);

		lock_release(testlock);
	}*/

	/* Check for solutions that don't track ownership properly */

	/*for (i=0; i<NLOCKLOOPS; i++) {
		kprintf_t(".");
		if (lock_do_i_hold(testlock)) {
			goto fail2;
		}
	}

	V(donesem);
	return;

fail:
	lock_release(testlock);
fail2:
	failif(true);
	V(donesem);
	return;
}*/

int rwtest(int nargs, char **args) {
	(void)nargs;
	(void)args;

	/*int i, result;

	kprintf_n("Starting lt1...\n");
	for (i=0; i<CREATELOOPS; i++) {
		kprintf_t(".");
		testlock = rwlock_create("testlock");
		if (testlock == NULL) {
			panic("lt1: lock_create failed\n");
		}
		donesem = sem_create("donesem", 0);
		if (donesem == NULL) {
			panic("lt1: sem_create failed\n");
		}
		if (i != CREATELOOPS - 1) {
			rwlock_destroy(testlock);
			sem_destroy(donesem);
		}
	}
	spinlock_init(&status_lock);
	test_status = TEST161_SUCCESS;

	for (i=0; i<NTHREADS; i++) {
		kprintf_t(".");
		result = thread_fork("synchtest", NULL, locktestthread, NULL, i);
		if (result) {
			panic("lt1: thread_fork failed: %s\n", strerror(result));
		}
	}
	for (i=0; i<NTHREADS; i++) {
		kprintf_t(".");
		P(donesem);
	}

	rwlock_destroy(testlock);
	sem_destroy(donesem);
	testlock = NULL;
	donesem = NULL;*/

	kprintf_t("rwt1 unimplemented\n");
	success(TEST161_SUCCESS, SECRET, "rwt1");

	return 0;
}

int rwtest2(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt2 unimplemented\n");
	success(TEST161_SUCCESS, SECRET, "rwt2");

	return 0;
}

int rwtest3(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt3 unimplemented\n");
	success(TEST161_SUCCESS, SECRET, "rwt3");

	return 0;
}

int rwtest4(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt4 unimplemented\n");
	success(TEST161_SUCCESS, SECRET, "rwt4");

	return 0;
}

int rwtest5(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt5 unimplemented\n");
	success(TEST161_SUCCESS, SECRET, "rwt5");

	return 0;
}
