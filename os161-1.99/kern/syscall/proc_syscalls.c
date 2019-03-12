#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>

#include "opt-A2.h"
#include <mips/trapframe.h>
#include <synch.h>
#include <limits.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <vm.h>
  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {
  struct addrspace *as;
  #ifdef OPT_A2
  struct proc *p = curproc; 
#else
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;
  #endif
  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);
  
  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  #ifdef OPT_A2
  // When destroying a process, we need to notify the parent that is waiting on us that we have died
  // Also, we need to set our exit code
  // parent may or may not be waiting on a cv for us to die
  p->has_exited = true;
  // Note, we can safely delete our process if there is no parent
  if(p->parent == NULL){
    proc_destroy(p);
  }else{
    // If we still have a parent, they could call waitpid on us, even though we have exited, so we still need to stick around
    p->exit_code = _MKWAIT_EXIT(exitcode);
    
    // However, if they are waiting on us, we will wake them up using the cv
    lock_acquire(p->lock);
    cv_broadcast(p->parent_cv, p->lock);
    lock_release(p->lock);
    // We will destroy the proc when the parent is destroyed 
  }

  #endif
  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  #ifdef OPT_A2
  // Proc destroy is being handled elsewhere
  #else
  proc_destroy(p);
  #endif

  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  #ifdef OPT_A2
  *retval = curproc->p_id;
  #else
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  *retval = 1;
  #endif
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */
  if (options != 0) {
    return(EINVAL);
  }
  #ifdef OPT_A2
  // First, check if the requested pid is a child
  int index_of_child = -1;
  for(unsigned int i = 0; i < array_num(curproc->children); i++){
    if(pid == ((struct proc *)array_get(curproc->children, i))->p_id){
      index_of_child = i;
      break;
    }
  }
  // If there was no child, return ECHILD error
  if(index_of_child == -1){
    return ECHILD;
  }
  // If the child has already exited, set exitstatus as their exit code!
  // If the child is still alive, wait on their cv for them to exit
  struct proc *child = (struct proc *)array_get(curproc->children, index_of_child);
  lock_acquire(child->lock);
  if(child->has_exited){
    exitstatus = child->exit_code;
  }else{
    while(!child->has_exited){
      cv_wait(child->parent_cv, child->lock);
    }
    exitstatus = child->exit_code;
  }
  lock_release(child->lock); 
  #else
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  #endif
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
  
}

#ifdef OPT_A2

//Fork system call
int sys_fork(struct trapframe *tf, pid_t *retval){
	(void) tf;
	(void) retval;
	KASSERT(curproc != NULL);
	// Create process struct for the child
	struct proc *child_proc = proc_create_runprogram("new proc");
	if(child_proc == NULL){
		return ENOMEM;
	}
	// Create and copy address space + data from parent to child
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if(!as){
		proc_destroy(child_proc);
		return ENOMEM;
	}
	int as_error = as_copy(curproc->p_addrspace, &as);
	if(as_error){
		kfree(as);
		proc_destroy(child_proc);
		return ENOMEM;
	}
	
 	// Attach newly created address space to the child process
	child_proc->p_addrspace = as;
	// Assign PID to child, create parent/child relationship
	// Append the child to the children array
	unsigned int index_of_child;
	//////////////
	lock_acquire(curproc->lock);
	array_add(curproc->children, child_proc, &index_of_child);
	lock_release(curproc->lock);
	// Assign the child's parent
	child_proc->parent = curproc;
	// Create a duplicate trapframe for the child process
	struct trapframe *tf_copy = kmalloc(sizeof(struct trapframe));
	if(!tf_copy){
		return ENOMEM;
	}
	*tf_copy = *tf;

	// Create thread for child process (how does trapframe get passed?)
	int fork_status = thread_fork("child", child_proc, enter_forked_process, tf_copy, 0);
	if(fork_status != 0){
		kfree(tf_copy);
		proc_destroy(child_proc);
		return fork_status;
	}
	*retval = child_proc->p_id;
	// Enter_forked_process will handle the rest from the checklist
	return 0;
}

