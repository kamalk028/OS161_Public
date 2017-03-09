
#include <copyinout.h>
#include <syscall.h>
#include <types.h>
#include <kern/syscall.h>
#include <file_syscalls.h>
//#include <proc_syscalls.h>
#include <kern/errno.h>
#include <proc.h>
#include <thread.h>
#include <current.h>

//forktest calls waitpid, so I have to write my own test, or finish that syscall first.
/*int
sys_fork(int *ret)
{
	KASSERT(curproc != NULL);
	KASSERT(curproc->ft != NULL);

	char name[16] = "fillername";
	struct proc *newproc;
	int result;
	int parent_pid = curproc->pid;
	newproc = proc_fork_runprogram(name);

	result = thread_fork(name, newproc, NULL, NULL, NULL);
	if (result){
		kprintf("Thread fork failed!!");
		return -1;
	}

	//At this point, there should be two processes...
	if (curproc->pid == parent_pid){
		*ret = newproc->pid;
		return 0;
	}
	else if(curproc->pid == newproc->pid){
		*ret = 0;
		return 0;
	}
	else {
		kprintf("What kind of monster did you produce?!");
		*ret = 0;
		return 0;//I would make this return an error, but newproc->pid may be modified by thread_fork().
			//This could be normal behaviour. I just want to know if that happens.
	}
}*/
