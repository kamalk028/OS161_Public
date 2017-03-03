
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

int sys_read(int fd, void* buff, size_t len, int* ret)
{
	*ret = 0;
	struct file_table *ft;
	KASSERT(curproc != NULL);
	ft = curproc->ft;
	KASSERT(ft != NULL);
	return ft_read(fd, buff, len, ft, ret);
}

int sys_lseek(int fd, off_t offset, int whence, off_t* ret)
{
	*ret = 0;
	int err = 0;
	struct file_table *ft;
	ft = curproc->ft;
	err = ft_lseek(fd, offset, whence, ft, ret);
	return err;
}

uint64_t to64(uint32_t high, uint32_t low)
{
	return (uint64_t) high << 32 | low;
}

uint32_t high32(uint64_t value)
{
	return value >> 32;
}

uint32_t low32(uint64_t value)
{
	return value;
}

