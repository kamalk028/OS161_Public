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
#include <bitmap.h>
#include <kern/fcntl.h>
#include <vfs.h>
#include <uio.h>
#include <fs.h>
#include <vnode.h>
#include <kern/stat.h>





/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

 #define DUMBVM_STACKPAGES    1024
 #define FREE_STATE 0
 #define FIXED_STATE 1
 #define DIRTY_STATE 2
 #define CLEAN_STATE 3
 #define DISK_FILE_NAME "lhd0raw:"


//Coremap objects.
//static struct lock *cm_lock = NULL;//Memory must be allocated for the lock as well!
static struct coremap *cm_entry = NULL;//Can initialze the coremap components later. //memsteal will later be used to make this an array.
static unsigned int kern_pages = 0;//Total number of physical pages the kernel has used before and during coremap initilization.
static unsigned int npages_used = 0;//Total number of coremap pages used by all processes.
static unsigned int total_npages = 0;//Total number of pages in the core map.
static struct spinlock cm_splk;

//Swap table objects.
static struct swap_table st[4096];
static bool is_disk_available;
static unsigned int clock = 0; //Used in clock-LRU algorithm for selecting a page to swap.
static struct lock *swap_table_lk = NULL;
static struct vnode *st_vnode = NULL;
static struct bitmap *st_bit_map = NULL;

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}
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
_getppages(unsigned long npages, bool is_kern, unsigned long pid)
{
	//NOTE: Think about read-write locks for faster implementations...
	//  Think about using splhigh() and setting volatile values...

	//Much to my surprise, this kassert didn't get triggered.
	if(CURCPU_EXISTS())
	{
		KASSERT(spinlock_do_i_hold(&cm_splk) == 0);
	}
	//as_prepare_load also calls this function, not just alloc_kpages.

	paddr_t addr = 0x0;//To be filled in by either swapout or coremap scan.

	if(CURCPU_EXISTS()) //&& !spinlock_do_i_hold(&cm_splk))
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
	if(i == total_npages)//Coremap is full.
	{
		/*if(CURCPU_EXISTS()) //&& spinlock_do_i_hold(&cm_splk))
		{
			spinlock_release(&cm_splk);
		}*/

		if(is_disk_available)
		{
			//This should not be necessary. If this gets thrown, swapout must be edited.
			KASSERT(npages == 1);

			addr = swapout(npages);
			if (addr == 0)//This will happen if the entire coremap has all FIXED pages.
			{
				spinlock_release(&cm_splk);
				return 0;
			}
			KASSERT((addr & PAGE_FRAME) == addr);
			//If it's a user page, set it back to dirty.
			/*if(CURCPU_EXISTS()) //&& spinlock_do_i_hold(&cm_splk))
			{
				spinlock_acquire(&cm_splk);
			}*/
			KASSERT(cm_entry[addr/PAGE_SIZE].pid != 1);
			cm_entry[addr/PAGE_SIZE].pid = pid;
			//kprintf("pid value in coremap: %u\n", curproc->pid);
			unsigned pg_status = FIXED_STATE;
			if(!is_kern)
			{
				pg_status = DIRTY_STATE;
			}
			cm_entry[addr/PAGE_SIZE].page_status = pg_status;
			cm_entry[addr/PAGE_SIZE].npages = npages;
			cm_entry[addr/PAGE_SIZE].ref = 0;
		}
		else
		{
			spinlock_release(&cm_splk);
			return 0;
		}
		/*if(err)
		{
			return 0;
		}*/
	}
	else//We found however many consecutive free pages we needed.
	{
		unsigned pg_status = FIXED_STATE;
		if(!is_kern)
		{
			pg_status = DIRTY_STATE;
		}

		i = i - (npages);//Change i to first open index.
		addr = i * PAGE_SIZE;
		while(cont_pages > 0)
		{
			KASSERT(cm_entry[i].pid != 1);
			cm_entry[i].page_status = pg_status;
			cm_entry[i].npages = npages;
			cm_entry[i].ref = 0;
			if(CURCPU_EXISTS())
			{
				cm_entry[i].pid = pid;
			}
			else
			{
				kern_pages++;
				cm_entry[i].pid = 1;
			}
			i++;
			cont_pages--;
		}
		npages_used+=npages;
	}

	as_zero_region(addr, npages);

	if(CURCPU_EXISTS())// && spinlock_do_i_hold(&cm_splk))
	{
		spinlock_release(&cm_splk);
	}

	return addr;
}

static
paddr_t
getppages(unsigned long npages, unsigned int pid)
{
	return _getppages(npages, true, pid);
}

//Same as getppages, but gives dirty state to pages.
//Called when mem is being allocated for users.
/*static
paddr_t
getupages(unsigned long npages)
{

	return _getppages(npages, false);
}*/
//Ultimately not used, since we can set page state to dirty later, and often need to.

static
void
st_create()
{
	//st[512] = { { NULL } };
	/*st->entries = array_create();
	if(st->entries == NULL)
	{
		kfree(st);
		return NULL;
	}*/
	swap_table_lk = lock_create("swap_table_lock");
	if(swap_table_lk == NULL)
	{
		panic("Swap table lock create failed!");
	}
	st_vnode = NULL;
}

