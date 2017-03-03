//#ifndef _SYSCALL_H_
//#define _SYSCALL_H_


#include <cdefs.h>

int sys_open(const_userptr_t filename, int flags, mode_t mode, int* retval);
int sys_close(int fd, int *ret);
int sys_dup2(int oldfd, int newfd, int *ret);
int sys_write(int fd, void* buff, size_t len, int* ret);


//#endif
