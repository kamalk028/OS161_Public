
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
#include <mips/trapframe.h>
#include <kern/wait.h>
#include <synch.h>

//static struct cv *parent_cv;
//static struct cv *execution_chamber;
//static struct lock *parent_cv_lock;
//static struct lock *chamber_lock;

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
	copyerr = copyinstr(filename, kname, len, &actualSizeRead);
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

int
sys_getpid(int *ret)//Still assumes pid's are integers.
{
	*ret = curproc->pid;
	return 0;
}

//forktest calls waitpid, so I have to write my own test, or finish that syscall first.
int
sys_fork(int *ret)
{
	KASSERT(curproc != NULL);
	KASSERT(curproc->ft != NULL);

	char name[16] = "fillername";
	struct proc *newproc;
	int result;
	unsigned int parent_pid = curproc->pid;
	int err = 0;
	int err_code = 0;
	newproc = proc_fork_runprogram(name, &err, &err_code);
	if(err)
	{
		*ret = ENOMEM;
		return -1;
	}
	//kprintf("Newly forked process: pid value: %d\n",newproc->pid);

	//NOTE: Args 3, 4, and 5 most likely should be changed.
	struct trapframe *tf;
	tf = newproc->tframe;
	result = thread_fork(name, newproc, enter_forked_process, tf, 0);

	if (result){
		//kprintf("Thread fork failed!");
		return -1;
	}

	if (curproc->pid == parent_pid){
		*ret = newproc->pid;
		return 0;
	}
	else
	{
		//kprintf("Control shouldn't reach here:::");
		return -1;
	}
}

	/*else if(curproc->pid == newproc->pid){
		*ret = 0;
		return 0;
	}
	else {
		kprintf("What kind of monster did you produce?!");
		*ret = 0;
		return 0;//I would make this return an error, but newproc->pid may be modified by thread_fork().
			//This could be normal behaviour. I just want to know if that happens.
	}*/

int
sys_waitpid(int pid, int *status, int options, int *ret)
{
	//pid represents the process we'll wait for to exit.
	//  Only parents of exiting children should ever call
	//  this function.
	//  Anything else, return error ECHILD. (Or, for a non-
	//  existent process, return ESRCH.)
	//status will get an encoded exit status assigned to it
	//  once the process exits (or if it has already exited).
	//  Unless status is passed in as NULL, in which case do
	//  not do anything to it.
	//  Passed in status value otherwise doesn't matter.
	//  The exit code used to calculate this value comes
	//  from the user space code.
	//options should always just be 0. Just assert that
	//  it is the only value ever passed in (unless you
	//  wanna implement more options). err = EINVAL.

	//When a parent calls this function on its child, the
	//  child can be totally removed from the process table.
	//When a parent exits, then the child should just go
	//  straight off the proc_table right when it exits.
	//  (that'll get handled in sys__exit)

	//BIGGEST HURDLE: understanding for certain how to get
	//  the various signals a process can have upon death.

	//On success, this function returns the pid of the exited
	//  child (which is the pid passed in).

	struct proc *child;
	child = get_proc(pid);

	if (options != 0)
	{
		return EINVAL;
	}

	if (child == NULL)
	{
		return ESRCH;
	}
	else if (child->ppid != curproc->pid)
	{
		return ECHILD;
	}

	//Waiting processes should go into a wait channel or cv,
	//  then get pulled out when their children exit.
	//  Problem with cv: must only signal the correct
	//  process when multiple parents are waiting...
	//  Could place cv_wait in a while loop, then make
	//  sys_exit use cv_broadcast... (smart idea)
	//Here's an issue: the child can exit by calling
	//  sys__exit, OR by recieving a fatal signal. I am
	//  not sure how to get that signal number, but when you
	//  figure it out, && it to this while condition.
	//For now, I'm assuming it'll all go to proc->exit_code.
	lock_acquire(child->parent_cvlock);
	while(child->exit_code == 4)//Apparently, 0-3 are reserved.
	{
		cv_wait(child->parent_cv, child->parent_cvlock);
	}
	lock_release(child->parent_cvlock);

	//Here is where the exit status is generated, based on the
	//  exit code that child now has, or the signal if the child
	//  was killed by a fatal signal or something.
	int calc_status = 0;
	
	switch(child->exit_code)
	{
		case 0:
			calc_status = _MKWAIT_EXIT(child->exit_code);
		break;
		case 1:
			calc_status = _MKWAIT_SIG(child->exit_code);
		break;
		case 2:
			calc_status = _MKWAIT_CORE(child->exit_code);
		break;
		case 3:
			calc_status = _MKWAIT_STOP(child->exit_code);
		break;
	}
	if(status != NULL)
	{
		*status = calc_status;	
	}
	//Add the other two or three cases here. CHECK thread.c TO SEE IF THOSE PROVIDE CASES!
	//Not sure how to handle __WSTOPPED...
	//Now all children waiting to be destroyed will be woken briefly.
	//  They will only continue if their exit_status has been created.

	lock_acquire(child->child_cvlock);
	child->exit_status = calc_status;
	cv_broadcast(child->child_cv, child->child_cvlock);
	lock_release(child->child_cvlock);

	//From there, the child processes will get destroyed.

	*ret = pid;
	return 0;
}