/*
struct swap_table_entry* ste_create()
{
	//THIS IS THE PROBLEM! WE CANNOT KMALLOC SOMETHING DURING SWAPOUT!!
	struct swap_table_entry* ste = kmalloc(sizeof(struct swap_table_entry));
	if(ste == NULL)
	{
		return NULL;
	}
	return ste;
}
*/

/*
void ste_destroy(struct swap_table_entry* ste)
{
	KASSERT(ste != NULL);
	kfree(ste);
}
*/

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
		cm_entry[i].ref = false;
		//cm_entry[i].in_evict = 0;
		//Leave the pte field alone.
	}

	for (i = first_chunk; i < num_core_entries; i++)
	{
		cm_entry[i].page_status = FREE_STATE;
		cm_entry[i].npages = 0;
		cm_entry[i].pid = 0;//Default value; normally assigned curproc->pid once memory is fixed.
		cm_entry[i].ref = false;
		//cm_entry[i].in_evict = 0;
	}
	npages_used = first_chunk;
	kern_pages = first_chunk;

	//cm_lock = lock_create("coremap_lock"); // It is not safe to call kmalloc here. Memory was already allocated above.
	//The problem with the standard lock is that we also must allocate a wchan and name for it...
	spinlock_init(&cm_splk);

	return;
} 

//We started by copying dumbvm functions to get the kernel to boot.
//At this point, we've replaced almost everything.

void
vm_bootstrap(void)
{
	// Allocate memory for the swap table here...
	struct vnode *vn = NULL;
	char *tmp = kstrdup(DISK_FILE_NAME);
	int err = vfs_open(tmp, O_RDWR, 0664, &vn);
	if(err)
	{
		is_disk_available = false;
	}
	else
	{
		struct stat stat;
		VOP_STAT(vn, &stat);
		unsigned size = stat.st_size;
		unsigned npages = (size/PAGE_SIZE) + 1;
		if(size % PAGE_SIZE == 0)
		{
			npages--;
		}
		st_create();
		st_bit_map = bitmap_create(npages);
		is_disk_available = true;
		st_vnode = vn;

	}
	kfree(tmp);
}

unsigned int coremap_used_bytes()
{
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
	unsigned pid;
	if (CURCPU_EXISTS()) { pid = curproc->pid; }
	else { pid = 1; }

	dumbvm_can_sleep();
	pa = getppages(npages, pid); //This should return a physical address, and no longer just "steal" memory.
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa); //All this does is add 0x80000000 to pa.
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	//This function needs to remove a TLB entry from another CPU core.
	//It should only be called if the other process is running and one of its pages will get swapped out.
	(void)ts;
	//panic("dumbvm tried to do tlb shootdown?!\n");
}

int free_ppages(paddr_t p_addr)
{
	if(CURCPU_EXISTS()) { KASSERT(!spinlock_do_i_hold(&cm_splk)); }
	//dumbvm_can_sleep();//Sometimes, we are in an interrupt handler when we get here.
	unsigned int i = p_addr/PAGE_SIZE;// i is assumed to be the index of the first coremap entry used by the process.
	if(CURCPU_EXISTS()) //&& !spinlock_do_i_hold(&cm_splk))
	{
		spinlock_acquire(&cm_splk);
	}
	int chunk = cm_entry[i].npages;
	int temp_chunk = chunk;
	while(chunk > 0)
	{
		KASSERT(cm_entry[i].pid != 1);
		KASSERT(cm_entry[i].page_status != FREE_STATE);
		if (cm_entry[i].pid == 2000) //The page is in the middle of being swapped out.
		{				//No risk freeing a wrong page, thanks to pte->state checks.
			npages_used--;
			if(CURCPU_EXISTS()) { spinlock_release(&cm_splk); }
			return 1; //Signals to the caller (probably as_destroy) that the page is being swapped out.
			//At this point, we do not want to update the values in this cm_entry, 'cuz they'll get filled in for another process by swapout.
		}
		cm_entry[i].page_status = FREE_STATE;
		cm_entry[i].npages = 0;
		/*if(CURCPU_EXISTS())
		{
			//KASSERT(cm_entry[i].pid == curproc->pid);
		}*/
		cm_entry[i].pid = 0;
		cm_entry[i].ref = false;

		i++;
		chunk--;
	}
	npages_used-=temp_chunk;
	if(CURCPU_EXISTS()) //&& spinlock_do_i_hold(&cm_splk))
	{
		spinlock_release(&cm_splk);
	}
	return 0;
}

