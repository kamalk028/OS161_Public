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

/*
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

#define MAX_PROC 1000//Max number of entries in proc table.
#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <uio.h>
#include <fs.h>
#include <kern/errno.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <mips/trapframe.h>
#include <synch.h>


/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

/*
 * User process table. Can only be accessed in proc.c
 */
static struct proc_table pt[MAX_PROC] = { { NULL } };
static unsigned int next_pid = 2;
static struct lock *pt_lock;
//Next_pid should be incremented when new procs are added.
//You can acces element n of the array with pt[n].proc

//Give the process table a pid, and it'll give you the pointer to the proc.
struct proc *get_proc(int pid)
{
	struct proc *p;
	if (pid >= MAX_PROC || pid < 0){
		return NULL;
	}
	lock_acquire(pt_lock);
	p =  pt[pid].proc;
	lock_release(pt_lock);
	return p;
}

void
pt_remove(int pid)
{
	pt[pid].proc = NULL;
	//Code can be added here if we implement recycling pid's.
	return;
}

/*
 * Create a proc structure.
 */
static
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

	proc->ft = ft_create(name, proc);
	if(proc->ft == NULL)
	{
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}
	proc->tframe = kmalloc(sizeof(struct trapframe));
	if(proc->tframe == NULL)
	{
		ft_destroy(proc->ft);
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}

	/*
	* Initialising synch primitives added for waitpid and exit
	*/
	proc->parent_cvlock = lock_create("parent_cvlock");
	proc->child_cvlock = lock_create("child_cvlock");
	proc->parent_cv = cv_create("parent_cv");
	proc->child_cv = cv_create("child_cv");
	if(proc->parent_cvlock == NULL || proc->child_cvlock == NULL || proc->parent_cv == NULL || proc->child_cv == NULL)
	{
		if(proc->parent_cvlock != NULL)
		{
			lock_destroy(proc->parent_cvlock);
		}
		if(proc->child_cvlock != NULL)
		{
			lock_destroy(proc->child_cvlock);
		}
		if(proc->parent_cv != NULL)
		{
			cv_destroy(proc->parent_cv);
		}
		if(proc->child_cv != NULL)
		{
			cv_destroy(proc->child_cv);
		}
		ft_destroy(proc->ft);
		kfree(proc->tframe);
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}

	/* Initialising child_pids: RECAP: It holds pids of children processes*/
	proc->child_pids_lock = lock_create("child_pids_lock");
	if(proc->child_pids_lock == NULL)
	{
		lock_destroy(proc->parent_cvlock);
		lock_destroy(proc->child_cvlock);
		cv_destroy(proc->parent_cv);
		cv_destroy(proc->child_cv);
		ft_destroy(proc->ft);
		kfree(proc->tframe);
		kfree(proc->p_name);
		kfree(proc);
	}
	proc->child_pids = array_create();
	if(proc->child_pids == NULL)
	{
		lock_destroy(proc->child_pids_lock);
		lock_destroy(proc->parent_cvlock);
		lock_destroy(proc->child_cvlock);
		cv_destroy(proc->parent_cv);
		cv_destroy(proc->child_cv);
		ft_destroy(proc->ft);
		kfree(proc->tframe);
		kfree(proc->p_name);
		kfree(proc);
	}

	//spinlock_init(&proc->exit_values_splk);

	proc->p_numthreads = 0;
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

	/* Pointer to parent process */
	proc->parent_proc = NULL;
	proc->has_parent_exited = false;

	/* New stuff for multiplexing. */
	//proc->pid = 2;//PID SHOULD BE ASSIGNED AFTER PLACEMENT ON proc_table.

	//NOTE: Changed exit_status to reflect own process instead of child.
	//  That is because a process can have more than one child!
	proc->exit_status = -1;//Returned by waitpid() after child exits.
	proc->exit_code = 4;//User provides a value here before process exits.

//	proc->ft = ft_create(proc->p_name);

	return proc;
}

