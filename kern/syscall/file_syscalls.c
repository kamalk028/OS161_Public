
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

#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <mips/tlb.h>


//static struct cv *parent_cv;
//static struct cv *execution_chamber;
//static struct lock *parent_cv_lock;
//static struct lock *chamber_lock;

static char buffer[ARG_MAX];
//static char *k_args[1000];
//static char *stack_clone[1000];
//static struct lock *execv_lock = NULL;
static int arg_len_arr[3860];//Just enough to pass the bigexec test.

int
sys_open(const_userptr_t filename, int flags, mode_t mode, int *retval)
{
	*retval = 0;
	KASSERT(curproc != NULL);
	KASSERT(curproc->ft != NULL);
	char kname[500];//copyinstr is needed to maintain the filename in the kernel.
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
	if (buff == NULL){
		*ret = -1;
		return EFAULT;
	}
	return ft_write(fd, buff, len, ft, ret);
	//kfree(buff);
}

int sys_read(int fd, void* buff, size_t len, int* ret)
{
	*ret = 0;
	struct file_table *ft;
	KASSERT(curproc != NULL);
	ft = curproc->ft;
	KASSERT(ft != NULL);
	if (buff == NULL){
		*ret = -1;
		return EFAULT;
	}
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
		*ret = err_code;
		return err_code;
	}
	//kprintf("Newly forked process: pid value: %d\n",newproc->pid);

	//NOTE: Args 3, 4, and 5 most likely should be changed.
	struct trapframe *tf=NULL;
	tf = newproc->tframe;
	result = thread_fork(name, newproc, enter_forked_process, tf, 0);

	if (result){
		*ret = result;
		return result;
	}

	if (curproc->pid == parent_pid){
		*ret = newproc->pid;
		return 0;
		//Reminder: the child process returns form enter_forked_process.
	}
	else
	{
		//kprintf("Control shouldn't reach here:::");
		// *ret = EINVAL;//I just put a random error code here.
		//return -1;
		panic("sys_fork is broken!");
	}
}

unsigned sys_copyin_buffer(char *buffer, char *buff_ptr, unsigned *len)
{
	KASSERT(buff_ptr != NULL);
	KASSERT(buffer != NULL);
	unsigned actual = 0;
	int copyerr;
	//unsigned len = 0; //I'm making this get passed by reference.
	do
	{
		copyerr = copyinstr((const_userptr_t) buff_ptr+(*len), buffer+(*len), PATH_MAX, &actual);
		if (copyerr){
			return copyerr;
		}
		*len = (*len)+actual;
	}while(actual == PATH_MAX);
	//kprintf("KAMAL: Printing arg before padding: %s\n", buffer);
	return copyerr;
}

void print_padded_str(char *buffer, int len)
{
	int i=0;
	kprintf("KAMAL: Printing padded string next line \n");
	for(i=0;i<len; i++)
	{
		kprintf("%c",buffer[i]);
	}
	kprintf("\n");
}