void
free_kpages(vaddr_t addr)
{
	paddr_t p_addr = addr;
	KASSERT(addr-1 > MIPS_KSEG0);
	if(addr-1 > MIPS_KSEG0)//What if addr = 0x80000000?
	{
		p_addr = addr - MIPS_KSEG0;
	}
	int free_ret = 0;
	free_ret = free_ppages(p_addr);
	if (free_ret)
	{
		panic("A kernel page at paddr %x was swapped out!!\n", addr);
	}
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	KASSERT(spinlock_do_i_hold(&cm_splk) == 0);
	/*bool cm_lk_held = spinlock_do_i_hold(&cm_splk);
	if(cm_lk_held)
	{
		spinlock_release(&cm_splk);
	}*/
	//To my surprise, this kassert was not triggerred.
	//KASSERT(spinlock_do_i_hold(&cm_splk) == 0);

	paddr_t paddr = 0; //Used only as a value for getppages to fill.
	unsigned int i = 0;
	uint32_t ehi = 0, elo = 0;
	struct addrspace *as = NULL;
	struct as_region *as_region = NULL;
	int spl = 0;
	bool valid_addr = 0;
	int err = 0; //Error code for pt_lookup.
	uint32_t vpn = 0;
	uint32_t ppn = 0; //Used for page tabe lookup.

	faultaddress &= PAGE_FRAME; //This effectively chops off 12 bits of faultaddress.

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
		//For now, we will ignore this. 3.3: These might matter now.
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
		//KASSERT(r->size != 0); //This KASSERT rejects our heap.
		KASSERT((r->start & PAGE_FRAME) == r->start);
		KASSERT((r->end & PAGE_FRAME) == r->end);
		if(faultaddress >= r->start && faultaddress < r->end)
		{
			valid_addr = 1;
			as_region = r; 
			break;
		}
	}

	//I made the stack a full-fledged as_region, so this isn't needed anymore.
	/*stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK; // CHANGE THIS IF YOU CHANGE STACK ALLOCATION!

	if (faultaddress >= stackbase && faultaddress < stacktop) {
		//paddr = (faultaddress - stackbase) + as->as_stackpbase;
		valid_addr = 1;
	}
	*/

	if(!valid_addr)
	{
		return EFAULT;
	}

	//Debugging:
	//uint32_t curpid = curproc->pid;
	struct page_table_entry *dummy = NULL;


	vpn = faultaddress & PAGE_FRAME; //Chops off last twelve bits of faultaddress. (2^32 - 2^12 - 1)
	err = pt_lookup(as->pt, vpn, as_region->permission, &ppn, &dummy);

	if(err == 1)
	{
		int swp_err = swapin(vpn, &ppn, curproc->pid);
		if(swp_err)
		{
			panic("Swap in failed ");
			//return ENOMEM;
		}
	}
	else if (err)//If no pte was found, allocate some physical memory.
	{
		paddr = getppages(1, curproc->pid);//Changed to getppages so it doesn't get swapped out before its pte is created...
		//ppn = copy_fa - MIPS_KSEG0;
		ppn = paddr & PAGE_FRAME;
		struct page_table_entry *pte = pte_create(vpn, ppn, as_region->permission, 1, 1, 0);
		pt_append(as->pt, pte);
		cm_entry[paddr/PAGE_SIZE].page_status = DIRTY_STATE;
	}
	else//If a pte was found, make sure it indeed owns the physical page it says it owns. (It's probably okay if the cm_pid == 2000)
	{
		//3.3: Also need to consider pages on disk here!
		//pte = array_get(as->pt->pt_array, idx);
		spinlock_acquire(&cm_splk);
		//KASSERT(curproc->p_addrspace == as);//This check might slow the system immensely. USE IT FOR TESTS ONLY.
		if((curproc->pid != cm_entry[ppn/PAGE_SIZE].pid) && (cm_entry[ppn/PAGE_SIZE].pid != 2000))
		{
			panic("cm_entry pid's are wrong! In map: %u, Curproc: %u", cm_entry[ppn/PAGE_SIZE].pid, curproc->pid);
		}
		cm_entry[ppn/PAGE_SIZE].ref = true;
		spinlock_release(&cm_splk);
	}

	paddr = ppn; //We didn't want to change TLB-related code too much.

	//We still disable interrupts here, too.
	//Update the tlb. There is a tlb_probe function, I'm not sure why they don't just call that.
	spl = splhigh();
	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue; //as_activate uses tlb_write to fill all entries with INVALID.
				  //  So, after a context switch, INVALID means AVAILABLE.
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID; //The 22nd and 23rd bits of TLB entries track dirty and valid.
		// DEBUG(DB_VM, "Not-so-dumb-vm before tlb_write: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	//At this point, the TLB is full.
	//kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");

	//We will use random entry replacement for now.
	ehi = faultaddress;
	elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
	// DEBUG(DB_VM, "Not-so-dumb-vm before tlb_random: 0x%x -> 0x%x\n", faultaddress, paddr);
	tlb_random(ehi, elo); //Overwrites a random TLB entry.
	splx(spl);
	(void)dummy;
	//(void)curpid;
	//(void)pte;
	/*if(cm_lk_held)
	{
		spinlock_acquire(&cm_splk);
	}*/
	return 0;
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
	if(as->as_regions == NULL)
	{
		kfree(as);
		return NULL;
	}
	array_init(as->as_regions);
	//as->next_start = 0x00000000; //We were originally going to disallow the passing of vaddr into define_region().
	//as->stack_start = USERSTACK; //Default 0x7fffffff, I think.
	as->pt = pt_create(); //This effectively initializes an array.
	if(as->pt == NULL)
	{
		array_destroy(as->as_regions);
		kfree(as);
		return NULL;
	}
	//May add heap declaration here, as well.

	return as;
}


