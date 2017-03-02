//#ifndef _SYSCALL_H_
//#define _SYSCALL_H_


#include <cdefs.h>

int sys_open(const_userptr_t filename, int flags, mode_t mode, int* retval);
int sys_close(int fd, int *ret);
int sys_write(int fd, void* buff, size_t len, int* ret);
int sys_read(int fd, void* buff, size_t len, int* ret);
int sys_lseek(int fd, off_t offset, int whence, off_t* ret);
uint64_t to64(uint32_t high, uint32_t low);
uint32_t high32(uint64_t value);
uint32_t low32(uint64_t value);


//#endif