//Apparently, pid's should have their own type called pid_t.
//  I'm gonna see if I can just use integers.
//  Type casting will take place in syscall.c
/*int
sys_waitpid1(int pid, int *status, int options, int *ret)
{
	//Synch primitives can't be created at compile time.
	if (parent_cv_lock == NULL)
	{
		parent_cv = cv_create("parent_cv");
		parent_cv_lock = lock_create("parent_cv_lock");
	}

	if (chamber_lock == NULL)
	{
		execution_chamber = cv_create("exec_chamber");
		chamber_lock = lock_create("chamber lock");
	}

	//pid represents the process we'll wait for to exit.
	//  Only parents of exiting children should ever call
	//  this function.
	//  Anything else, return error ECHILD. (Or, for a non-
	//  existent process, return ESRCH.)
	//status will get an encoded exit status assigned to it
	//  once the process exits (or if it has already exited).
	//  Unless status is passed in as NULL, in which case do
	//  not do anything to it.
	//  Passed in status value otherwise doesn't matter.
	//  The exit code used to calculate this value comes
	//  from the user space code.
	//options should always just be 0. Just assert that
	//  it is the only value ever passed in (unless you
	//  wanna implement more options). err = EINVAL.

	//When a parent calls this function on its child, the
	//  child can be totally removed from the process table.
	//When a parent exits, then the child should just go
	//  straight off the proc_table right when it exits.
	//  (that'll get handled in sys__exit)

	//BIGGEST HURDLE: understanding for certain how to get
	//  the various signals a process can have upon death.

	//On success, this function returns the pid of the exited
	//  child (which is the pid passed in).

	struct proc *child;
	child = get_proc(pid);

	if (options != 0)
	{
		return EINVAL;
	}

	if (child == NULL)
	{
		return ESRCH;
	}
	else if (child->ppid != curproc->pid)
	{
		return ECHILD;
	}

	//Waiting processes should go into a wait channel or cv,
	//  then get pulled out when their children exit.
	//  Problem with cv: must only signal the correct
	//  process when multiple parents are waiting...
	//  Could place cv_wait in a while loop, then make
	//  sys_exit use cv_broadcast... (smart idea)
	//Here's an issue: the child can exit by calling
	//  sys__exit, OR by recieving a fatal signal. I am
	//  not sure how to get that signal number, but when you
	//  figure it out, && it to this while condition.
	//For now, I'm assuming it'll all go to proc->exit_code.
	while(child->exit_code == 4)//Apparently, 0-3 are reserved.
	{
		lock_acquire(parent_cv_lock);
		cv_wait(parent_cv, parent_cv_lock);
		lock_release(parent_cv_lock);
	}

	//Here is where the exit status is generated, based on the
	//  exit code that child now has, or the signal if the child
	//  was killed by a fatal signal or something.
	if (status == NULL)
	{
		;//We do not want to change status in this case.
	}
	else if (child->exit_code == 0)//case of __WEXITED
	{
		*status = (int) _MKWAIT_EXIT(0);
		child->exit_status = *status;
		//User code will handle *status.
	}
	//Add the other two or three cases here. CHECK thread.c TO SEE IF THOSE PROVIDE CASES!
	//Not sure how to handle __WSTOPPED...

	//Now all children waiting to be destroyed will be woken briefly.
	//  They will only continue if their exit_status has been created.
	lock_acquire(chamber_lock);
	cv_broadcast(execution_chamber, chamber_lock);
	lock_release(chamber_lock);

	//From there, the child processes will get destroyed.

	*ret = pid;
	return 0;
}*/