int
as_copy(struct addrspace *old, struct addrspace **ret, unsigned int newpid)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	/*
	 * Our new code:
	 */

	struct as_region *r, *newr;
	struct page_table_entry *pte, *newpte;//We will copy all pte's as well, except the new as will get different ppn's.
	unsigned int num_regions = array_num(old->as_regions);
	//unsigned int num_pte = array_num(old->pt->pt_array);
	unsigned int i;
	bool read = 0, write = 0, exec = 0;
	paddr_t newppn;
	int err = 0;

	//Start by copying and allocating mem for each region.
	for (i = 0; i < num_regions; i++)
	{
		read = write = exec = 0;
		r = array_get(old->as_regions, i);
		KASSERT(r->start != 0); //dumbvm did this too.

		//Set permission variables.
		if (r->permission >= 4) {read = 1;}
		if (r->permission >= 6 || r->permission == 2 || r->permission == 3) {write = 1;}
		if (r->permission % 2 == 1) {exec = 1;}
		//It could be more efficient to define regions manually, if we must.
		err = as_define_region(newas, r->start, (r->size * PAGE_SIZE), read, write, exec);
		if (err)
		{
			kprintf("as_define_region failed, causing Pid %u to destroy an addrspace.\n", curproc->pid);
			as_destroy(newas);//kfree(*ret);
			(void)*ret;
			return err;
		}
		//New: Try to copy the actual contents of memory.
		newr = array_get(old->as_regions, i);
	}
	//Now copy all the pte's, except allocte new physical pages for each entry.
	//lock_acquire(old->pt->paget_lock); //Nothing but as_destroy can change number of entries.
	unsigned int num_pte = array_num(old->pt->pt_array);
	for (i = 0; i < num_pte; i++)
	{
		//DEADLOCK ISSUE! Do not try holding the paget_lock while calling getppages.
		newppn = getppages(1, newpid);//These pages cannot be swapped out yet since they get marked as fixed.
		if(newppn == 0)
		{
			//kprintf("getppages failed during as_copy from pid %u.\n", curproc->pid);
			as_destroy(newas);//kfree(*ret);
			return ENOMEM;
		}
		//Now we re-adjust that pid value in the coremap. 
		//TODO: If swapping takes place just before this re-assignment, mass confusion WILL ensue. 
		//Rmv locks from getupages.
		spinlock_acquire(&cm_splk);
		KASSERT(cm_entry[newppn/PAGE_SIZE].pid != 1);
		//cm_entry[newppn/PAGE_SIZE].pid = newpid;
		spinlock_release(&cm_splk);

		lock_acquire(old->pt->paget_lock);
		pte = array_get(old->pt->pt_array, i);
		//Originally, we accidentally made the child's pte->state equal the parent's, even though it should always be 1 here.
		newpte = pte_create(pte->vpn, newppn, pte->permission, 1, pte->valid, 0);
		if(newpte == NULL)
		{
			lock_release(old->pt->paget_lock);
			int free_ret = free_ppages(newppn);
			if (free_ret)
			{
				panic("We tried swapping out a page that didn't have a pte ready yet!!");
			}
			kprintf("PTE creation failed during as_create from pid %u.\n", curproc->pid);
			as_destroy(newas);//kfree(*ret);
			return ENOMEM;
		}
		if(pte->state == 0)
		{
			lock_release(old->pt->paget_lock);
			//Only possibility for data race is if this page is gonna get swapped in. Which I don't think is possible here.
			lock_acquire(swap_table_lk);
			int i = 0, n = 512;

			//struct swap_table ste = NULL;
			unsigned pid = curproc->pid;

			for (i=0; i<n; i++)
			{
				//ste = st[i];
				//ste = array_get(st->entries, i);
				if (st[i].pid == pid && st[i].vpn == pte->vpn)
				{
					unsigned disc_idx = st[i].disc_idx;
					err = block_read(newppn, disc_idx);
					if(err)
					{
						panic("In func: as_copy :::: block_read returned err");
					}
					break;
				}
			}
			lock_release(swap_table_lk);
		}
		else if(pte->state == 1)
		{
			//New: 3rd attempt: Copy the actual contents of memory in the physical pages.
			//TODO: Maybe put a lock on these? Copying memory while it's being wirtten would break stuff...
			memmove((void *)PADDR_TO_KVADDR(newpte->ppn),
			(const void *)PADDR_TO_KVADDR(pte->ppn),
			PAGE_SIZE);
			lock_release(old->pt->paget_lock);
		}
		pt_append(newas->pt, newpte);

		spinlock_acquire(&cm_splk);
		cm_entry[newppn/PAGE_SIZE].page_status = DIRTY_STATE;//Should be no need to get lock here, since a fixed page won't get swapped out.
		KASSERT(cm_entry[newppn/PAGE_SIZE].pid != 1);
		spinlock_release(&cm_splk);

		num_pte = array_num(old->pt->pt_array);
		//lock_release(old->pt->paget_lock);
	}
	//lock_release(old->pt->paget_lock);

	//(void)old;
	//(void)*ret;
	/*(void)*pte;
	(void)*newpte;
	(void)num_pte;
	(void)newppn;*/
	(void)newr;
	*ret = newas;
	return 0;
}