/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */
void
proc_destroy(struct proc *proc)
{
	//By the way, WTH HAPPENED TO THE COMMENT HERE?!
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */                                                                     
	//, it's just for reference or debugging anyway.                                     | * Destroy a

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	kfree(proc->tframe);
	ft_destroy(proc->ft);

	//array_cleanup(proc->child_pids);
	//int i=0;
	lock_acquire(proc->child_pids_lock);
	
	while(array_num(proc->child_pids))
	{
		array_remove(proc->child_pids, 0);
	}
	array_destroy(proc->child_pids);
	
	lock_release(proc->child_pids_lock);
	
	lock_destroy(proc->child_pids_lock);

	/*Destory primitives created for waitpid and exit syscalls*/
	lock_destroy(proc->parent_cvlock);
	lock_destroy(proc->child_cvlock);
	cv_destroy(proc->parent_cv);
	cv_destroy(proc->child_cv);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	spinlock_acquire(&curproc->p_lock);
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}
	spinlock_release(&curproc->p_lock);



	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

	//DEBUG ONLY!!
	//kprintf("Number of threads: %d", proc->p_numthreads);
	proc->parent_proc = NULL;
	KASSERT(proc->p_numthreads == 0);
	//spinlock_cleanup(&proc->exit_values_splk);
	spinlock_cleanup(&proc->p_lock);

	//kfree(proc->vn);
	kfree(proc->p_name);
	//kfree(proc->child_exit_status);
	//kfree(proc->exit_code);
	if(proc->ppid == 0)
	{
		lock_destroy(pt_lock);
	}
	kfree(proc);
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	kproc = proc_create("[kernel]");
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *newproc;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	/* Initialise console. */
	ft_init(newproc->ft);

	/* VM fields */

	newproc->p_addrspace = NULL;

	/* VFS fields */

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	//This lock will be used by fork runprogram.
	pt_lock = lock_create("pt_lock");

	/* Update the process table and assign PID. NOTE: Recycling pid's not yet implemented.*/
	lock_acquire(pt_lock);
	pt[next_pid].proc = newproc;//Firat PID is 2. 
	//Other PIDs will depend on next index (held by next_pid).
	newproc->ppid = 0;//ONLY THE FIRST PROCESS SHOULD HAVE 0 FOR THIS! OTHERS GET curproc->pid!!
	newproc->pid = next_pid;
	next_pid++;
	if (next_pid >= MAX_PROC || pt[next_pid].proc != NULL)
	{
		next_pid = 2;
		while(pt[next_pid].proc != NULL)
		{
			next_pid++;
		}
	}
	lock_release(pt_lock);
	return newproc;
}

