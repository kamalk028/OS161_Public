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

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <synch.h>
#include <spinlock.h>
#include <machine/vm.h>
#include <spl.h>
#include <cpu.h>
#include <mainbus.h>




/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

 #define DUMBVM_STACKPAGES    18
 #define FREE_STATE 0
 #define FIXED_STATE 1
 #define DIRTY_STATE 2
 #define CLEAN_STATE 3


//Coremap objects.
//static struct lock *cm_lock = NULL;//Memory must be allocated for the lock as well!
static struct coremap *cm_entry = NULL;//Can initialze the coremap components later. //memsteal will later be used to make this an array.
static unsigned int kern_pages = 0;//Total number of physical pages the kernel has used before and during coremap initilization.
static unsigned int npages_used = 0;//Total number of coremap pages used by all processes.
static unsigned int total_npages = 0;//Total number of pages in the core map.
static struct spinlock cm_splk;


static
void
dumbvm_can_sleep(void)
{
	if (CURCPU_EXISTS()) {
		/* must not hold spinlocks */
		KASSERT(curcpu->c_spinlocks == 0);

		/* must not be in an interrupt handler */
		KASSERT(curthread->t_in_interrupt == 0);
	}
}

static
paddr_t
getppages(unsigned long npages)
{
	//as_prepare_load also calls this function, not just alloc_kpages.
	paddr_t addr;
	/*bool is_lock_created = cm_lock != NULL;

	if(is_lock_created)
	{
	lock_acquire(cm_lock);
	}*/

	if(CURCPU_EXISTS() && !spinlock_do_i_hold(&cm_splk))
	{
		spinlock_acquire(&cm_splk);
	}

	unsigned long i=kern_pages;
	unsigned long cont_pages = 0;

	while(i<total_npages && cont_pages < npages)
	{ 
		if(cm_entry[i].page_status == FREE_STATE)
		{
			cont_pages++;
		}
		else
		{
			cont_pages = 0;
		}
		i++;
	}
	if(i == total_npages)
	{
		return 0; //TODO: check with TA if we can return 0 or something else. For 3.3, we will call page swapping code here.
	}
	else
	{
		i = i - (npages);//Change i to first open index.
		addr = i * PAGE_SIZE;
		while(cont_pages > 0)
		{
			cm_entry[i].page_status = FIXED_STATE;
			cm_entry[i].npages = npages;
			if(CURCPU_EXISTS())
			{
				cm_entry[i].pid = curproc->pid;
			}
			else
			{
				kern_pages++;
				cm_entry[i].pid = 1;
			}
			i++;
			cont_pages--;
		}
	}

	npages_used+=npages;

	if(CURCPU_EXISTS() && spinlock_do_i_hold(&cm_splk))
	{
		spinlock_release(&cm_splk);
	}
	/*if(is_lock_created)
	{
	lock_release(cm_lock);
	}*/

	return addr;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

void
coremap_init()
{
	int num_core_entries = mainbus_ramsize() / PAGE_SIZE;//Hopefully this computes the amount of memory we have.

	/*Manual memory allocation for core map :*/
	unsigned long npages = (sizeof(*cm_entry) * num_core_entries) / PAGE_SIZE;
	if (((sizeof(*cm_entry) * num_core_entries) % PAGE_SIZE) != 0) { npages++; }//Calc the number of physical pages the core map requires.

	paddr_t pa = ram_stealmem(npages);//We should never call this function again. It might even be bad to use it right now.
	if (pa==0) {
		panic("Mem allocation for coremap failed:::");
	}
	paddr_t pa_temp = pa;
	cm_entry = (struct coremap*)PADDR_TO_KVADDR(pa_temp); //Change to kernel virtual address
	total_npages = num_core_entries;

	/*Memory allocation for coremap_lock:
	unsigned int npages_lock = (sizeof(*cm_lock))/PAGE_SIZE;
	if(sizeof(*cm_lock) > npages_lock * PAGE_SIZE)
	{
		npages_lock++;
	}
	paddr_t lock_addr = ram_stealmem(npages_lock);//We are now allocating an entire page for the lock. Probably will need to change!
	if (lock_addr==0) {
		panic("Mem allocation for coremap failed:::");
	}
	cm_lock = (struct lock *) PADDR_TO_KVADDR(lock_addr);*/


	paddr_t f_addr = pa + (npages*PAGE_SIZE);
	int first_chunk = f_addr / PAGE_SIZE;
	int i;

	for(i = 0; i<first_chunk; i++)
	{
		cm_entry[i].page_status = FIXED_STATE;
		cm_entry[i].npages = first_chunk;
		cm_entry[i].pid = 1;//Special pid value for kernel involved memory.
	}

	for (i = first_chunk; i < num_core_entries; i++)
	{
		cm_entry[i].page_status = FREE_STATE;
		cm_entry[i].npages = 0;
		cm_entry[i].pid = 0;//Default value; normally assigned curproc->pid once memory is fixed.
	}
	npages_used = first_chunk;
	kern_pages = first_chunk;

	//cm_lock = lock_create("coremap_lock"); // Is it safe to call kmalloc here? 
	spinlock_init(&cm_splk);

	return;
} 

/* Copying dumbvm functions 
 * Just to get the kernel started
*/

void
vm_bootstrap(void)
{
	/* Do nothing. */
}

unsigned int coremap_used_bytes() {

	/* dumbvm doesn't track page allocations. Return 0 so that khu works. */
	if(CURCPU_EXISTS() && !spinlock_do_i_hold(&cm_splk))
	{
		spinlock_acquire(&cm_splk);
	}
	unsigned int bytes = npages_used * PAGE_SIZE;
	if(CURCPU_EXISTS() && spinlock_do_i_hold(&cm_splk))
	{
		spinlock_release(&cm_splk);
	}
	return bytes;
}

vaddr_t
alloc_kpages(unsigned npages)
{
	//Taarget addresses can be calculated with cm_index * PAGE_SIZE.
	paddr_t pa;

	dumbvm_can_sleep();
	pa = getppages(npages); //This should return a physical address, and no longer just "steal" memory.
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa); //All this does is add 0x80000000 to pa.
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
free_kpages(vaddr_t addr)
{
	paddr_t p_addr = addr - MIPS_KSEG0;
	unsigned int i = p_addr/PAGE_SIZE;// i is assumed to be the index of the first coremap entry used by the process.
	if(CURCPU_EXISTS() && !spinlock_do_i_hold(&cm_splk))
	{
		spinlock_acquire(&cm_splk);
	}
	int chunk = cm_entry[i].npages;
	int temp_chunk = chunk;
	while(chunk > 0)
	{
		KASSERT(cm_entry[i].pid != 1);
		KASSERT(cm_entry[i].page_status != FREE_STATE);
		cm_entry[i].page_status = FREE_STATE;
		cm_entry[i].npages = 0;
		/*if(CURCPU_EXISTS())
		{
			//KASSERT(cm_entry[i].pid == curproc->pid);
		}*/
		cm_entry[i].pid = 0;

		i++;
		chunk--;
	}
	npages_used-=temp_chunk;
	if(CURCPU_EXISTS() && spinlock_do_i_hold(&cm_splk))
	{
		spinlock_release(&cm_splk);
	}
	
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	/*
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		// We always create pages read-write, so we can't get this.
		panic("dumbvm: got VM_FAULT_READONLY\n");
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		
		//  No process. This is probably a kernel fault early
		//  in boot. Return EFAULT so as to panic instead of
		//  getting into an infinite faulting loop.
		 
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL) {
		
		 // No address space set up. This is probably also a
		 // kernel fault early in boot.
		 
		return EFAULT;
	}

	// Assert that the address space has been set up properly. 
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		paddr = (faultaddress - vbase1) + as->as_pbase1;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = (faultaddress - vbase2) + as->as_pbase2;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		paddr = (faultaddress - stackbase) + as->as_stackpbase;
	}
	else {
		return EFAULT;
	}

	// make sure it's page-aligned.
	KASSERT((paddr & PAGE_FRAME) == paddr);

	// Disable interrupts on this CPU while frobbing the TLB.
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
	*/


	//vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	vaddr_t stackbase, stacktop; //We will probably want to replace these eventually!
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;
	bool valid_addr = 0;

	faultaddress &= PAGE_FRAME; //This effectively chops off 12 bits of faultaddress.

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	
	switch (faulttype) {
		//For now, we will ignore this. faulttype may not matter?
	    case VM_FAULT_READONLY:
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {

		//  No process. This is probably a kernel fault early
		//  in boot. Return EFAULT so as to panic instead of
		//  getting into an infinite faulting loop.

		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL) {

		 // No address space set up. This is probably also a
		 // kernel fault early in boot.

		return EFAULT;
	}

	// Assert that the address space has been set up properly.
	for (i = 0; i < array_num(as->as_regions); i++)
	{
		struct as_region *r = array_get(as->as_regions, i);
		KASSERT(r->start != 0);
		KASSERT(r->end != 0);
		KASSERT(r->size != 0);
		KASSERT((r->start & PAGE_FRAME) == r->start);
		KASSERT((r->end & PAGE_FRAME) == r->end);
		if(faultaddress >= start && faultaddress < end)
		{
			valid_addr = 1;
			break;
		}
	}

	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	if (faultaddress >= stackbase && faultaddress < stacktop) {
		//paddr = (faultaddress - stackbase) + as->as_stackpbase;
		valid_addr = 1;
	}
	
	if(!valid_addr)
	{
		return EFAULT;
	}

		


	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;

}

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;//May want to return another error instead?
	}

	/*
	 * Old dumbvm initializations:
	 */
	/*
	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;
	*/

	/*
	 * Our new stuff:
	 */
	
	as->as_regions = array_create();
	array_init(as->as_regions);
	//as->next_start = 0x00000000; //Whatever calls as_define_region might need this.
	//as->stack_start = USERSTACK; //Default 0x7fffffff, I think.
	as->pt = pt_create(); //This effectively initializes an array.
	//May add heap declaration here, as well.
	 
	return as;
}