void
as_destroy(struct addrspace *as)
{
	//Remove this if it gets thrown. This should not be mandatory.
	//KASSERT(as == curproc->p_addrspace);
	//kprintf("as_destroy called on addrspace of pid %u.\n", curproc->pid);
	//This function originally did almost nothing, and probably leaked a lot of each process' memory.
	dumbvm_can_sleep();
	struct as_region *r;
	struct page_table_entry *pte;
	//Free all memory regions before freeing the address space.
	while(array_num(as->as_regions))
	{
		r = array_get(as->as_regions, 0);
		kfree(r);//This will only free the attributes of each region, NOT their process' memory.
		array_remove(as->as_regions, 0);//Removing the first shifts the rest down.
	}
	array_destroy(as->as_regions);
	//All physical pages held by the address space must be set to free! Scan the page table!
	lock_acquire(as->pt->paget_lock);
	while(array_num(as->pt->pt_array))
	{
		pte = array_get(as->pt->pt_array, 0);
		//acquire page table lock here!
		if(pte->state)
		{
			int err = 0;
			err = free_ppages(pte->ppn);
			if (err)//If free_ppages finds a page being swapped out, it should return an error code, and remove_pageonisk should be called.
			{
				lock_release(as->pt->paget_lock);
				while(pte->state) { ; /*Do nothing but wait for the swap out to complete.*/ }
				remove_pageondisk(pte->vpn);//This function will try acquiring the st_lock, making it wait until swapout is done.
				lock_acquire(as->pt->paget_lock);
			}
			kfree(pte);
			array_remove(as->pt->pt_array, 0);
		}
		else
		{
			remove_pageondisk(pte->vpn);
			kfree(pte);
			array_remove(as->pt->pt_array, 0);
		}
	}
	lock_release(as->pt->paget_lock);
	array_destroy(as->pt->pt_array);
	lock_destroy(as->pt->paget_lock);
	kfree(as->pt);
	kfree(as);
}

void
as_activate(void)
{
	//All this does is invalidate a previous proc's TLB entries.
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
	//I'll make this clear the TLB just in case.
	int i, spl;
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);

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
		 bool readable, bool writeable, bool executable)
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

	dumbvm_can_sleep();
	//Create a new region called newregion.
	struct as_region *newregion;
	//NEW DESIGN DECISION: Make the passed in vaddr page-aligned.
	uint32_t offset = vaddr % PAGE_SIZE;
	vaddr -= offset;
	//Allocate memory just for the attributes of as_region, NOT the memory it will request.
	newregion = kmalloc(sizeof(struct as_region));
	if(newregion == NULL)
	{
		return ENOMEM;
	}
	unsigned int i = 0;
	unsigned int page_num = 0;
	vaddr_t page_start = vaddr; //Used for verifying that each requested virtual page doesn't belong to another region.

	newregion->start = vaddr;
	newregion->size = (memsize / PAGE_SIZE) + 1; //If memsize is page-aligned, then remove the +1.
	if ((memsize % PAGE_SIZE) == 0) {newregion->size--;}

	for (i = 0; i < array_num(as->as_regions); i++)
	{
		//Make sure another region doesn't own any requested pages.
		struct as_region *r = array_get(as->as_regions, i);
		for (page_num = 0; page_num < newregion->size; page_num++);
		{
			if(page_start >= r->start && page_start < r->end)
			{
				kfree(newregion);
				return ENOSYS;
				//We need to do a kfree here. Hey, WE MIGHT'VE MADE OTHER MEMLEAKS THIS WAY!!
			}
			page_start += PAGE_SIZE;
		}
		page_start = newregion->start;
	}
	//Make sure we actually have enough memory for this new region. (Check that we haven't collided with the stack.)
	//  Eventually, we may want to check for a collision with the heap instead.
	/*if (((newregion->size * PAGE_SIZE) + as->next_start) > (as->stack_start - PAGE_SIZE))
	{
		kprintf("Warning: Way too much memory allocated in one address space!");
		return ENOSYS;
	}*/
	newregion->end = newregion->start + (newregion->size * PAGE_SIZE);
	//Update the start address for the next region. From our old plan of ignoring vaddr.
	//as->next_start += (newregion->size * PAGE_SIZE);

	//Assign permission. Will be stored in page table entry later.
	newregion->permission = 0; //Default value. Gets increased if any permission flags are set.
	if (executable) {newregion->permission++;}
	if (writeable) {newregion->permission+=2;}
	if (readable) {newregion->permission+=4;}

	//Expand the address space's stack to give this region a stack of its own.
	//  THAT WAS THE OlD PLAN! Now, we're keeping the stack how it was in dumbvm.
	//newregion->stack = as->stack_start;
	//as->stack_start -= PAGE_SIZE; //For now, we'll just give one page for each stack.

	//Add this new region to the address space's array.
	unsigned int idx = 0;
	array_add(as->as_regions, newregion, &idx);
	//(void)vaddr;
	dumbvm_can_sleep();
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
*/
//	as_zero_region(as->as_pbase1, as->as_npages1);
//	as_zero_region(as->as_pbase2, as->as_npages2);
//	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);