//This is to be called by sys_fork(). This is similar to proc_create_runprogram, but
//  it does not initialize the console, and it copies the old process' ft. 
struct proc *
proc_fork_runprogram(const char *name, int *err, int *err_code)//fork() currently takes no name arg.
{
	//Note that, at this point in the code, there is still only one process
	//  in control. thread_fork() will be called inside sys_fork().
	struct proc *newproc;

	//MAY NEED TO COPY TRAPFRAME!!

	newproc = proc_create(name);
	if (newproc == NULL) {
		*err = -1;
		*err_code = ENOMEM;
		return NULL;
	}

	/* VM fields */
	newproc->p_addrspace = NULL;

	//spinlock_acquire(&curproc->p_lock);
	as_copy(curproc->p_addrspace, &newproc->p_addrspace);
	//spinlock_release(&curproc->p_lock);

	if(newproc->p_addrspace == NULL)
	{
		*err_code = ENOMEM;
		*err = -1;
		return NULL;
	}

	//spinlock_acquire(&curproc->p_lock);
//	copy_trapframe(curproc->tframe, newproc->tframe);
	memcpy(newproc->tframe, curproc->tframe, sizeof(struct trapframe));
	//spinlock_release(&curproc->p_lock);
	/*VFS Feilds*/
	/*
	 * Lock the current process to copy its current directory.
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	/*Copy the parent's file table.*/
	newproc->ft = ft_copy_all(curproc->ft, newproc->ft);
	newproc->ft->proc = newproc;

	/* Update the process table and assign PID. */
	lock_acquire(pt_lock);
	pt[next_pid].proc = newproc;//Firat PID for this function should be 3. 
	//Other PIDs will depend on next index (held by next_pid).
	newproc->ppid = curproc->pid;//curproc is the parent proc.
	newproc->pid = next_pid;
	newproc->parent_proc = curproc; //Setting reference to parent proc.
	newproc->has_parent_exited = false;
	next_pid++;
	if (next_pid >= MAX_PROC || pt[next_pid].proc != NULL)
	{
		next_pid = 2;
		while(pt[next_pid].proc != NULL)
		{
			next_pid++;
		}
	}
	lock_release(pt_lock);

	/*Adding the pid to child_pids array of the parent (curproc)*/
	unsigned idx = 0;
	//Acquire lock and proceed
	lock_acquire(curproc->child_pids_lock);
	array_add(curproc->child_pids, (void *)newproc->pid, &idx);
	lock_release(curproc->child_pids_lock);

	return newproc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	proc->p_numthreads++;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = proc;
	splx(spl);

	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	KASSERT(proc->p_numthreads > 0);
	proc->p_numthreads--;
	if(proc->p_numthreads == 0)
	{
		spinlock_release(&proc->p_lock);
		proc_destroy(proc);
	}
	else
	{
		spinlock_release(&proc->p_lock);
	}

	spl = splhigh();
	t->t_proc = NULL;
	splx(spl);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}

/* File Table function definitions */

struct file_table* ft_create (const char *name, struct proc *proc)
{
	struct file_table* ft;
	ft = kmalloc(sizeof(*ft));
	if(ft == NULL)
	{
		return NULL;
	}
	ft->proc_name = kstrdup(name);
	if(ft->proc_name == NULL)
	{
		kfree(ft);
		return NULL;
	}
	ft->file_handle_arr = array_create();
	if(ft->file_handle_arr == NULL)
	{
		kfree(ft->proc_name);
		kfree(ft);
		return NULL;
	}
	array_init(ft->file_handle_arr);
	ft->proc = proc;//This should probably hold curproc's PID!!
	return ft;
}

void ft_init(struct file_table* ft)
{
	KASSERT(ft != NULL);
	struct file_handle* fh_read;
	struct file_handle* fh_write;
	struct file_handle* fh_write2;
	char *f_name = kstrdup("con:");
	fh_read = fh_create(f_name);
	fh_write = fh_create(f_name);
	fh_write2 = fh_create(f_name);
	fh_open(f_name, O_RDONLY, 0664, fh_read);
	fh_open(f_name, O_WRONLY, 0664, fh_write);
	fh_open(f_name, O_WRONLY, 0664, fh_write2);
	unsigned idx;
	array_add(ft->file_handle_arr, fh_read, &idx);
	array_add(ft->file_handle_arr, fh_write, &idx);
	array_add(ft->file_handle_arr, fh_write2, &idx);
	kfree(f_name);
	return;
}