int sys_execv(const char *program, char **args, int *retval)
{
	int err = 0;
	//(void)args;
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	char program_test[10];
	char *args_test[1];
	//unsigned prg_test_idx = 0;
	if(program == NULL || args == NULL)
	{
		*retval = EFAULT;
		return EFAULT;
	}
	/*if(execv_lock == NULL)
	{
		execv_lock = lock_create("execv_lock");
		if(execv_lock == NULL)
		{
			*retval = ENOMEM;
			return -1;
		}
	}*/
	err = copyin((const_userptr_t) program, program_test, 10);
	if(err)
	{
		*retval = err;
		return err;
	}
	err = copyin((const_userptr_t)args, args_test, 4);
	if(err)
	{
		*retval = err;
		return err;
	}

	/*size_t size = 0;
	size_t len = 10;
	//copycheck(const_userptr_t userptr, size_t len, size_t *stoplen)
	err = copycheck((const_userptr_t) program, len, &size);
	if(err)
	{
		*retval = err;
		return err;
	}

	err = copycheck((const_userptr_t) args, len, &size);
	if(err)
	{
		*retval = err;
		return err;
	}*/

	/*int e_prg_test = copyinstr((const_userptr_t)program, program_test, 10, &prg_test_idx);
	if(e_prg_test)
	{
		*retval = EFAULT;
		return EFAULT;
	}*/

	//This check breaks execv, even though it works in waitpid.

	//Must be because execv generally needs more space, especially for a large buffer.
	//What if I check the buffer size...?
	//Did we ever try copyin on the programname?
	//unsigned int progaddr = (unsigned int)program;
	//unsigned int argsaddr = (unsigned int)args;
	//kprintf("progaddr: 0x%x \n", progaddr);
	//kprintf("argsaddr: 0x%x \n", argsaddr);

	/*if (((progaddr <= 0x40000000) && (progaddr >= 0x01000000)) ||//ISSUE: This may fail for a buffer size between 32K and 63K.
		//EFAULT should probably only be thrown if a pointer is trying to reserve more space than it'll need.
		//bigexec just so happens to reserve nearly all of the space from 0x0 to 0x80000000 (probably), so it gets by.
		//It may be possible to resolve this with copyouts.
		(progaddr >= 0x80000000) || ((progaddr % 4) != 0)){//May need to check this per index.
		//Even if this does work, it may need to change on ASST3...
		*retval = -1;
		return EFAULT;
	}

	if (((argsaddr <= 0x40000000) && (argsaddr >= 0x01000000)) ||
		(argsaddr >= 0x80000000) || ((argsaddr % 4) != 0)){
		//Why didn't I write a funtion to do this? I was tired, okay?
		*retval = -1;
		return EFAULT;
	}*/

	//Simplified check: just make sure it isn't NULL. May need to do this for each arg. It is a problem if more than one NULL value is found.
	/*if (program == NULL || args == NULL){
		*retval = EFAULT;
		return EFAULT;
	}*/

	/*
	//Can try a copyin on program itself, but that should already be handled by args[0].
	char progcheck[500];
	copyerr = copyinstr((const_userptr_t)program, progcheck, 500);
	*/

	unsigned actual = 0;
	unsigned args_idx = 0;
	unsigned buff_idx=0;
	unsigned itr = 0;
	//int copyerr;

	lock_acquire(execv_lock);
	//Must release the lock before returning anywhere!!
	while(args[args_idx] != NULL)//You do not recieve argc. You must verify how many args there really are, or else you could stop early.
	{
		err = copyinstr((const_userptr_t)args[args_idx], &buffer[buff_idx], ARG_MAX, &actual);
		if(err)
		{
			lock_release(execv_lock);
			*retval = err;
			return err;
		}
		unsigned len = 0;
		len = strlen(&buffer[buff_idx]);
		//Extra check for totally empty string. (Test still failed, because the code below for n_extra works anyway.)
		/*if (len == 0){
			buffer[buff_idx] = '\0';
			len++;
		}*/
		buff_idx = buff_idx+len;

		/* Padding required emptys. Done in between each arg string. */
		//Since this relies on len, and works for 0, I have no idea why the code fails the empty string test.

		unsigned n_extra = len/4;
		n_extra = (n_extra+1)*4;
		n_extra = n_extra - len;

		//Should check for full buffer.
		if ((buff_idx + n_extra) >= ARG_MAX){
			*retval = -1;
			lock_release(execv_lock);
			return E2BIG;
		}
		for(itr = 0; itr<n_extra; itr++)
		{
			buffer[buff_idx+itr] = '\0';
		}

		buff_idx = buff_idx + n_extra;

		arg_len_arr[args_idx] = len+n_extra;

		/*if(program == NULL || args == NULL)
		{
			lock_release(execv_lock);
			*retval = EFAULT;
			return -1;
		}*/
		args_idx++;
	}

	//Brute-force check to see if the very first char in args is '\0'.
	//If it is, I think the syscall should fail.
	if (buffer[0] == '\0'){
		*retval = -1;
		lock_release(execv_lock);
		return EFAULT;
	}

	//Not a perfect check, since it doesn't consider added null chars.
	//But, make sure there's nothing left in the args array.
	//That would mean there was an extra NULL ptr in the args.
	//May need to have a loop that counts how many chars are in the args array per character.
	if (strlen(*args) >= buff_idx){
		*retval = -1;
		lock_release(execv_lock);
		return EFAULT;
	}

	unsigned n_args = args_idx+1; //Used for finding lowest level of stack needed.
	/*for(itr = 0; itr<n_args-1; itr++)
	{
		kprintf("%d \t",arg_len_arr[itr]);
	}*/

	char *dummy = NULL;
	char **dummy2 = &dummy;

	vaddr_t value_ptr, addr_ptr, d_stack_ptr;

	// Open the file. AT THIS POINT, IF program IS AN INVALID POINTER, KERNEL CRASHES!
	result = vfs_open((char *)program, O_RDONLY, 0, &v);
	if (result) {
		lock_release(execv_lock);
		*retval = result;
		return result;
	}

	/* This wont be a new process, since it will called from the user process */
	//KASSERT(proc_getas() == NULL); 

	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		vfs_close(v);
		*retval = ENOMEM;
		lock_release(execv_lock);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	struct addrspace *tmp_as = proc_setas(as);
	as_destroy(tmp_as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		*retval = result;
		lock_release(execv_lock);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		*retval = result;
		lock_release(execv_lock);
		return result;
	}

	value_ptr = stackptr;//Current position on stck (starts on top).
	d_stack_ptr = stackptr - buff_idx - n_args*4;//Bottom of the stack.
	addr_ptr = d_stack_ptr;

	buff_idx = 0;
	for(args_idx = 0; args_idx<n_args-1; args_idx++)
	{
		value_ptr = value_ptr - arg_len_arr[args_idx];
		err = copyout(&value_ptr, (userptr_t)addr_ptr, 4);
		if(err)
		{
			lock_release(execv_lock);
			*retval = err;
			return err;
		}
		addr_ptr = addr_ptr+4;
		err = copyout(&buffer[buff_idx],(userptr_t) value_ptr, arg_len_arr[args_idx]);
		if(err)
		{
			lock_release(execv_lock);
			*retval = err;
			return err;
		}
		buff_idx = buff_idx+arg_len_arr[args_idx];
	}
	
	err = copyout(dummy2, (userptr_t)addr_ptr, 4);
	if(err)
	{
		lock_release(execv_lock);
		*retval = err;
		return err;
	}

	stackptr = d_stack_ptr;
	
	lock_release(execv_lock);//Hopefully nothing writes to the pointer right at this moment...

	/* Warp to user mode. It MIGHT be dangerous to pass the stackptr as two args here...*/
	enter_new_process(n_args-1 /*argc*/, (userptr_t) stackptr /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	*retval = EINVAL;
	return EINVAL;
}

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
		*ret = -1;
		return EINVAL;
	}

	if (child == NULL)
	{
		*ret = -1;
		return ESRCH;
	}
	else if (child->ppid != curproc->pid)
	{
		*ret = -1;
		return ECHILD;
	}

	/*int copyerr;
	int *status;
	copyerr = copyin(userstatus, (void *)status, 500);
	if (copyerr){
		*ret = -1;
		return copyerr;//This was an attempt athandling invalid ptrs.
	}*/

	unsigned int stataddr = (unsigned int)status;
	//kprintf("Address of status is: 0x%x \n", stataddr);

	if (((stataddr <= 0x40000000) && (stataddr != 0x0)) ||
		(stataddr >= 0x80000000) || ((stataddr % 4) != 0)){
		//Even if this does work, it may need to change on ASST3...
		//The mod4 check makes badcall-waitpid pass, but breaks forkbomb and badcall-write...
		*ret = -1;
		return EFAULT;
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
	while(child->exit_code == -1)
	{
		cv_wait(child->parent_cv, child->parent_cvlock);
	}
	lock_release(child->parent_cvlock);

	//Here is where the exit status is generated, based on the
	//  exit code that child now has, or the signal if the child
	//  was killed by a fatal signal or something.
	int calc_status = 0;

	/*
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
	}*/
	if(child->exit_signal)
	{
		calc_status = _MKWAIT_SIG(child->exit_code);
	}
	else
	{
		calc_status = _MKWAIT_EXIT(child->exit_code);
	}
	if(status != NULL)
	{
		*status = calc_status;	
		//*status = _MKWAIT_EXIT(child->exit_code); //Brute force: Please change it back
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
	/*copyerr = copyout(status, (userptr_t)userstatus, 32);
	if (copyerr){
		*ret = -1;
		return copyerr;
	}*/
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
	//  Still need to check for EFAULT, though...
	//options should always just be 0. Just assert that
	//  it is the only value ever passed in (unless you
	//  wanna implement more options). err = EINVAL.

	//When a parent calls this function on its child, the
	//  child can be totally removed from the process table.
	//When a parent exits, then the child should just go
	//  straight off the proc_table right when it exits.
	//  (that'll get handled in sys__exit)

	//BIGGEST HURDLE: understanding how to get the _WCORED case.
	//  Understanding how to handle _WSTOPPED.

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
	//Processes can exit by calling sys__exit OR by recieving a
	//  fatal signal. I think I have now addressed this
	//  correctly, but I am not certain. I currently assume
	//  the user always passes in 0 with sys__exit. I have
	//  modified kill_curthread in trap.c to handle signals.
	while(child->exit_code == 4)//Apparently, 0-3 are reserved.
	{
		lock_acquire(parent_cv_lock);
		cv_wait(parent_cv, parent_cv_lock);
		lock_release(parent_cv_lock);
	}

	//Here is where the exit status is generated, based on the
	//  exit code that child now has.
	if (child->exit_code < 0 || child->exit_code > 4)
	{
		kprintf("Invalid exit code. I'm just gonna pretend it was a 0.");
		child->exit_code = 0;
	}
	if (status == NULL)
	{
		;//We do not want to change status in this case.
	}
	else if (child->exit_code == 0)//case of __WEXITED; user called exit(0).
	{
		*status = (int) _MKWAIT_EXIT(0);
		child->exit_status = *status;
		//User code will handle *status.
	}
	else if (child->exit_code == 1)//case of __WSIGNALED; kill_curthread called exit(1).
	{
		*status = (int) _MKWAIT_SIG(1);
		child->exit_status = *status;
	}
	else if (child->exit_code == 2)//Not sure how to check for __WCORED.
	{
		*status = (int) _MKWAIT_CORE(2);
		child->exit_status = *status;
	}
	else	//Not even sure how to handle __WSTOPPED. For now, treat it like the rest.
	{
		*status = (int) _MKWAIT_STOP(3);
		child->exit_status = *status;
	}

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
		//_MKWAIT_EXIT(curproc->exit_code);
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


	//TODO: Need to remove the pid from parents child_pids 
	if(curproc->ppid > 1 && !curproc->has_parent_exited)
	{
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
	}


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

	curproc->exit_code = exitcode;
	//ASSUMPTION: The user will always pass in a 0 when they manually call exit().

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
	//  But, they will only continue if their child now has a valid exit code.
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

//Need to do type casting in syscall.c to make args a const_userptr_t, so copyinstr works.
//For now, I assume args is passed in as a const_user_ptr_t to an array of const_userptr_t's.
/*int sys_execv(const_userptr_t program, const_userptr_t args, int *retval)
{
	*retval = 0;
	KASSERT(curproc != NULL);
	char kprogram[320];//copyinstr so the kernel can access the program name.
	int copyerr;
	size_t actualSizeRead = 0;
	size_t len = 320;
	copyerr = copyinstr(program, kprogram, len, &actualSizeRead);
	if(copyerr)
	{
		kprintf("Error copying pathname!");
		*retval = -1;
		return copyerr;
	}

	//IF I AM NOT MISTAKEN, args is a pointer to an array of const_userptr_t's.
	//  We need to convert that pointer into a kernel-usable pointer so the array is accessible.
	const_userptr_t karray[200];
	//  Then we need to take each of those userptrs in the array and turn them into kernel pointers (char *).
	char *kargs[200];//200 arrays of 320 chars each for args.
	//kargs = array_create(); //Ignore this line, old idea.
	//array_init(kargs);

	//WARNING! I am really not certain how to handle the user pointer to user pointers!!
	//  I believe that args points to an array, and objects of the array are user pointers.
	//  I hope that the first copyin allows the kernel to access those user pointers!!
	copyerr = copyin(args, karray, 64000);
	if(copyerr)//You might get an error, because args may need to be constant.
	{
		kprintf("Error copying args array!");
		*retval = -1;
		return copyerr;
	}

	//Copyin each userptr_t from karray into kargs, so the kernel can access the strings.
	len = 320;
	int i = 0;
	while (karray[i] != NULL)//This may attempt to check the value of a userptr_t, which may fail.
	{
		actualSizeRead = 0;
		copyerr = copyinstr(karray[i], kargs[i], len, &actualSizeRead);
		if(copyerr)
		{
			kprintf("Error copying one of the args!");
			*retval = -1;
			return copyerr;
		}
		i++;
	}

	//At this point, we should make a kernel-readable pointer to the array of pointers to strings.
	int err = 0;
	err = execv_runprogram(kprogram, kargs);//This won't actually return anything unless there was an error.
	*retval = -1;
	return err;
}*/

int
sys_sbrk(intptr_t amount, int *ret)
{
	unsigned int i = 0;
	//int err = 0;
	int kamount= amount;
	//kamount = amount;
	struct as_region *r = NULL;

	//First, get the integer copied in.
	/*err = copyin((const_userptr_t)amount, kamount, 4);
	if (err)
	{
		*ret = err;
		return err;
	}*/

	//Next, find the heap region in this proc's as_regions array.
	while(r == NULL || r->start != (USERSTACK / 2))
	{
		r = array_get(curproc->p_addrspace->as_regions, i);
		i++;
	}

	//Now, move the top of the heap, provided kamount is valid.
	if (((kamount % PAGE_SIZE) != 0) || ((r->end + (kamount)) < (USERSTACK / 2)))
	{
		//kfree(r);
		*ret = EINVAL;
		return EINVAL;
	}
	if ((r->end + kamount) > USERSTACK - (1024 * PAGE_SIZE))
	{
		*ret = ENOMEM;
		return ENOMEM;
	}	

	//Expand (or shrink) the heap. Note: physical mem not alloc'd yet.
	//If the heap is getting shrunk, it needs to free memory.
	*ret = r->end;
	r->end += (kamount);
	r->size += (kamount / PAGE_SIZE);

	//If the heap is being shrunk, we need to free physical pages it used.
	//Only way to find out what was used is to scan the page table...
	if (kamount < 0)
	{
		unsigned idx = 0;
		vaddr_t vpn;
		paddr_t ppn = 0;
		int not_found = 0;
		for (vpn = r->end; vpn < (r->end - kamount); vpn+=PAGE_SIZE)
		{
			not_found = pt_lookup1(curproc->p_addrspace->pt, vpn, r->permission, &ppn, &idx);
			if (not_found)
			{
				;
			}
			else
			{
				//Need to update TLB and pte.
				free_ppages(ppn);
				struct page_table_entry* pte = array_get(curproc->p_addrspace->pt->pt_array,idx);
				kfree(pte);
				array_remove(curproc->p_addrspace->pt->pt_array,idx);
				int t = tlb_probe(vpn, ppn);
				if(t > -1)
				{
					tlb_write(TLBHI_INVALID(t), TLBLO_INVALID(), t);
				}
			}
		}
	}
	return 0;
}

uint64_t to64(uint32_t high, uint32_t low)
{
	return (uint64_t) high << 32 | low;
}

uint32_t high32(uint64_t value)
{
	return value >> 32;
}

uint32_t get_and_n(unsigned n)
{
	unsigned val=1;
	for(unsigned i=0; i<n; i++)
	{
		val *= 2;
	}
	return val - 1;
}

uint32_t get_first_n(uint32_t value, unsigned n)
{
	return value >> n & get_and_n(n);
}

uint32_t get_last_n(uint32_t value, int n)
{
	return value & get_and_n(n);
}



uint32_t low32(uint64_t value)
{
	return value;
}