//	return 0;

	/*
	 * It's probably fine that this does nothing.
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
	 * It's probably fine that this doesn't do anything anymore.
	 * We don't want any memory statically allocated anymore.
	 */
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	int err = 0, heaperr = 0;
	struct as_region *r = NULL;
	//KASSERT(as->as_stackpbase != 0);//Need to keep part of as_prepre_load to keep this working...

	//We'll give the stack a statically-sized region AT FIRST. Give it read-write permission.
	//  Notice that the address passed in is the BOTTOM of the stack!
	err = as_define_region(as, (USERSTACK - (DUMBVM_STACKPAGES * PAGE_SIZE)), (DUMBVM_STACKPAGES * PAGE_SIZE), 1, 1, 0);
	if (err) { return err; }
	r = array_get(as->as_regions, array_num(as->as_regions)-1);

	//I'm also gonna declare the heap here, since this is also a region every process needs.
	//The heap starts at address USERSTACK/2 with 0 pages allocated for it. (Default: 0x40000000)
	//The sbrk call will grow it by as many pages as the user requests.
	heaperr = as_define_region(as, (USERSTACK / 2), 0, 1, 1, 0);
	if (heaperr)
	{
		kfree(r);
		return heaperr;
	}


	//(void)as;
	//(void)heaperr;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK; //Top of the stack.
	// *heap_break = USERSTACK / 2; //Top of the heap, can be moved with sbrk().

	return 0;
}

static
paddr_t
pick_page(unsigned int *pid)
{
	unsigned limit = total_npages * 5;
	unsigned ctr = 0;
	//spinlock_acquire(&cm_splk);

	//May not want to try picking a page from a proc that's getting destroyed, since freepageondisk
	while(1)//If every single page is fixed, infinite loop. Don't think that's possible.
	{
		clock++;
		ctr++;
		if (clock == total_npages) { clock = 0; }
		if (ctr == limit)
		{
			return 0;
		}

		if(cm_entry[clock].page_status == FIXED_STATE || cm_entry[clock].page_status == FREE_STATE)
		{
			continue;
		}
		if(cm_entry[clock].ref == 1)
		{
			cm_entry[clock].ref = 0;
		}
		else
		{
			*pid = cm_entry[clock].pid;
			//kprintf("Pid retrieved from pick_page: %u\n", *pid);
			cm_entry[clock].page_status = FIXED_STATE;
			//spinlock_release(&cm_splk);
			if (cm_entry[clock].pid == 1) { panic("Attempting to swap out kernel page!!"); }
			if (cm_entry[clock].pid == 2000) { panic("Attempting to swap out same page twice!!"); }
			cm_entry[clock].pid = 2000; //This indicates that the page is being swapped out.
			return (clock * PAGE_SIZE);
		}
		//ctr++;
	}
	/*if(ctr == limit)
	{
		panic("pick_page does not stop ::: ");
	}*/
	//spinlock_release(&cm_splk);
	panic("How did our code hit the bottom of pick_page?!");
	return 0;
}