void ft_destroy(struct file_table *ft)
{
	KASSERT(ft != NULL);
//	struct proc *proc = ft->proc;
//	lock_acquire(proc->child_pids_lock);
//	bool has_child = array_num(proc->child_pids) > 0 ? true : false;
//	lock_release(proc->child_pids_lock);
	ft->proc = NULL;
	kfree(ft->proc_name);
	//Update ref_count of any fh's still left in the array.
	//  Destroy them if they are not needed.
	unsigned int i = 0;
	bool no_refs = false;
	struct file_handle* fh;
	for (i = 0; i < array_num(ft->file_handle_arr); i++)
	{
		if(array_get(ft->file_handle_arr, i) != NULL){
			fh = (struct file_handle*) array_get(ft->file_handle_arr, i);
			spinlock_acquire(&fh->fh_splk);
			fh->ref_count--;
			if(fh->ref_count == 0){
				no_refs = true;
			}
			spinlock_release(&fh->fh_splk);
			if(no_refs){ //|| !has_child) {// && i != 2){
				vfs_close(fh->vnode);
				fh_destroy(fh);
			}
		}
	}//Apologies for the sloppiness.

	//Now, all elements of the array must be emptied so that it can be destroyed.
	while(array_num(ft->file_handle_arr) != 0)
	{
		array_remove(ft->file_handle_arr, 0);
	}
	array_destroy(ft->file_handle_arr);//This requires an array to be empty.
	//kfree(ft->file_handle_arr);
	kfree(ft);
	return;
}

struct file_table *get_curproc_ft()
{
	return curthread->t_proc->ft;
}

bool is_valid_fd(int fd, struct file_table *ft)
{
	bool result = true;
	int num = array_num(ft->file_handle_arr);                                  
	if(fd < 0 || fd >= num)
	{
		result = false;
	}
	else
	{
		struct file_handle* fh;
		fh = (struct file_handle*) array_get(ft->file_handle_arr, fd);
		if(fh == NULL)
		{
			result = false;
		}
	}
	return result;
}

int ft_write(int fd, void *buff, size_t bufflen, struct file_table *ft, int *retval)
{
	KASSERT(buff != NULL);//This assertion actually causes a badcall test to fail!
	KASSERT(ft != NULL);
	KASSERT(retval != NULL);
	int err;
	err = 0;

	//kprintf("Inside ft_write: FD value is: %d\n",fd);
	//Checks for valid fd.
	int num = array_num(ft->file_handle_arr);//ASSUMPTION: array_num still counts if an element == NULL.
	if(fd < 0 || fd >= num)
	{
		err = EBADF;
		*retval = EBADF;
		return err;	
	}

	struct file_handle *fh;
	fh = (struct file_handle*) array_get(ft->file_handle_arr, fd);
	if(fh == NULL)
	{
		kprintf("fhandle is null for the fd .... \n");
		err = EBADF;
		*retval = EBADF;
		return err;
	}

	err=fh_write(buff, bufflen, fh, retval);
	return err;
}

int ft_read(int fd, void* buff, size_t bufflen, struct file_table* ft, int* retval)
{
	KASSERT(buff != NULL);
	KASSERT(ft != NULL);
	KASSERT(retval != NULL);
	int err;
	err = 0;
	if(!is_valid_fd(fd, ft))
	{
		err = EBADF;
		*retval = EBADF;
		return err;
	}
	struct file_handle *fh;
	fh = (struct file_handle*) array_get(ft->file_handle_arr, fd);
	err = fh_read(buff, bufflen, fh, retval);
	return err;
}


int ft_open(const char *file, int flags, mode_t mode, struct file_table *ft, int *retval)
{
	unsigned idx;
	idx = 0; //Initialising because of a compile time error
	struct file_handle* fh = NULL;

	fh = fh_create(file);//ft_close will eventually free the memory for this.

	int err = 0;//By default, assume no errors while err==0.
	err = fh_open(file,flags,mode,fh);//If fh_open fails, then try passing fh by reference.
	//err will now hold 0 unless fh_open failed.
	if(err)
	{
		fh_destroy(fh);
		*retval = -1; //This is supposedly not correct, but badcall-open is passing anyway.
		return err;
	}
	
	//New file handle is added to first element of file_handle_arr which contains NULL.
	//  If none are found, then the array is expanded.
	unsigned int i;
	bool null_found = false;
	for(i = 0; i < array_num(ft->file_handle_arr); i++)
	{
		if(array_get(ft->file_handle_arr, i) == NULL){
			idx = i;
			null_found = true;
		}
	}
	if(null_found)
	{
		array_set(ft->file_handle_arr, idx, fh);
	}
	else
	{
		array_add(ft->file_handle_arr, fh, &idx);
	}
	*retval = idx;
	return err;
}

