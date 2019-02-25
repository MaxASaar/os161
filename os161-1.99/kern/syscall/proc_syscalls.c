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
  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  #ifdef OPT_A2
  // We don't need p, we are just using curproc
  // Also exitcode is being used
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
  
  #ifdef OPT_A2
  // When destroying a process, we need to notify the parent that is waiting on us that we have died
  // Also, we need to set our exit code
  // parent may or may not be waiting on a cv for us to die
  curproc->has_exited = true;
  // Note, we can safely delete our process if there is no parent
  if(!curproc->parent){
    proc_destroy(curproc);
  }else{
    // If we still have a parent, they could call waitpid on us, even though we have exited, so we still need to stick around
    curproc->exit_code = _MKWAIT_EXIT(exitcode);
    
    // However, if they are waiting on us, we will wake them up using the cv
    lock_acquire(curproc->lock);
    cv_broadcast(curproc->parent_cv, curproc->lock);
    lock_release(curproc->lock);
    // We will destroy the proc when the parent is destroyed 
  }

  #endif
  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

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
	struct proc *child_proc = proc_create_runprogram(curproc->p_name);
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
	array_add(curproc->children, child_proc, &index_of_child);
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
	*retval = 1;
	// Enter_forked_process will handle the rest from the checklist
	return 0;
}
#endif