void sys__exit(int exitcode)
{
	unsigned l = 0;
	unsigned i = 0;
	if ((curproc->ppid == 0) || (get_proc(curproc->ppid) == NULL)){
		//If this process has no parent, just destroy it.
		//struct proc *temp;
		//temp = get_proc(curproc->pid);
		
		//proc_remthread(curthread);//This sets the curproc pointer to NULL.
		//curproc->p_numthreads--;
		//curthread->t_stack = NULL;
		//curproc = temp;
		//proc_destroy(curproc);
		lock_acquire(curproc->child_pids_lock);
		l = array_num(curproc->child_pids);
		//kprintf("CHILDREN OF THREAD WITH NO PARENT: %s", curproc->p_name);
		for(i=0; i<l; i++)
		{
			int t_pid = (int) array_get(curproc->child_pids, i);
			//kprintf("_____%d____ ", t_pid);
			struct proc *t_child = get_proc(t_pid);
			t_child->has_parent_exited = true;
			lock_acquire(t_child->child_cvlock);
			cv_broadcast(t_child->child_cv, t_child->child_cvlock);
			lock_release(t_child->child_cvlock);
		}
		lock_release(curproc->child_pids_lock);
		//kprintf("KAMAL: Exiting thread pid value: %d\n",curproc->pid);
		pt_remove(curproc->pid);
		thread_exit();
		return;//Code shouldn't even make it here.
	}

	//This will breifly wake all parents waiting in waitpid.
	//  But, they will only continue if their child now has an exit code.
	lock_acquire(curproc->parent_cvlock);
	curproc->exit_code = exitcode;//Remember: different from exit_status.
	cv_broadcast(curproc->parent_cv, curproc->parent_cvlock);
	lock_release(curproc->parent_cvlock);

	//Now, the child will wait until its parent has generated its exit status.
	//  Once that happens, the child will be released, then destroyed.
	lock_acquire(curproc->child_cvlock);
	while(curproc->exit_status < 0 && (curproc->parent_proc != NULL && !curproc->has_parent_exited)){
		cv_wait(curproc->child_cv, curproc->child_cvlock);
	}
	lock_release(curproc->child_cvlock);


	/*TODO: Need to remove the pid from parents child_pids */
	lock_acquire(curproc->parent_proc->child_pids_lock);
	l = array_num(curproc->parent_proc->child_pids);
	for(i=0; i<l; i++)
	{
		int t_pid = (int) array_get(curproc->parent_proc->child_pids, i);
		if(t_pid == (int) curproc->pid){
			array_remove(curproc->parent_proc->child_pids, i);
			break;
		}
	}
	lock_release(curproc->parent_proc->child_pids_lock);
	/*
	*TODO: For all the pids in child_pids set has_parent_exited = true
	*And release the child if it is waiting for the status to get updated
	*/
	//kprintf("CHILDREN OF THREAD: %s", curproc->p_name);
	lock_acquire(curproc->child_pids_lock);
	l = array_num(curproc->child_pids);
	for(i=0; i<l; i++)
	{
		int t_pid = (int) array_get(curproc->child_pids, i);
		//kprintf("_____%d____ ", t_pid);
		struct proc *t_child = get_proc(t_pid);
		t_child->has_parent_exited = true;
		lock_acquire(t_child->child_cvlock);
		cv_broadcast(t_child->child_cv, t_child->child_cvlock);
		lock_release(t_child->child_cvlock);
	}
	lock_release(curproc->child_pids_lock);

	//Remove the child process from the process table.
	//struct proc *temp;
	//temp = get_proc(curproc->pid);
	//kprintf("KAMAL: Exiting thread pid value: %d\n",curproc->pid);
	pt_remove(curproc->pid);





	//Destroy the child process.
	//  It is safe to do this now because only the child's
	//  parent must call waitpid.
	//proc_remthread(curthread);
	//curproc = temp;
	//curproc->p_numthreads--;
	//proc_destroy(curproc);
	thread_exit();
	return;

	//No thread should ever reach this point.
	kprintf("A thread somehow escaped the execution chamber!!");
}


/*void sys__exit1(int exitcode)
{
	//I wonder if I am getting errors because these locks can never be destroyed?
	if (parent_cv_lock == NULL)
	{
		parent_cv = cv_create("parent_cv");
		parent_cv_lock = lock_create("parent_cv_lock");
	}

	if (chamber_lock == NULL)
	{
		chamber_lock = lock_create("chamber_lock");
		execution_chamber = cv_create("execution_chamber");
	}

	curproc->exit_code = exitcode;//Remember: different from exit_status.

	if ((curproc->ppid == 0) || (get_proc(curproc->ppid) == NULL)){
		//If this process has no parent, just destroy it.
		//struct proc *temp;
		//temp = get_proc(curproc->pid);
		pt_remove(curproc->pid);
		//proc_remthread(curthread);//This sets the curproc pointer to NULL.
		curproc->p_numthreads--;
		//curthread->t_stack = NULL;
		//curproc = temp;
		proc_destroy(curproc);
		thread_exit();
		return;//Code shouldn't even make it here.
	}

	//This will breifly wake all parents waiting in waitpid.
	//  But, they will only continue if their child now has an exit code.
	lock_acquire(parent_cv_lock);
	cv_broadcast(parent_cv, parent_cv_lock);
	lock_release(parent_cv_lock);

	//Now, the child will wait until its parent has generated its exit status.
	//  Once that happens, the child will be released, then destroyed.
	while(curproc->exit_status == 0){
		lock_acquire(chamber_lock);
		cv_wait(execution_chamber, chamber_lock);
		lock_release(chamber_lock);
	}

	//Remove the child process from the process table.
	//struct proc *temp;
	//temp = get_proc(curproc->pid);
	pt_remove(curproc->pid);

	//Destroy the child process.
	//  It is safe to do this now because only the child's
	//  parent must call waitpid.
	//proc_remthread(curthread);
	//curproc = temp;
	curproc->p_numthreads--;
	proc_destroy(curproc);
	thread_exit();
	return;

	//No thread should ever reach this point.
	kprintf("A thread somehow escaped the execution chamber!!");
}*/

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

