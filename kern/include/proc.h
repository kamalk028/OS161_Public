/*
 * Copyright (c) 2013
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

#ifndef _PROC_H_
#define _PROC_H_

/*
 * Definition of a process.
 *
 * Note: curproc is defined by <current.h>.
 */

#include <spinlock.h>

struct addrspace;
struct thread;
struct vnode;

/*Process Table Definition*/
struct proc_table {
	struct proc *proc;
	//Array should be non-dynamic for memory conservation.
	//pid for each process is same as index in table.
	//Currently, the array is declared in proc.c
};
struct proc *get_proc(int pid);
void pt_remove(int pid);

/*File Table Declaration*/
struct file_table {
	char *proc_name;
	struct array *file_handle_arr;
	struct proc *proc;
};
struct file_table *ft_create(const char *name, struct proc* proc);
void ft_init(struct file_table *ft);
void ft_destroy(struct file_table *ft);
struct file_table *get_curproc_ft(void);

bool is_valid_fd(int fd, struct file_table* ft);
int ft_write(int fd, void *buff, size_t bufflen, struct file_table *ft, int *retval);
int ft_read(int fd, void* buff, size_t bufflen, struct file_table* ft, int* retval);
int ft_open(const char *file, int flags, mode_t mode, struct file_table *ft, int *retval);
int ft_close(int fd, struct file_table *ft, int *retval);
int ft_copy(int oldfd, int newfd, struct file_table *ft, int *retval);
struct file_table* ft_copy_all(struct file_table* src, struct file_table *dest);
int ft_lseek(int fd, off_t offset, int whence, struct file_table* ft, off_t *retval);

/*File Handle Declaration*/
struct file_handle {
	char *file_name;
	struct vnode *vnode;
	off_t offset;
	struct spinlock fh_splk;
	struct lock *fh_lock;
	int ref_count;//Tracks number of fd's which point to this fh.
	int flags;
};

struct file_handle* fh_create(const char *file_name);
void fh_destroy(struct file_handle *fh);
int fh_open(const char *file, int flags, mode_t mode, struct file_handle *fh);
int fh_write(void *buff, size_t bufflen, struct file_handle *fh, int *retval);
int fh_read(void* buff, size_t bufflen, struct file_handle* fh, int* retval);
int fh_lseek(off_t offset, int whence, struct file_handle *fh, off_t *retval);

/*Page Table Declaration*/
struct page_table {
	struct array *pt_array;
	struct lock *paget_lock;//Need a lock for swapping. pt_lock was already used.
};
struct page_table* pt_create(void);

struct page_table_entry {
	uint32_t vpn;//Only need first 20 bits.
	uint32_t ppn;
	uint8_t permission; //Can contain values 0-7.
	bool state;//In memory or on disk? 1 for in memory.
	bool valid;//Has a physical page been allocated yet?
	bool ref;//Used later for swapping algo.
};
struct page_table_entry *pte_create(uint32_t vnp, uint32_t ppn, uint8_t pm, bool state, bool valid, bool ref);
int pt_lookup(struct page_table *pt, uint32_t vpn, uint8_t pm, uint32_t *ppn);//To pull the first three bits of pm only, & it with 00000111.
int pt_lookup1 (struct page_table *pt, uint32_t vpn, uint8_t pm, uint32_t *ppn, unsigned *idx);
int pt_append(struct page_table *pt, struct page_table_entry *pte);


/*
 * Process structure.
 *
 * Note that we only count the number of threads in each process.
 * (And, unless you implement multithreaded user processes, this
 * number will not exceed 1 except in kproc.) If you want to know
 * exactly which threads are in the process, e.g. for debugging, add
 * an array and a sleeplock to protect it. (You can't use a spinlock
 * to protect an array because arrays need to be able to call
 * kmalloc.)
 *
 * You will most likely be adding stuff to this structure, so you may
 * find you need a sleeplock in here for other reasons as well.
 * However, note that p_addrspace must be protected by a spinlock:
 * thread_switch needs to be able to fetch the current address space
 * without sleeping.
 */
struct proc {
	char *p_name;			/* Name of this process */
	struct spinlock p_lock;		/* Lock for this structure */
	unsigned p_numthreads;		/* Number of threads in this process */
	unsigned int pid;
	unsigned int ppid;		/* Parent process ID. Should be NULL for init. */
	int exit_status;	/* Can be filled in by sys_waitpid after exiting. */
	int exit_code;			/* Starts as 0, filled in with user's value before process exits. */

	struct proc *parent_proc; /* A Pointer to the parent process */
	bool has_parent_exited;

	/* VM */
	struct addrspace *p_addrspace;	/* virtual address space */

	/* VFS */
	struct vnode *p_cwd;		/* current working directory */
	struct file_table *ft;		/* Process' own file table */
	struct trapframe *tframe;

		/* add more material here as needed */
	/* synch primitives for handling waitpid and exit */
	//struct spinlock exit_values_splk; //Mostly wont be used //Leaving it as of now //Remove it when you are sure
	struct lock *parent_cvlock;
	struct lock *child_cvlock; 
	struct cv *parent_cv;
	struct cv *child_cv;


	/* Holding on to all the child pids
		Add pids to child_pids whenever fork is called
		remove pid from parents->child_pids when sys_exit is called
	 */
	struct lock *child_pids_lock; //Should acquire lock for adding, removing and reading out of the array.
	struct array *child_pids;
	bool exit_signal;

};

/* This is the process structure for the kernel and for kernel-only threads. */
extern struct proc *kproc;

/* Call once during system startup to allocate data structures. */
void proc_bootstrap(void);

/* Create a fresh process for use by runprogram(). */
struct proc *proc_create_runprogram(const char *name);

/* NEW: Set up a new process for sys_fork(). */
struct proc *proc_fork_runprogram(const char *name, int *err, int *err_code);

/* Destroy a process. */
void proc_destroy(struct proc *proc);

/* Attach a thread to a process. Must not already have a process. */
int proc_addthread(struct proc *proc, struct thread *t);

/* Detach a thread from its process. */
void proc_remthread(struct thread *t);

/* Fetch the address space of the current process. */
struct addrspace *proc_getas(void);

/* Change the address space of the current process, and return the old one. */
struct addrspace *proc_setas(struct addrspace *);

/* Copy trapframe declaration */
void copy_trapframe(struct trapframe *, struct trapframe *);


#endif /* _PROC_H_ */
