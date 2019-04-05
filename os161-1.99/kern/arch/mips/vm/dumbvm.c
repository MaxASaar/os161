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
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include "opt-A3.h"
#include <syscall.h>
/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground.
 */

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12

/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

#if OPT_A3
static struct coremap *cmap;
static struct lock * coremap_lock;
static bool coremap_is_initialized = false;

struct coremap * coremap_init(void){
	coremap_lock = lock_create("coremap lock");
	paddr_t start_of_ram;
	paddr_t end_of_ram;
	// Get the size of ram
	ram_getsize(&start_of_ram, &end_of_ram);

	unsigned int total_memory = (unsigned int) end_of_ram - start_of_ram;
	unsigned int number_of_frames = total_memory / PAGE_SIZE;
		
	
	// Allocate space for the coremap itself
	// We need to use the first frame of memory to store the coremap
	// But what if the coremap is too big for the first frame?
	// We need to add the size of the coremap and the size of all of the cores
	// To figure out how big of a chunk we need to adjust for at the start of ram
	
	// We know the number of frames, so we can calculate the size of the array
	
	unsigned int size_of_core_array = sizeof(struct core) * number_of_frames;
	//unsigned int size_of_core_array_pointers = sizeof(struct core*) * number_of_frames;
	unsigned int size_of_coremap = sizeof(struct coremap);
	
	// Determine how many less frames we have due to adding these structures
	// at the start of ram 
	unsigned int total_coremap_size = size_of_core_array +
					//size_of_core_array_pointers +
					size_of_coremap;
	//kprintf("TOTAL COREMAP SIZE: %d\n", total_coremap_size);
	//kprintf("NUMBER OF FRAMES IN RAM: %d\n", number_of_frames);
	//kprintf("A FRAME IS THIS BIG: %d\n", PAGE_SIZE);
	int number_of_frames_less = total_coremap_size / PAGE_SIZE + 1; //sloppy round up
	//kprintf("We need to take away %d frames\n", number_of_frames_less);
	
	paddr_t this_coremap_paddr = start_of_ram;
	paddr_t this_core_array = start_of_ram + size_of_coremap;
	//paddr_t this_core_array_data = this_core_array + size_of_core_array_pointers;

	vaddr_t coremap_vaddr = PADDR_TO_KVADDR(this_coremap_paddr);
	vaddr_t core_array_vaddr = PADDR_TO_KVADDR(this_core_array);
	//vaddr_t core_array_data_vaddr = PADDR_TO_KVADDR(this_core_array_data);
	// Populate the coremap with all of the data
	struct coremap * this_coremap = (struct coremap *) coremap_vaddr;
	this_coremap->coremap_size = number_of_frames - number_of_frames_less;
	this_coremap->cores = (struct core *)core_array_vaddr;
	
	// Loop through the array of cores, initializing all of them
	for(unsigned int i = 0; i < this_coremap->coremap_size; i++){
		this_coremap->cores[i].physical_address = start_of_ram
					+ (PAGE_SIZE * number_of_frames_less)
					+ (PAGE_SIZE * i);
		this_coremap->cores[i].is_being_used = false;
	}	
	return this_coremap;
}
#endif

void
vm_bootstrap(void)
{
	#if OPT_A3
	// Initialize the coremap
	cmap = coremap_init();	
	coremap_is_initialized = true;
	#else
	/* Do nothing. */
	#endif
}

static
paddr_t
getppages(unsigned long npages)
{
	#if OPT_A3
	if(!coremap_is_initialized){
		paddr_t addr;
		spinlock_acquire(&stealmem_lock);
		//kprintf("STEALING SOM RAM PAGES: %d\n", (int) npages);
		addr = ram_stealmem(npages);
		//kprintf("Stolen address = %d\n", (int) addr);
		spinlock_release(&stealmem_lock);
		return addr;
	}
	
	lock_acquire(coremap_lock);
	for(unsigned int i = 0; i < cmap->coremap_size; i++){
		// Loop through all of the cores until an unowned one is found
		if(!cmap->cores[i].is_being_used){
			// Check if there are n pages in a row
			int count_in_row = 1;
			bool not_long_enough = false;
			for(unsigned int j = i + 1; j < i + npages && j < cmap->coremap_size; j++){
				if(cmap->cores[i].is_being_used){
					not_long_enough = true;
				}
				count_in_row++;
			}
			if(not_long_enough){
				//i += count_in_row - 1;
				continue;
			}
			cmap->cores[i].size = npages;
			// Loop through all of the pages and set them to used
			for(unsigned int j = i; j < i + npages; j++){
				cmap->cores[j].is_being_used = true;
			}
			lock_release(coremap_lock);
			return cmap->cores[i].physical_address;
		}
	}
	lock_release(coremap_lock);
	return 0;
	#else
	paddr_t addr;

	spinlock_acquire(&stealmem_lock);

	addr = ram_stealmem(npages);
	
	spinlock_release(&stealmem_lock);
	return addr;
	#endif
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{
	paddr_t pa;
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void 
free_kpages(vaddr_t addr)
{
	/* nothing - leak the memory. */

	(void)addr;
}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;
	#if OPT_A3
	bool is_text_segment = false;
	#endif
	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		#if OPT_A3
		// We need to kill the current process (use sys exit)
		sys__exit(1);
		
		break;
		#else
		/* We always create pages read-write, so we can't get this */
		panic("dumbvm: got VM_FAULT_READONLY\n");
		#endif
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
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
		#if OPT_A3
		// We know we are in the text segment
		is_text_segment = true;
		#endif
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

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		#if OPT_A3
		if(as->as_blocktextwrite && is_text_segment){ 
			elo &= ~TLBLO_DIRTY;
		}
		#endif
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}
	#if OPT_A3
	// In the case that we couldnt find the entry in the tlb
	// Use tlb_random to write the entry and return 0 instead of EFAULT
	// kprintf("TLB is full, writing to random TLB index");
	ehi = faultaddress;
	elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
	if(as->as_blocktextwrite && is_text_segment){ 
		elo &= ~TLBLO_DIRTY;
	}
	tlb_random(ehi, elo);
	// kprintf("Wrote to random TLB index");
	splx(spl);
	return 0;	
	#else
	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
	#endif
}

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;
	
	#if OPT_A3
	as->as_blocktextwrite = false;
	#endif
	return as;
}

void
as_destroy(struct addrspace *as)
{
	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;
	as = curproc_getas();
#ifdef UW
        /* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages; 

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;
	
	/* We don't use these - all pages are read-write */
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

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
	KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);

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
}

int
as_complete_load(struct addrspace *as)
{
	#if OPT_A3
	// Since the file has successfully loaded into the address space
	// we need to set the text segment of the address space to read only
	as->as_blocktextwrite = true;	
	#else
	(void)as;
	#endif
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	KASSERT(as->as_stackpbase != 0);

	*stackptr = USERSTACK;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

	KASSERT(new->as_pbase1 != 0);
	KASSERT(new->as_pbase2 != 0);
	KASSERT(new->as_stackpbase != 0);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
		(const void *)PADDR_TO_KVADDR(old->as_pbase1),
		old->as_npages1*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
		(const void *)PADDR_TO_KVADDR(old->as_stackpbase),
		DUMBVM_STACKPAGES*PAGE_SIZE);
	
	*ret = new;
	return 0;
}
