/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than runprogram() does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <copyinout.h>

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname)
{
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new process. */
	KASSERT(proc_getas() == NULL);

	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	proc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}

	/* Warp to user mode. */
	enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}
/*
int
execv_runprogram(char *progname, char *argv[])
{
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	userptr_t userspace;

	// Open the file.
	//This might be what normally starts the console.
	//It gets progname passed into it, so I assume it can run anything else.
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	// We should be a new process.
	
	// THIS MAY BE A BIG PROBLEM!! Or, maybe I need to get rid of this...
	
	KASSERT(proc_getas() == NULL);

	// Create a new address space.
	as = as_create();
	if (as == NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	// Switch to it and activate it.
	proc_setas(as);
	as_activate();

	// Load the executable.
	result = load_elf(v, &entrypoint);
	if (result) {
		// p_addrspace will go away when curproc is destroyed
		vfs_close(v);
		return result;
	}

	// Done with the file now.
	vfs_close(v);

	// Define the user stack in the address space
	result = as_define_stack(as, &stackptr);//Always starts at 0x80000000
	if (result) {
		// p_addrspace will go away when curproc is destroyed
		return result;
	}
	
	//This new array should make the copying to user stack easier. THIS GETS MESSY.
	char *argv4[1000];//A version of argv where each value is 4 chars.
				//These will get copyout'ed onto the stack.
				//Elements are in the opposite order as they were.
				//NOTE THAT IT IS NOT AN ARRAY OF POINTERS.
	int i = 0;//Gotta find the first NULL element of *argv[].
	while (argv[i] != NULL)
	{
		i++;
	}
	int argc = i;//To be used by enter_new_process.
	i--;
	int c = 0;//Char counter for each string.
	int argv4_index = 0;//Incremented every four chars.
	int first_char_idx = 0;//Help make each arg in argv point to the correct 4-byte element.
	while (i >= 0)//Start from the last element of argv, skipping NULLs.
	{
		while (argv[i][c] != '\0')
		{
			argv4[argv4_index][c % 4] = argv[i][c];
			if (c == 0)
			{
				first_char_idx = argv4_index;
			}
			else if ((c % 4) == 3)
			{
				argv4_index++;
			}
			c++;
		}
		//At this point, a /0 terminator was found.
		argv4[argv4_index][c % 4] = '\0';
		while ((c % 4) != 3)
		{//Make sure every char is filled!
			c++;
			argv4[argv4_index][c % 4] = '\0';
		}
		//At this point, move on to the next string.
		//Update *argv[i] so that it points to the correct 4-char value.
		argv[i] = argv4[first_char_idx];
		i--;
		c = 0;
		argv4_index++;
	}

	//Copy the pointers in each element of argv[] to the userstack.
	//MAY NEED TO COPYOUT THE 4-BYTE ARRAYS FIRST!
	//WE PORBABLY NEED A DIFFERENT userptr_t FOR EACH COPYOUT!! For now, I just assumed copyout pushes the addresses onto a stack, and userptr_t userspace is recyclable.
	i = 0;
	int copyerr;
	while(argv[i] != NULL)
	{
		copyerr = copyout(argv[i], userspace, 320);
		if (copyerr) {
			kprintf("Copying argv pointers to userstak failed!");
			return copyerr;
		}
		i++;
	}
	copyerr = copyout(NULL, userspace, 320);//Not sure how many bytes should go here.
	if (copyerr) {
		kprintf("I couldn't even copy a NULL pointer to userstack!");
		return copyerr;
	}

	//Copy all the 4-byte arrays out to userspace.
	i = 0;
	size_t actualSize = 0;
	while(argv4[i] != NULL)
	{
		copyerr = copyoutstr(argv4[i], userspace, 4, &actualSize);
		if (copyerr) {
			kprintf("Copying of 4-byte strings failed!");
			return copyerr;//NOTE: To use copyout on those values, they must be char pointers, not just char arrays!
		i++;
	}

	//At this point, each element of argv should contain pointers to the first 4 bytes of each original string.

	// Warp to user mode. WARNING: I may have needed to copyout argv or something.
	//You are allowed to keep the environment as NULL, though.
	enter_new_process(argc,
			  *argv, //userspace addr of argv. Note that it is a userptr_t.
			  NULL, //userspace addr of environment, DO NOT TOUCH THIS.
			  stackptr, entrypoint);

	// enter_new_process does not return.
	panic("enter_new_process returned\n");
	return EINVAL;
}*/