int
as_copy(struct addrspace *old, struct addrspace **ret)
{
/*	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}


	   //Old dumbvm code.

	newas->as_vbase1 = old->as_vbase1;
	newas->as_npages1 = old->as_npages1;
	newas->as_vbase2 = old->as_vbase2;
	newas->as_npages2 = old->as_npages2;

	// (Mis)use as_prepare_load to allocate some physical memory.
	if (as_prepare_load(newas)) {
		as_destroy(newas);
		return ENOMEM;
	}

	KASSERT(newas->as_pbase1 != 0);
	KASSERT(newas->as_pbase2 != 0);
	KASSERT(newas->as_stackpbase != 0);

	memmove((void *)PADDR_TO_KVADDR(newas->as_pbase1),
		(const void *)PADDR_TO_KVADDR(old->as_pbase1),
		old->as_npages1*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(newas->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(newas->as_stackpbase),
		(const void *)PADDR_TO_KVADDR(old->as_stackpbase),
		DUMBVM_STACKPAGES*PAGE_SIZE);

	*ret = newas;
	return 0;
	
*/
	/*
	 * Our new code:
	 */


	(void)old;
	(void)*ret;
	// *ret = newas;
	return 0;
}


void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */
	dumbvm_can_sleep();
	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
	/*
	 * We may actually be able to leave this as-is.
	 * Unless, we try to implement the allowance of multiple
	 *   process' address translations at once in the TLB.
	 */
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	/*
	size_t npages;

	dumbvm_can_sleep();

	// Align the region. First, the base...
	memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	// ...and now the length.
	memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = memsize / PAGE_SIZE;

	// We don't use these - all pages are read-write
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		return 0;
	}

	// Support for more than two regions is not available.
	kprintf("dumbvm: Warning: too many regions\n");
	return ENOSYS;
	*/

	/*
	 * Our new code:
	 */

	
	//Create a new region called newregion.
	struct as_region *newregion;
	//Allocate memory just for the attributes of as_region, NOT the memory it will request.
	newregion = kmalloc(sizeof(struct as_region));
	unsigned int i = 0;
	for (i = 0; i < array_num(as->as_regions); i++)
	{
		struct as_region *r = array_get(as->as_regions, i);
		if(vaddr >= r->start && vaddr < r->end)
		{
			return ENOSYS;
		}
	}
	newregion->start = vaddr;
	newregion->size = (memsize / PAGE_SIZE) + 1; //If memsize is page-aligned, then remove the +1.
	if ((memsize % PAGE_SIZE) == 0) {newregion->size--;}
	//Make sure we actually have enough memory for this new region. (Check that we haven't collided with the stack.)
	//  Eventually, we may want to check for a collision with the heap instead.
	/*if (((newregion->size * PAGE_SIZE) + as->next_start) > (as->stack_start - PAGE_SIZE))
	{
		kprintf("Warning: Way too much memory allocated in one address space!");
		return ENOSYS;
	}*/
	newregion->end = newregion->start + (newregion->size * PAGE_SIZE);
	//Update the start address for the next region. NOT NEEDED IF ADDRESSES ARE PASSE IN!
	//as->next_start += (newregion->size * PAGE_SIZE);
	newregion->permission = 0; //Default value. Gets increased if any permission flags are set.
	if (executable) {newregion->permission++;}
	if (writeable) {newregion->permission+=2;}
	if (readable) {newregion->permission+=4;}
	//Expand the address space's stack to give this region a stack of its own.
	//  MOVE THIS CODE TO as_define_stack()!
	newregion->stack = as->stack_start;
	as->stack_start -= PAGE_SIZE; //For now, we'll just give one page for each stack.
	int idx = 0;
	array_add(as->as_regions, newregion, &idx);
	//(void)vaddr;
	return 0;
	
}

int
as_prepare_load(struct addrspace *as)
{
/*	KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);

	dumbvm_can_sleep();

	as->as_pbase1 = getppages(as->as_npages1);
	if (as->as_pbase1 == 0) {
		return ENOMEM;
	}

	as->as_pbase2 = getppages(as->as_npages2);
	if (as->as_pbase2 == 0) {
		return ENOMEM;
	}

	as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	if (as->as_stackpbase == 0) {
		return ENOMEM;
	}

	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);

	return 0;
*/
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	dumbvm_can_sleep();
	(void)as;
	return 0;
	/*
	 * Write this.
	 */
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	KASSERT(as->as_stackpbase != 0);
	/*
	 * We will leave this as-is for now, but will probably need to change it later!
	 *   We want to find out how to track the size of the stack, not just its start.
	 */

	//as_define_region(addrspace, USERSTACK, this won't work 'cuz the stack isn't static.

	//(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

