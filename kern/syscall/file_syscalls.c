
/*  This file placed here at the reccomendation of
 *  a slide in Recitation 3.
 *  The file system syscalls will be called here from syscall.c
 */

#include <types.h>
//#include <unistd.h>
//#include <fcntl.h>
//#include <copyinout.h>
#include <syscall.h>
#include <kern/syscall.h>
#include <file_syscalls.h>
//#include <vfs.h>
#include <proc.h>
#include <thread.h>
//#include <current.h>
//#include <synch.h>
//May need more synch primitives.

int
sys_open(const char *filename, int flags, mode_t mode, int *retval)
{
	/*
	 	Get pointer to the current process' file table,
		Get pointer to the current proc using curthread
		call fileopen function that takes in a filetable
	 */
	//REMEMBER TO UPDATE INCLUDE STATEMENTS FOR CERTAIN ARGS!!
	err = ft_open(&filename, flags, mode, curthread->t_proc->proc_ftable;
	//WHAT DO WE DO WITH retval?
	//(void) filename;
	//(void) flags;
	//(void) mode;
	return 0;
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