int ft_lseek(int fd, off_t offset, int whence, struct file_table* ft, off_t* retval)
{
	//Need to use vop_isseekable on the file handle.
	KASSERT(ft != NULL);
	KASSERT(retval != NULL);
	int err;
	err = 0;
	if(!is_valid_fd(fd, ft))
	{
		err = EBADF;
		*retval = EBADF;
		return err;
	}
	struct file_handle *fh;
	fh = (struct file_handle*) array_get(ft->file_handle_arr, fd);
	err = fh_lseek(offset, whence, fh, retval);
	return err;
}

int ft_close(int fd, struct file_table *ft, int *retval)
{
	KASSERT(retval != NULL);
	KASSERT(ft != NULL);
	int err = 0;
	struct file_handle *fh;

	//Checks for valid fd. Note that fd's 0, 1, and 2 ARE ALLOWED.
	int num = array_num(ft->file_handle_arr);
	if(fd < 0 || fd >= num || array_get(ft->file_handle_arr, fd) == NULL)
	{
		err = EBADF;
		*retval = -1;
		return err;	
	}

	//Why is this type cast here?
	fh = (struct file_handle*) array_get(ft->file_handle_arr, fd);

	//Remove the handle from the file table.
	//NOTE: array_remove is not used because it can automatically shift items downward.
	//  We do not want open fh's having their fd's inadvertently modified.
	//  Do not destroy the file handle yet; other proc's might be using it.
	array_set(ft->file_handle_arr, fd, NULL);

	//If the file handle is NOT being used by any other processes, then destroy it.
	//IF THIS ref_count IS TOO HIGH, THEN MEMLEAKS WILL OCCUR!
	spinlock_acquire(&fh->fh_splk);
	fh->ref_count--;
	bool no_refs = false;
	if(fh->ref_count == 0)
	{
		no_refs = true;
	}
	spinlock_release(&fh->fh_splk);
	if(no_refs)
	{
		vfs_close(fh->vnode);
		fh_destroy(fh);
	}

	return err;
}

int ft_copy(int oldfd, int newfd, struct file_table *ft, int *retval)
{
	KASSERT(retval != NULL);
	KASSERT(ft != NULL);
	int err = 0;
	struct file_handle *fh;
	unsigned int idx = 0;

	if (newfd == oldfd){
		return err;//Do nothing in this case.
	}

	//Do the usual check for valid fd on oldfd. //NOTE: 0-2 ARE NOT ALLOWED! They probably should be, though. Even though that's crazy.
	int num = array_num(ft->file_handle_arr);
	if(oldfd < 0 || oldfd >= num || array_get(ft->file_handle_arr, oldfd) == NULL)
	{
		err = EBADF;
		*retval = -1;
		return err;
	}
	fh = (struct file_handle*) array_get(ft->file_handle_arr, oldfd);

	//Also need to check the validity of newfd. Cannot be negative,
	//  and if it is larger than array_num, then the array must be expanded.
	//  I will impose a hard limit on newfd being less than 128.
	if (newfd < 0 || newfd > 127)
	{
		err = EBADF;
		*retval = -1;
		return err;
	}
	while (newfd >= num)
	{
		//array_set will return an error if we do not manually increase size.
		array_add(ft->file_handle_arr, NULL, &idx);
		num++;
	}

	//CHECK FOR A fh IN newfd BEFOREHAND! (Only neccesary b/c fh may need to be destroyed.)
	if(((unsigned int)newfd < array_num(ft->file_handle_arr)) && (array_get(ft->file_handle_arr, newfd) != NULL))
	{
		err = ft_close(newfd, ft, retval);
		if (err) {
			*retval = -1;
			return err;
		}
	}

	//Add the file handle to the ft. It is intentionally the exact same file handle.
	array_set(ft->file_handle_arr, newfd, fh);
	spinlock_acquire(&fh->fh_splk);
	fh->ref_count++;
	spinlock_release(&fh->fh_splk);
	*retval = newfd; //Man pages say to do this if no errors occurred.
	if(err){
		*retval = -1;
	}
	return err;
}

