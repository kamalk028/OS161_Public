//#ifndef _SYSCALL_H_
//#define _SYSCALL_H_


#include <cdefs.h>

int sys_open(const char *filename, int flags, mode_t mode, int* retval);
int sys_write(int fd, void* buff, size_t len, int* ret);


//#endif
