//#ifndef _SYSCALL_H_
//#define _SYSCALL_H_


#include <cdefs.h>

//File syscalls we wrote.
int sys_open(const_userptr_t filename, int flags, mode_t mode, int* retval);
int sys_close(int fd, int *ret);
int sys_dup2(int oldfd, int newfd, int *ret);
int sys_write(int fd, void* buff, size_t len, int* ret);
int sys_read(int fd, void* buff, size_t len, int* ret);
int sys_lseek(int fd, off_t offset, int whence, off_t* ret);
uint64_t to64(uint32_t high, uint32_t low);
uint32_t high32(uint64_t value);
uint32_t low32(uint64_t value);

//Process syscalls. Should be in their own file...
int sys_getpid(int *ret);
int sys_fork(int *ret);
unsigned sys_copyin_buffer(char *buffer, char *buff_ptr, unsigned *len);
void print_padded_str(char *buffer, int len);
int sys_execv(const char *, char **, int *);
int sys_waitpid(int pid, int *status, int options, int *ret);
void sys__exit(int exitcode);
//int sys_execv(const_userptr_t program, const_userptr_t args, int *retval);

//#endif