//Copy all elements of a file table from src to dest. For use in sys_fork().
struct file_table* ft_copy_all(struct file_table *src, struct file_table* dest)
{
	//Actually begin copying.
	unsigned int i = 0;
	unsigned int filler = 0;//To avoid passing i as reference and non-reference (shouldn't matter).
	struct file_handle *fh;
	for (i = 0; i < array_num(src->file_handle_arr); i++)
	{
		array_add(dest->file_handle_arr, array_get(src->file_handle_arr, i), &filler);
		//Update each fh's ref_count as you go.
		fh = array_get(dest->file_handle_arr, i);
		if (fh != NULL){
			spinlock_acquire(&fh->fh_splk);
			fh->ref_count++;
			spinlock_release(&fh->fh_splk);
		}
	}

	return dest;
}

/* File Handle function definitions */

struct file_handle* fh_create(const char* file_name)
{
	KASSERT(file_name != NULL);
	struct file_handle* fh;
	fh = kmalloc(sizeof(*fh));
	if(fh == NULL)
	{
		return NULL;
	}

	fh->file_name = kstrdup(file_name);
	if(fh->file_name == NULL)
	{
		kfree(fh);
		return NULL;
	}

	fh->fh_lock = lock_create("fh_lock");
	if(fh->fh_lock == NULL)
	{
		kfree(fh->file_name);
		kfree(fh);
		return NULL;
	}

	spinlock_init(&fh->fh_splk);
	fh->vnode = NULL;
	fh->offset = 0;
	fh->flags = 0;
	fh->ref_count = 0;//Number of processes using this file handle.
	
	return fh;
}

void fh_destroy(struct file_handle *fh)
{
	KASSERT(fh != NULL);
	lock_destroy(fh->fh_lock);
	spinlock_cleanup(&fh->fh_splk);
	fh->vnode = NULL;
	fh->flags = 0;
	fh->offset = 0;
	KASSERT(fh->ref_count == 0);
	kfree(fh->file_name);
	kfree(fh);
	return;
}

int fh_open(const char *file, int flags, mode_t mode, struct file_handle *fh)
{
	char *dup_fname = kstrdup(file);
	//strcpy(dup_fname, file);
	int err = 0;
	//0664 implies write permission, but that probably shouldn't ALWAYS go there...
	//Instead, perhaps mode_t should be passed in, since sys_open takes that anyway.
	err = vfs_open(dup_fname, flags, mode, &fh->vnode);
	if(err)//vfs_open should return 0 unless there was an error.
	{
		kfree(dup_fname);
		kprintf("Inside fh_open: Error while opening file: %s with flag %d\n", file, flags);
		return err;
	}
	fh->flags = flags;
	spinlock_acquire(&fh->fh_splk);
	fh->ref_count++;//This should also be incremented with sys_fork.
	spinlock_release(&fh->fh_splk);
	kfree(dup_fname);
	return 0;

}


int fh_write(void* buff, size_t bufflen, struct file_handle* fh, int* retval)
{
	KASSERT(fh != NULL);
	int err;
	err = 0;
	struct iovec iov;
	struct uio uio;
	if(!(fh->flags & O_WRONLY || fh->flags & O_RDWR))
	{
		kprintf("The file %s is not open for write purpose: \n", fh->file_name);
		kprintf("It is open in flag: %d\n",fh->flags);
		err = EBADF;
		*retval = EBADF;
		return err;
	}
	lock_acquire(fh->fh_lock);//spinlock_acquire(&fh->fh_splk);
	uio_uinit(&iov, &uio, buff, bufflen, fh->offset, UIO_WRITE);
	err = VOP_WRITE(fh->vnode, &uio);
	if(err)//VOP_WRITE always returns zero unless there was an error.
	{
		lock_release(fh->fh_lock);
		*retval = err;
		return err;
	}
	/*
	 *  Getting offset and bytes written from uio
	 *  Please make sure if getting offset and res_id like this is okay 
	 */
	fh->offset = uio.uio_offset;
	*retval = bufflen - uio.uio_resid;
	lock_release(fh->fh_lock);
	//spinlock_release(&fh->fh_splk);
	return err;
}