//Swap a page out to disk. Figures out what to swap on its own.
//We are holding the coremap spinlock when we reach this function.
paddr_t swapout(int npages)
{
	//Check the value of is_disk_available. Return ENOMEM if not.
	/*if(!is_disk_available)
	{
		return ENOMEM;
	}*/

	unsigned s_pid;
	struct proc *s_proc = NULL;
	paddr_t ppn = 0;
	    /*
        * pick_page holds cm_splk throughout the operation.
        * page_status of the picked_page is set as FIXED_STATE
        * Remember to change the state back to dirty when you make all the necessary changes
        */

	while((s_proc == NULL) || (s_proc->p_addrspace == NULL))
	{
		if (!spinlock_do_i_hold(&cm_splk)) { spinlock_acquire(&cm_splk); }
       		ppn = pick_page(&s_pid);
		if (ppn == 0)
		{
			return 0;
		}

		//get_proc requires getting a proc_table lock breifly. We could actually be holding that lock now, from proc_fork_runprogram.
		//If we are holding the pt_lock, it's safe to let it go by now. We only needed it so getppages would get the right pid.
		KASSERT(spinlock_do_i_hold(&cm_splk));
		spinlock_release(&cm_splk);//Safe to release here, since our pp wont get swapped out or freed now.
       		s_proc = get_proc(s_pid);
		//lock_acquire(s_proc->p_addrspace->pt->paget_lock);
		KASSERT(s_proc != NULL);
		//If the proc's address space is NULL, we'll now just pick another page. If that happens...
		//Either an entire as_destroy completes on the proces which owns the page we chose, we forgot a coremap spinlock somewhere, or a page is chosen for an uninitialized proc.
		//kprintf("Pid %u is gonna swap out page at %x, owned by %u.\n", curproc->pid, ppn, s_pid);
		//if (s_proc->p_addrspace == NULL) { panic("Address space of pid %u is NULL during swapout!", s_pid); }
		//I thought wrong pid values were getting in the coremap, but apparently not. It is an uninitialized addrspace from as_copy getting selected.
	}
	KASSERT(s_proc->p_addrspace->pt != NULL);
       struct page_table *pt = s_proc->p_addrspace->pt;

       /* function below changes the returned pte->state to 0
       */
       struct page_table_entry *pte = NULL;
	//lock_acquire(pt->paget_lock);
	lock_acquire(swap_table_lk);
       int err = pt_plookup(pt, ppn, &pte);
	KASSERT(pte->state == 0);//Have the swap table lock acquired before this.
				//We can make as_destroy wait until the state is changed to try acquiring the swap_table lock.
				//DEADLOCK ISSUE: Make sure that, if you do this, NOTHING tries acquiring the st_lock while holding a pt_lock!!
       //tlbshootdown code should go here.
	as_activate();

       KASSERT(err == 0);

       //Create a swap_entry
       //struct swap_table_entry* ste = ste_create();
       unsigned int st_idx = 0;
	while(st[st_idx].pid != 0)
	{
		st_idx++;
		//if (st_idx == 8192) { panic("We ran out of swap table entries!"); }
	}
       //lock_acquire(swap_table_lk);
       unsigned disc_idx = 0;
       bitmap_alloc(st_bit_map, &disc_idx);

       //call block write.
	//spinlock_release(&cm_splk);
       err = block_write(ppn, disc_idx);//APPARENTLY a swapin can occur during this.
       //err = block_write(pte->vpn, disc_idx);
       if(err)
       {
               //lock_release(pt->paget_lock);
		//lock_release(swap_table_lk);
		panic("block_write failed in swapout!");
               return -1;
       }
       pte->ppn = 0;

	//spinlock_acquire(&cm_splk);
       st[st_idx].pid = s_pid;
       st[st_idx].vpn = pte->vpn;
       st[st_idx].disc_idx = disc_idx;
       st[st_idx].pte = pte;
	lock_release(swap_table_lk);

       //unsigned dummy = 0;

       //array_add(st->entries, ste, &dummy);

       //On success, assign values to the ste
       //add the ste to the swap_table
       //lock_release(swap_table_lk);
	//lock_release(pt->paget_lock);


       //change the cm_entry page_status to DIRTY_STATE, if it's a user page.
	//getppages can tell if it's a user page or kernel page, so do it there.
       //cm_entry[ppn/PAGE_SIZE].page_status = DIRTY_STATE;

	//Get the coremap spinlock back before returning to getppages.
	spinlock_acquire(&cm_splk);
       (void)npages;
       return ppn;
	

	//Acquire the coremap lock and the swap table lock.
	//Using an algorithm, figure out which page should be swapped out.
	//(ISSUE: SWAPPING ALGO. Would take ages to check every pte
	//  inside EVERY page table! Maybe rely on coremap instead?
	//  Also, check for clean pages in the coremap. No data needs to be copied for that.
	//Will now want to acquire a page table lock. ALWAYS ACQUIRE IN THAT ORDER!!
	//Call bitmap_alloc to get a spot on the disk (reminder: it is an st object).
	//Pass that index, and the vpn, into block_write.
	//Use pt_lookup to see the old physical address.
	//Possibly update the TLB to remove an entry (or all entries).
	//ISSUE: Communicating with another CPU's TLB. Idk how.
	//  Should we just call as_activate or something?
	//  Can we find a proc's TLB once we've found the proc itself?
	//Update the pte to indicate the page is on disk, and delete its ppn.
	//If copy-on-write is implemented, and multiple proc's own a page,
	//  carefully change the pte for each proc involved!
	//Release paget_lock.
	//Update the coremap to free the page. POSSIBLY fill in a new page.
	//Release the coremap lock (done automatically by getppages).
	//Update the swap table with the new vpn-diskblock pair.
	//Release the lock on the swap table.
	//Done? Idunno, permissions might also need to be considered.
}

//To be used by as_destroy only.
int remove_pageondisk(vaddr_t vpn)
{
	//At this point, the page is either not on the coremap, or it's still there but is gonna get swapped out.
	lock_acquire(swap_table_lk);

	//locate the swap_table_entry using pid and vpn
	//WARNING! What if as_destroy is called with a non-curent proc?! (might never happen, I've KASSERTed it.)
	unsigned pid = curproc->pid;

	//lock_acquire(swap_table_lk);
	int i = 0, n = 512;

	//struct swap_table ste = NULL;
	struct page_table_entry *pte = NULL;

	bool temp = false;

	//kprintf("Deleting page on disk: pid %u, vaddr %x.\n", pid, vpn);
	for (i=0; i<n; i++)
	{
		//ste = st[i];
		if (st[i].pid == pid && st[i].vpn == vpn)
		{
			temp = true;
			//swap_table_entry found
			unsigned disc_idx = st[i].disc_idx;
			bitmap_unmark(st_bit_map, disc_idx);
			pte = st[i].pte;
			pte->valid = 0;
			st[i].pid = 0;
			st[i].vpn = 0;
			st[i].disc_idx = 0;
			st[i].pte = NULL;
			//array_remove(st->entries, i);
			break;
		}
	}

	lock_release(swap_table_lk);

	if(!temp)
	{
		panic("::: We dont have a swap_table_entry for this vpn ::: ");
	}

	return 0;
}

