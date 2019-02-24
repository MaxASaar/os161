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

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

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

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}

#ifdef OPT_A2
/* handler for fork() system call */
int sys_fork(struct trapframe * tf, pid_t * retval){
  KASSERT(curproc != NULL);
  /* Create process structure for child process */
  struct proc *child_proc = proc_create_runprogram(curproc->p_name);
  if(!child_proc){
    return ENOMEM;
  }

  /* Create and copy address space (and data) from parent to child */
  struct addrspace *as = kmalloc(sizeof(struct addrspace));
  if(!as){
    proc_destroy(child_proc);
    return ENOMEM;
  }
  int as_error = as_copy(curproc->p_addrspace, &as);
  if(as_error){
    // No memory, destroy the child process
    proc_destroy(child_proc);
    return ENOMEM;
  }
  /* Attach the newly created address space to the child process structure */
  child_proc->p_addrspace = as;

  /* Assign PID to child process and create the parent/child relationship */
  // Since proc.c already assigns a pid to the child, we just need to add the child's pid to the parents children array
  proc_connect(curproc, child_proc);
  
  /*
   * Create thread for child process (need a safe way to pass the trapframe to
   * the child thread).
   */
  // First duplicate the trapframe
  struct trapframe * tf_copy = kmalloc(sizeof(struct trapframe));
  if(!tf_copy){
    return ENOMEM;
  }

  // Initialize the array structure of the new child process;
  child_proc->children = array_create();
  array_init(child_proc->children);
  // Duplicate the data
  memcpy(tf_copy, tf, sizeof(struct trapframe));
  int status = thread_fork("child", child_proc, enter_forked_process, tf_copy, 0);

  if(status != 0){
    // Thread forking failed, delete the trapframe, destroy the child process
    kfree(tf_copy);
    array_cleanup(child_proc->children);
    array_destroy(child_proc->children);
    proc_destroy(child_proc);
    return status;
  }
  *retval = child_proc->p_id;
  return 0;
}
#endif

/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
#ifdef OPT_A2
  *retval = curproc->p_id;
  return(0);
#else
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  *retval = 1;
  return(0);
#endif
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
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