int fh_read(void* buff, size_t bufflen, struct file_handle* fh, int* retval)
{
	KASSERT(fh != NULL);
	int err;
	err = 0;
	struct iovec iov;//DO THESE NEED TO BE FREED?
	struct uio uio;
	if(!(fh->flags == O_RDONLY || fh->flags & O_RDWR))
	{
		kprintf("The file %s is not open for read purpose: \n",fh->file_name);
		kprintf("It is open in flag: %d\n", fh->flags);
		err = EBADF;
		*retval = EBADF;
		return err;
	}
	lock_acquire(fh->fh_lock);//spinlock_acquire(&fh->fh_splk);//spinlock_acquire(&fh->fh_splk);
	uio_uinit(&iov, &uio, buff, bufflen, fh->offset, UIO_READ);//DOES THIS CALL KMALLOC?
	err = VOP_READ(fh->vnode, &uio);
	if(err)
	{
		lock_release(fh->fh_lock);
		*retval = err;
		return err;
	}
	fh->offset = uio.uio_offset;
	*retval = bufflen - uio.uio_resid;
	lock_release(fh->fh_lock);//spinlock_acquire(&fh->fh_splk);//spinlock_release(&fh->fh_splk);
	return err;
}

int fh_lseek(off_t offset, int whence, struct file_handle *fh, off_t *retval)
{
	int err = 0;
	if(!(strcmp(fh->file_name, "con:")))
	{
		err = ESPIPE;
		*retval = ESPIPE;
		return err;
	}
	if(!(VOP_ISSEEKABLE(fh->vnode))){
		err = ESPIPE;
		*retval = ESPIPE;
		return err;
	}
	off_t new_offset;
	switch(whence)
	{
		case SEEK_SET:
			new_offset = offset;
			break;
		case SEEK_CUR:
			lock_acquire(fh->fh_lock);//spinlock_acquire(&fh->fh_splk);
			new_offset = fh->offset + offset;
			break;
		case SEEK_END: ;
			struct stat st;
			VOP_STAT(fh->vnode, &st);
			new_offset = st.st_size + offset;
			break;
		default:
			err = EINVAL;
			*retval = EINVAL;
			return err;
	}
	if(new_offset < 0)
	{
		if(lock_do_i_hold(fh->fh_lock))
		{
			lock_release(fh->fh_lock);//spinlock_release(&fh->fh_splk);
		}
		err = EINVAL;
		*retval = EINVAL;
		return err;
	}
	if(!lock_do_i_hold(fh->fh_lock))
	{
		lock_acquire(fh->fh_lock);//spinlock_acquire(&fh->fh_splk);
	}
	fh->offset = new_offset;
	*retval = fh->offset;
	lock_release(fh->fh_lock);//spinlock_release(&fh->fh_splk);
	return err;
}

/*
    Create a page table for a process.
*/
struct page_table *pt_create()
{
	struct page_table *pt;
	pt = kmalloc(sizeof(*pt));
	pt->pt_array = array_create();
	array_init(pt->pt_array);
	return pt;
}

int pt_append(struct page_table *pt, struct page_table_entry *pte)
{
	KASSERT(pt != NULL);
	KASSERT(pte != NULL);
	unsigned int idx = 0;
	return array_add(pt->pt_array, pte, &idx);
}