//Execv system call
int sys_execv(userptr_t progname, userptr_t args){
	// Count number of arguments
	int arg_count = 0;
	char ** args_ptr = (char **) args; // Casting args to proper type
	while(args_ptr[arg_count] != NULL){
		arg_count++;
	}
	
	// if too many args were passed in, return E2BIG
	if(arg_count > ARG_MAX){
		return E2BIG;
	}
	// Copy arguments into the kernel
	char ** args_copy = kmalloc((sizeof(char *))*(arg_count + 1));
	if(args_copy == NULL){
		return ENOMEM;
	}
	for(int x = 0; x < arg_count; x++){
		// Need to allocate space for each string in args_ptr
		int current_str_len = strlen(args_ptr[x]) + 1;
		args_copy[x] = kmalloc(sizeof(char) * current_str_len);
		if(args_copy[x] == NULL){
			return ENOMEM;
		}
		int strcopy_status = copyinstr((userptr_t)args_ptr[x], args_copy[x], current_str_len, NULL);
		if(strcopy_status > 0){
			kfree(args_copy);
			return EFAULT;
		}
	}
	// Add the null string to the end of the args_copy array
	args_copy[arg_count] = NULL;

	// Open program file using vfs_open
	//  need to create a copy of the program name to pass into vfs_open
	//  get length of progname first
	int progname_length = strlen((char *) progname) + 1;
	char * progname_copy = kmalloc(sizeof(char) * progname_length);
	if(progname_copy == NULL){
		return ENOMEM;
	}
	int strcopy_status = copyinstr(progname, progname_copy, progname_length, NULL);
	if(strcopy_status > 0){
		kfree(progname_copy);
		return EFAULT;
	}
	// We need a vnode ptr to store the file that we are opening
	struct vnode * vnode_ptr;
	int vfs_open_status = vfs_open(progname_copy, O_RDONLY, 0, &vnode_ptr);
	if(vfs_open_status){
		kfree(progname_copy);
		return ENOENT;
	}

	// Create new address space, set process to new address space, activate
	//  first get current address space
	struct addrspace *as = curproc_getas();
	struct addrspace *as_new = as_create();
	if(as_new == NULL){
		vfs_close(vnode_ptr);
		kfree(progname_copy);
		return ENOMEM;
	}
	as_deactivate(); // Deactivate old address space
	curproc_setas(as_new); // Add new address space to the process
	as_activate(); // Activate tbe new address space
	
	// Load program image using load_elf
	// ** we need to keep an entrypoint ptr
	vaddr_t entry_pt;
	int load_elf_status = load_elf(vnode_ptr, &entry_pt); // Elves btw
	if(load_elf_status){
		vfs_close(vnode_ptr);
		return ENODEV;
	}
	
	// Close the file since the executable is now loaded
	vfs_close(vnode_ptr);

	// Copy args into the new address space
	// Remember to copy the args (array and strings) onto user stack
	//  as part of as_define_stack
	
	//  ** we need a stack ptr
	vaddr_t stack_ptr;
	int as_define_stack_status = as_define_stack(as_new, &stack_ptr);
	if(as_define_stack_status){
		return ENOMEM;
	}
	//  we need to bring the args_copy into user space
	//  this means putting it on the user stack manually (gross)
	//  we must populate the array from back to front, decrementing stack ptr
	//  for every element
	vaddr_t args_array[arg_count+1];
	args_array[arg_count] = (vaddr_t) NULL; // Last element is null terminator
	for(int x = arg_count-1; x >= 0; x--){
		int current_string_length = strlen(args_copy[x]) + 1;
		// Store this string on the stack
		stack_ptr -= current_string_length;
		// Save the address of this string on the stack
		args_array[x] = stack_ptr;
		int copyoutstr_status = copyoutstr(args_copy[x],
						   (userptr_t) stack_ptr,
						   current_string_length,
						   NULL);
		if(copyoutstr_status){
			return ENOMEM;
		}
	}

	// Now that all of the strings are on the stack, we need to construct the array of
	//  pointers to the data

	// Adjust the stackpointer to the nearest multiple of 8
	if(stack_ptr % 8 != 0){
		stack_ptr -= stack_ptr % 8;
	}
	
	int vaddr_size = sizeof(vaddr_t) % 4 != 0 ?
		sizeof(vaddr_t) + 4 - (sizeof(vaddr_t) % 4) :
		sizeof(vaddr_t);
	// Store an array of pointers on the stack
	for (int x = arg_count; x >= 0; x--){ // Note this includes the null terminator
		
		stack_ptr = stack_ptr - vaddr_size;
		int copyout_status = copyout(&args_array[x],
					     (userptr_t) stack_ptr,
					     sizeof(vaddr_t));
		if(copyout_status){
			return ENOMEM;
		}
	}
	// Delete old address space
	as_destroy(as);
	// Call enter_new_process with address to arguments on the stack
	//  also pass the stack pointer, and program entry point 
	enter_new_process(arg_count, (userptr_t) stack_ptr, stack_ptr, entry_pt);
	
	// This code should never run:
	panic("enter_new_process call failed (sys_execv)\n");
	return -1;
}
#endif
