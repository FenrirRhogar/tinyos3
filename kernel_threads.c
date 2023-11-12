
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"

/** 
  @brief Create a new thread in the current process.
  */

Tid_t sys_CreateThread(Task task, int argl, void* args) 
{
    /* Initialize and return a new TCB */
    PCB* pcb = CURPROC;
    TCB* tcb;
    tcb = spawn_thread(pcb, start_main_thread_process);
    /*  and acquire a new PTCB */
    PTCB* ptcb;
    ptcb = (PTCB*)malloc(sizeof(PTCB)); /* Memory allocation for the new PTCB */
    
    /* Connect PTCB to TCB and the opposite */
    ptcb->tcb = tcb;
    tcb->ptcb = ptcb;

    /* Initialize PTCB */
    ptcb->task = task;
    ptcb->argl = argl;
    ptcb->args = args;
    ptcb->exited = 0;
    ptcb->detached = 0;
    ptcb->exit_cv = COND_INIT;
    ptcb->refcount = 0;
    rlnode_init(&ptcb->ptcb_node_list, ptcb); /* Initialize node list with PTCB being the node key */
    rlist_push_back(&pcb->ptcb_list, &ptcb->ptcb_node_list);
    pcb->thread_count++;
    wakeup(tcb);

    return (Tid_t) ptcb;
}


/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t) cur_thread()->ptcb;
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
	return -1;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
	return -1;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{

}

