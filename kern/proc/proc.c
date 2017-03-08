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

#define MAX_PROC 4000//Max number of entries in proc table.
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


/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

/* User process table. Can only be accessed in proc.c */
/*static struct proc_table *pt_create()
{
	static struct proc_table *pt;
	pt = kmalloc(sizeof(*pt));
	if (pt == NULL){
		kprintf("Not enough memory for user proc table!!");
		return NULL;
	pt->proc_arr = array_create();
	array_set(pt->proc_arr, 0, NULL);
	array_set(pt->proc_arr, 1, NULL);//Minimum PID is 2.

	return pt;
}*/

/* User process table. Can only be accessed in proc.c */
struct proc_table pt[MAX_PROC] = { { NULL } };
static int next_pid = 2;
//Next_pid should be incremented when new procs are added.
//You can acces element n of the array with pt[n].proc

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

	/* New stuff for multiplexing. */
	//proc->pid = 2;//PID SHOULD BE ASSIGNED AFTER PLACEMENT ON proc_table.
	proc->ppid = 0;//ONLY THE FIRST PROCESS SHOULD HAVE 0 FOR THIS! OTHERS GET curproc->pid!!
	proc->exit_status = 0;//We'll say 0 for not exited, 1 for exited.
	proc->exit_code = 0;//Filled in with random 32-bit integer when process exits.

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
proc_create_runprogram(const char *name)//fork() currently takes no name arg.
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

	/* Update the process table and assign PID. NOTE: Recycling pid's not yet implemented.*/
	pt[next_pid].proc = newproc;//Firat PID is 2. Other PIDs will depend on first available index.
	newproc->pid = next_pid;
	next_pid++;

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
	ft->proc = NULL;//This should probably hold curproc's PID!!
	return ft;
}

