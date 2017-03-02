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


/*
 *Here are the remnants of my unfinished attempt at initializing file table.
 */
/*struct file_table *
ft_create(const char *name)
{
	struct file_table *file_table;
	int i = 0;

	file_table = kmalloc(sizeof(*file_table));
	if (file_table == NULL) {
		return NULL;

	//We are giving the ft the process' name.
	file_table->proc_name = kstrdup(name);
	if (file_table->proc_name == NULL) {
		kfree(file_table);
		return NULL;

	//Array holding pointers to file handles.
	//Exactly 64 elements at the recommendation of Geoffery.
	for(i=0; i<63; i++)
	{
		file_table->file_handle_arr[i] = kmalloc(sizeof(struct file_handle));
	}

	//Track the process which created this file table. (WILL NEED TO BE CHANGED WHEN FORK IS IMPLEMENTED!)
	file_table->proc = curthread->t_proc;

	//Next step is to fill the first three elements of file_handle_arr.
	file_table->file_handle_arr[0] =
}*/


/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

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

	proc->ft = ft_create(name);
	if(proc->ft == NULL)
	{
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}

	proc->p_numthreads = 0;
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

	
	proc->ft = ft_create(proc->p_name);

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

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	ft_destroy(proc->ft);


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

	KASSERT(proc->p_numthreads == 0);
	spinlock_cleanup(&proc->p_lock);

	//kfree(proc->vn);
	kfree(proc->p_name);
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

	/**
	  Initialise console in write mode
	  Just to pass 2.1
	  Should change after submission
	 */
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
	spinlock_release(&proc->p_lock);

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

struct file_table* ft_create (const char *name)
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
	ft->proc = NULL;
	return ft;
}

void ft_init(struct file_table* ft)
{
	KASSERT(ft != NULL);
	struct file_handle* fh_read;
	struct file_handle* fh_write;
	fh_read = fh_create("con:");
	fh_write = fh_create("con:");
	fh_open("con:", O_RDONLY, fh_read);
	fh_open("con:", O_WRONLY, fh_write);
	unsigned idx;
	array_add(ft->file_handle_arr, fh_read, &idx);
	array_add(ft->file_handle_arr, fh_write, &idx);
	array_add(ft->file_handle_arr, fh_write, &idx);
	return;
}

void ft_destroy(struct file_table *ft)
{
	KASSERT(ft != NULL);
	ft->proc = NULL;
	kfree(ft->proc_name);
	/* Should we call fh_destroy, if there are any file handles in the array */
	array_cleanup(ft->file_handle_arr);
	kfree(ft->file_handle_arr);
	kfree(ft);
	return;
}

struct file_table* get_curproc_ft()
{
	return curthread->t_proc->ft;
}

int ft_write(int fd, void* buff, size_t bufflen, struct file_table *ft, int* retval)
{
	KASSERT(buff != NULL);
	KASSERT(ft != NULL);
	KASSERT(retval != NULL);
	int err;
	err = 0;
	int num = array_num(ft->file_handle_arr);
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

int ft_open(const char *file, int flags, struct file_table *ft)
{
	unsigned idx;
	idx = 0; //Initialising because of a compile time error
	struct file_handle* fh;
	fh = fh_create(file);
	int err;
	err = fh_open(file,flags,fh);
	if(err)
	{
		return err;
	}
	array_add(ft->file_handle_arr, fh, &idx);
	return idx;
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

	spinlock_init(&fh->fh_splk);
	fh->vnode = NULL;
	fh->offset = 0;
	fh->flags = 0;
	fh->ref_count = 1;
	
	return fh;
}

void fh_destroy(struct file_handle *fh)
{
	KASSERT(fh != NULL);
	spinlock_cleanup(&fh->fh_splk);
	fh->vnode = NULL;
	fh->flags = 0;
	fh->offset = 0;
	fh->ref_count = 0;
	kfree(fh->file_name);
	kfree(fh);
	return;
}

int fh_open(const char* file, int flags, struct file_handle* fh)
{
	char *dup_fname = kstrdup(file);
	//strcpy(dup_fname, file);
	int err;
	err = vfs_open(dup_fname, flags, 0664, &fh->vnode);
	if(err)
	{
		kprintf("Inside fh_open: Error while opening file: %s with flag %d\n", file, flags);
		return err;
	}
	fh->flags = flags;
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
		err = -1;
		*retval = EBADF;
		return err;
	}
	uio_uinit(&iov, &uio, buff, bufflen, fh->offset, UIO_WRITE);
	err = VOP_WRITE(fh->vnode, &uio);
	if(err)
	{
		*retval = err;
		return err;
	}
	/*
	 *  Getting offset and bytes written from uio
	 *  Please make sure if getting offset and res_id like this is okay 
	 */
	spinlock_acquire(&fh->fh_splk);
	fh->offset = uio.uio_offset;
	*retval = uio.uio_resid;
	spinlock_release(&fh->fh_splk);
	return err;
}
