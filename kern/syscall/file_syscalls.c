
/*  This file placed here at the reccomendation of
 *  a slide in Recitation 3.
 *  The file system syscalls will be called here from syscall.c
 */

//#include <unistd.h> 
/*
 Commenting out the include files.
 because bmake depend throws error saying cannot locate the include files.
 We can add more include files as and when we want.
 */
//#include <kern/fcntl.h>
#include <copyinout.h>
#include <syscall.h>
#include <types.h>
#include <kern/syscall.h>
#include <file_syscalls.h>
#include <kern/errno.h>
#include <proc.h>
#include <thread.h>
#include <current.h>
//#include <synch.h>
//May need more synch primitives.

int
sys_open(const_userptr_t filename, int flags, mode_t mode, int *retval)
{
	*retval = 0;
	KASSERT(curproc != NULL);
	KASSERT(curproc->ft != NULL);
	char kname[500];//copyinstr is needed to maintain the filename in the kernel.
	//size_t *filler;//Actual size should get filled in here eventually.
	// *filler = strlen(filename);
	int copyerr;
	size_t actualSizeRead = 0;
	size_t len = 500;
	copyerr = copyinstr(filename, kname, len, &actualSizeRead);//TEMPORARY: Passing in 64 for filename size.
	if(copyerr)
	{
		kprintf("Error copying filename!");
		*retval = -1;
		return copyerr;//ASSUMPTION: syscall.c will know what to do with these error codes.
	}

	/*
	 	Get pointer to the current process' file table,
		Get pointer to the current proc using curthread.
	 */
	//REMEMBER TO CHECK INCLUDE STATEMENTS IF YOU GET ERRORS!

	return ft_open(kname, flags, mode, curproc->ft, retval);
	//ft_open creates a file handle with the given flags and mode,
	//  then returns the fd which the fh was assigned inside retval.
	//If there was an error, that is returned to err in SYS_open.

}

int
sys_close(int fd, int *ret)
{
	*ret = 0;
	KASSERT(curproc != NULL);
	KASSERT(curproc->ft != NULL);
	return ft_close(fd, curproc->ft, ret);
}

int
sys_dup2(int oldfd, int newfd, int *ret)
{
	*ret = 0;
	KASSERT(curproc != NULL);
	KASSERT(curproc->ft != NULL);
	return ft_copy(oldfd, newfd, curproc->ft, ret);
}

int sys_write(int fd, void *buff, size_t len, int *ret)
{
	*ret = 0;
	struct file_table *ft;
	KASSERT(curproc != NULL);
	ft = curproc->ft;
	KASSERT(ft != NULL); 
	return ft_write(fd, buff, len, ft, ret);
}



//HERE IS THE START OF A sys_open FUNCTION I WAS GONNA WRITE!
/*int
sys_open(const char *filename, int flags, mode_t mode)
{
	int file_handle;
	//WARNING!! Process file table not yet implemented. FILLER CODE!
	file_handle = ft_open(filename, flags, curthread->t_proc->proc_ftable);

	//fh_open not written yet, either!
	file_handle = fh_open(filename, flags)

	//This is called by fh_open, not by sys_open.
	//vfs_open(path, flags, vnode);

	//Actually, vfs_open already has these cases for flags written.
	//NONE OF THIS WILL EVER BE NEEDED HERE!
	switch (flags) {
	    case O_RDONLY:
		//Open the file for reading only...
		break;
	    case O_WRONLY:
		//Open the file for writing only...
		break;
	    case O_RDWR:
		//Open the file for reading and writing.
		//There may be more cases than this.
		break;
	
	return file_handle;
}*/

/*int 
sys_write(int fd, const void* buff, size_t bufflen, int* retval)
{
	(void) fd;
	struct vnode *vnode;
	struct iovec iov;
	struct uio ku;
	int err=0;
	//int flags;
	off_t pos=0;
	char *fname;
	//char *dup_fname;
	//dup_fname=kstrdup("con:");
	struct proc* process;
	process = curthread->t_proc;
	vnode=process->vn;
	fname=kstrdup("con:");
	//dup

	//flags=O_WRONLY;
	if(fd == 0)
	{
		fname=kstrdup("con:");
		flags=O_WRONLY;//STDIN;
	}
	else if(fd == 1)
	{
		fname=kstrdup("con:");
		flags=O_WRONLY;
	}
	else if(fd == 2)
	{
		fname=kstrdup("con:");
		flags=O_WRONLY;
	}
	else
	{
		kprintf("fd > 2, we have not implemented it yet");
		return -1;
	}*/
	//err = vfs_open(dup_fname, flags, 0664, &vn);
	//kprintf("vfs_open has worked, the return int is %d\n", err);
/*	if(err)
	{
		kprintf("Could not open %s for write: %s\n", fname, strerror(err));
		return -1;
	}
	uio_kinit(&iov, &ku,(void *) buff, bufflen, pos, UIO_WRITE);
	err = VOP_WRITE(vnode, &ku);
	if(err)
	{*/