void ft_init(struct file_table* ft)
{
	KASSERT(ft != NULL);
	struct file_handle* fh_read;
	struct file_handle* fh_write;
	fh_read = fh_create("con:");
	fh_write = fh_create("con:");
	fh_open("con:", O_RDONLY, 0664, fh_read);
	fh_open("con:", O_WRONLY, 0664, fh_write);
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
	//Update ref_count of any fh's still left in the array.
	//  Destroy them if they are not needed.
	unsigned int i = 0;
	struct file_handle* fh;
	for (i = 0; i < array_num(ft->file_handle_arr); i++)
	{
		if(array_get(ft->file_handle_arr, i) != NULL){
			fh = (struct file_handle*) array_get(ft->file_handle_arr, i);
			fh->ref_count--;
			if(fh->ref_count == 0){
				fh_destroy(fh);
			}
		}
	}//Apologies for the sloppiness.
	array_cleanup(ft->file_handle_arr);
	kfree(ft->file_handle_arr);
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
	KASSERT(buff != NULL);
	KASSERT(ft != NULL);
	KASSERT(retval != NULL);
	int err;
	err = 0;

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
	struct file_handle* fh;
	fh = fh_create(file);
	int err = 0;//By default, assume no errors while err==0.
	err = fh_open(file,flags,mode,fh);//If fh_open fails, then try passing fh by reference.
	//err will now hold 0 unless fh_open failed.
	if(err)
	{
		*retval = -1; //This is not correct
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
	fh = (struct file_handle*) array_get(ft->file_handle_arr, fd);

	//Checks for valid fd. Note that fd's 0, 1, and 2 ARE ALLOWED.
	int num = array_num(ft->file_handle_arr);
	if(fd < 0 || fd >= num)
	{
		err = EBADF;
		*retval = -1;
		return err;	
	}

	//Remove the handle from the file table.
	//NOTE: array_remove is not used because it can automatically shift items downward.
	//  We do not want open fh's having their fd's inadvertently modified.
	array_set(ft->file_handle_arr, fd, NULL);

	//If the file handle is NOT being used by any other processes, then destroy it.
	fh->ref_count--;
	if(fh->ref_count == 0)
	{
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
	fh = (struct file_handle*) array_get(ft->file_handle_arr, oldfd);

	//Do the usual checl for valid fd.
	int num = array_num(ft->file_handle_arr);
	if(oldfd < 0 || oldfd >= num)
	{
		err = EBADF;
		*retval = -1;
		return err;	
	}
	//CHECK FOR A fh IN newfd BEFOREHAND! (Only neccesary b/c fh may need to be destroyed.)
	if(((unsigned int)newfd < array_num(ft->file_handle_arr)) && (array_get(ft->file_handle_arr, newfd) != NULL))
	{
		err = ft_close(newfd, ft, retval);
	}

	//Add the file handle to the ft. It is intentionally the exact same file handle.
	array_set(ft->file_handle_arr, newfd, fh);
	fh->ref_count++;
	*retval = newfd; //Man pages say to do this if no errors occurred.
	if(err){
		*retval = -1;
	}
	return err;
}

//Copy all elements of a file table from src to dest. For use in sys_fork().
struct file_table* ft_copy_all(struct file_table *src, const char *child_name)
{
	struct file_table* dest;
	dest = kmalloc(sizeof(*dest));
	if(dest == NULL)
	{
		return NULL;
	}
	dest->proc_name = kstrdup(child_name);
	if(dest->proc_name == NULL)
	{
		kfree(dest);
		return NULL;
	}
	dest->file_handle_arr = array_create();
	if(dest->file_handle_arr == NULL)
	{
		kfree(dest->proc_name);
		kfree(dest);
		return NULL;
	}
	array_init(dest->file_handle_arr);
	dest->proc = NULL;//REMEMBER TO CHANGE THIS TO CHLID PID!!

	//Actually begin copying.
	unsigned int i = 0;
	unsigned int filler = 0;//To avoid passing i as reference and non-reference (shouldn't matter).
	struct file_handle *fh;
	for (i = 0; i < array_num(src->file_handle_arr); i++)
	{
		array_add(dest->file_handle_arr, array_get(src->file_handle_arr, i), &filler);
		//Update each fh's ref_count as you go.
		fh = array_get(dest->file_handle_arr, i);
		fh->ref_count++;
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
	spinlock_cleanup(&fh->fh_splk);
	fh->vnode = NULL;
	fh->flags = 0;
	fh->offset = 0;
	fh->ref_count = 0;
	kfree(fh->file_name);
	kfree(fh);
	return;
}

int fh_open(const char *file, int flags, mode_t mode, struct file_handle *fh)
{
	char *dup_fname = kstrdup(file);
	//strcpy(dup_fname, file);
	int err;
	//0664 implies write permission, but that probably shouldn't ALWAYS go there...
	//Instead, perhaps mode_t should be passed in, since sys_open takes that anyway.
	err = vfs_open(dup_fname, flags, mode, &fh->vnode);
	if(err)//vfs_open should return 0 unless there was an error.
	{
		kprintf("Inside fh_open: Error while opening file: %s with flag %d\n", file, flags);
		return err;
	}
	fh->flags = flags;
	fh->ref_count++;//This should also be incremented with sys_fork.
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
	uio_uinit(&iov, &uio, buff, bufflen, fh->offset, UIO_WRITE);
	err = VOP_WRITE(fh->vnode, &uio);
	if(err)//VOP_WRITE always returns zero unless there was an error.
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
	*retval = bufflen - uio.uio_resid;
	spinlock_release(&fh->fh_splk);
	return err;
}

int fh_read(void* buff, size_t bufflen, struct file_handle* fh, int* retval)
{
	KASSERT(fh != NULL);
	int err;
	err = 0;
	struct iovec iov;
	struct uio uio;
	if(!(fh->flags == O_RDONLY || fh->flags & O_RDWR))
	{
		kprintf("The file %s is not open for read purpose: \n",fh->file_name);
		kprintf("It is open in flag: %d\n", fh->flags);
		err = EBADF;
		*retval = EBADF;
		return err;
	}
	uio_uinit(&iov, &uio, buff, bufflen, fh->offset, UIO_READ);
	err = VOP_READ(fh->vnode, &uio);
	if(err)
	{
		*retval = err;
		return err;
	}
	spinlock_acquire(&fh->fh_splk);
	fh->offset = uio.uio_offset;
	*retval = bufflen - uio.uio_resid;
	spinlock_release(&fh->fh_splk);
	return err;
}

int fh_lseek(off_t offset, int whence, struct file_handle *fh, off_t *retval)
{
	int err = 0;
	if(!strcmp(fh->file_name, "con:"))
	{
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
			spinlock_acquire(&fh->fh_splk);
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
		if(spinlock_do_i_hold(&fh->fh_splk))
		{
			spinlock_release(&fh->fh_splk);
		}
		err = EINVAL;
		*retval = EINVAL;
		return err;
	}
	if(!spinlock_do_i_hold(&fh->fh_splk))
	{
		spinlock_acquire(&fh->fh_splk);
	}
	fh->offset = new_offset;
	*retval = fh->offset;
	spinlock_release(&fh->fh_splk);
	return err;
}