/*
	Create an entry in a page table.
	For now, we assume that whatever calls this function will know the values of all parameters.
	Eventually, we may want to change this function so that it does the work.
*/
struct page_table_entry *pte_create(uint32_t vpn, uint32_t ppn, uint8_t pm, bool state, bool valid, bool ref)
{
	struct page_table_entry *pte;
	pte = kmalloc(sizeof(*pte));
	if(pte == NULL)//KAMAL_CHECK: I have added this if loop, handle NULL case wherever the function is called.
	{
		return NULL;
	}

	pte->vpn = vpn;
	pte->ppn = ppn;
	pte->permission = pm;
	pte->state = state;
	pte->valid = valid;
	pte->ref = ref;
	return pte;
}

int pt_lookup (struct page_table *pt, uint32_t vpn, uint8_t pm, uint32_t *ppn)
{
	(void)pm;
	int num = array_num(pt->pt_array);
	//bool has_entry = 0;
	for (int i = 0; i<num; i++)
	{
		struct page_table_entry *pte = array_get(pt->pt_array, i);
		//KAMAL_CHECK: what if the pte is NULL? 
		if(vpn == pte->vpn)
		{
			if(pte->valid)
			{
				*ppn = pte->ppn;
				pte->ref = 1;
				return 0;
			} 
		}
		else
		{
			pte->ref = 0;//While implementing swapping, CHANGE THIS FUNCTION so that it starts the lookup where the last one left off.
		}
	}
	return -1; //This means no page table entry for given vpn.
}

/*paddr_t pt_lookup(uint32_t va, uint8_t pm)
{
	//Write this!
}*/

/*
Function copies all the members of the src_tf to dest_tf:
*/
void copy_trapframe(struct trapframe *src_tf, struct trapframe *dest_tf)
{
	KASSERT(src_tf != NULL);
	KASSERT(dest_tf != NULL);
	dest_tf->tf_vaddr = src_tf->tf_vaddr;
	dest_tf->tf_status = src_tf->tf_status;
	dest_tf->tf_cause = src_tf->tf_cause;
	dest_tf->tf_lo = src_tf->tf_lo;
	dest_tf->tf_hi = src_tf->tf_hi;
	dest_tf->tf_ra = src_tf->tf_ra;
	dest_tf->tf_at = src_tf->tf_at;
	dest_tf->tf_v0 = src_tf->tf_v0;
	dest_tf->tf_v1 = src_tf->tf_v1;
	dest_tf->tf_a0 = src_tf->tf_a0;
	dest_tf->tf_a1 = src_tf->tf_a1;
	dest_tf->tf_a2 = src_tf->tf_a2;
	dest_tf->tf_a3 = src_tf->tf_a3;
	dest_tf->tf_t0 = src_tf->tf_t0;
	dest_tf->tf_t1 = src_tf->tf_t1;
	dest_tf->tf_t2 = src_tf->tf_t2;
	dest_tf->tf_t3 = src_tf->tf_t3;
	dest_tf->tf_t4 = src_tf->tf_t4;
	dest_tf->tf_t5 = src_tf->tf_t5;
	dest_tf->tf_t6 = src_tf->tf_t6;
	dest_tf->tf_t7 = src_tf->tf_t7;
	dest_tf->tf_s0 = src_tf->tf_s0;
	dest_tf->tf_s1 = src_tf->tf_s1;
	dest_tf->tf_s2 = src_tf->tf_s2;
	dest_tf->tf_s3 = src_tf->tf_s3;
	dest_tf->tf_s4 = src_tf->tf_s4;
	dest_tf->tf_s5 = src_tf->tf_s5;
	dest_tf->tf_s6 = src_tf->tf_s6;
	dest_tf->tf_s7 = src_tf->tf_s7;
	dest_tf->tf_t8 = src_tf->tf_t8;
	dest_tf->tf_t9 = src_tf->tf_t9;
	dest_tf->tf_gp = src_tf->tf_gp;
	//dest_tf->tf_sp = src_tf->tf_sp;
	dest_tf->tf_s8 = src_tf->tf_s8;
	dest_tf->tf_epc = src_tf->tf_epc;
}