//Called if a TLB fault occus on a page that's on disk.
int swapin(vaddr_t vpn, paddr_t *paddr, unsigned int pid)
{
	//PTLOOKUP CHANGED!! 3.3: AFFECTS THIS, VM_FAULT, and SBRK.
	int err = 0;
	//First, we might need to call swapout(). swapout may get called by getppages.

	*paddr = getppages(1, pid);//Page temporarily labelled as FIXED.
	if (*paddr == 0)
	{
		panic("We're SOL, pid %u needs a swapin and the whole coremap is fixed!\n", pid);
	}

	//Acquire the locks in the same order: swap table, page table, coremap.
	//  This holds true for all functions that must hold more than one at once.

	//Use the swap table to find the data we need.
	//lock_acquire(swap_table_lk);
	int i = 0, n = 512;

	//struct swap_table ste = NULL;
	struct page_table_entry *pte = NULL;

	//Update the pte for the proc that owns this page.
	struct proc *p = NULL;
	p = get_proc(pid);
	KASSERT(p != NULL);
	//spinlock_acquire(&cm_splk);//SYNCH ISSUE: Will need to acquire paget_lock before this.
	err = pt_lookup(p->p_addrspace->pt, vpn, 0, paddr, &pte);
	if(err == 0 || err == -1)
	{
		panic("Page table out of synch while swapping in!");
	}
	KASSERT(pte != NULL);
	while(pte->state == 1)//A swapout is finishing. 
	{
		 ; //Do nothing. It will eventually change.
	}

	lock_acquire(swap_table_lk);
	err = 0;
	for (i=0; i<n; i++)
	{
		//ste = st[i];
		if (st[i].pid == pid && st[i].vpn == vpn)
		{
			//Call block read to retrieve the data from disk.
			//Assumption: calling block_read with the ppn fills that physical page.
			err = block_read(*paddr, st[i].disc_idx);
			if (err)
			{
				panic("block_read failed in swapin for some reason.");
			}
			//TODO: Right now we will remove a swap table entry. Change that later.
			bitmap_unmark(st_bit_map, st[i].disc_idx);
			//ste_destroy(ste);
			st[i].pid = 0;
			st[i].vpn = 0;
			st[i].disc_idx = 0;
			st[i].pte = NULL;
			//array_remove(st->entries, i);
			lock_release(swap_table_lk);

			lock_acquire(p->p_addrspace->pt->paget_lock);
			pte->ppn = *paddr;
			pte->state = 1; //Mark it as being in memory.
			lock_release(p->p_addrspace->pt->paget_lock);

			//kprintf("Swapped page into %x, owned by %u. Done by %u.\n", pte->ppn, pid, curproc->pid);

			//STUPID DEBUG: Make sure modifying pte returned by ptlookup also modifies the original.
			/*unsigned int n = array_num(p->p_addrspace->pt->pt_array);
			unsigned int idx = 0;
			struct page_table_entry *pte2 = NULL;
			for (idx = 0; idx < n; idx++)
			{
				pte2 = array_get(p->p_addrspace->pt->pt_array, idx);
				if (pte2->vpn == pte->vpn) {break;}
			}

			pte->ppn = *paddr;
			pte->state = 1; //Mark it as being in memory.
			KASSERT(pte2->state == 1);*/

			//What if a process swaps this page back out?
			//lock_release(p->p_addrspace->pt->paget_lock);

			//Mark the coremap page as owned by this proc and DIRTY_STATE..
			//  TODO: Mark page as CLEAN once you change swapout to support that.
			spinlock_acquire(&cm_splk);
			cm_entry[*paddr/PAGE_SIZE].page_status = DIRTY_STATE;
			KASSERT(cm_entry[*paddr/PAGE_SIZE].pid != 1);
			cm_entry[*paddr/PAGE_SIZE].ref = 1;
			spinlock_release(&cm_splk);
			KASSERT(pte->state == 1);//Checking for data race.
			break;
		}
	}
	if (i == n)
	{
		lock_release(swap_table_lk);
		panic("Swapin couldn't find its page!");
		return -1;
	}
	return 0;
}

//Copy a page of memory to disk. Called by swapout().
//int block_write(paddr_t ppn, unsigned disk_idx)
int block_write(paddr_t ppn, unsigned disk_idx)
{
	ppn = ppn+MIPS_KSEG0;
	int err = 0;
	struct iovec iov;
	struct uio uio;
	uio_kinit(&iov, &uio, (void*)ppn, PAGE_SIZE, disk_idx*PAGE_SIZE, UIO_WRITE);
	err = VOP_WRITE(st_vnode, &uio);
	if(err)
	{
		return err;
	}
	return 0;
}

//Copy a page of memory back from disk to memory.
int block_read(paddr_t ppn, unsigned disk_idx)
{
	ppn = ppn+MIPS_KSEG0;
	int err = 0;
	struct iovec iov;
	struct uio uio;
	uio_kinit(&iov, &uio, (void*)ppn, PAGE_SIZE, disk_idx*PAGE_SIZE, UIO_READ);
	err = VOP_READ(st_vnode, &uio);
	if(err)
	{
		return err;
	}
	return 0;
}




