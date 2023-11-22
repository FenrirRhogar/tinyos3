
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"

#include "kernel_cc.h"
#include "kernel_streams.h"
#include "util.h"

/** 
  @brief Create a new thread in the current process.
  */

Tid_t sys_CreateThread(Task task, int argl, void* args) 
{
    /* Initialize and return a new TCB */
    PCB* pcb = CURPROC;
    TCB* tcb;
    tcb = spawn_thread(pcb, start_main_thread_process());
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
  PTCB* ptcb= (PTCB*) tid;

  if(rlist_find(&CURPROC->ptcb_list_node,ptcb,NULL)==NULL)/*search the list of ptcbs looking for ptcb with the given id(key)*/
  {
    return -1;/*if it's null return error.*/
  }
  if(sys_ThreadSelf()==tid)/*if id of thread calling thread join is the same as the given id return error.*/
  {
    return -1;
  }
  if(ptcb->detached==1)/*if the thread that calling thread wants to join is detached return error*/
  {
    return -1;
  }

  if(ptcb->exited==1){
    return -1;
  }

  ptcb->refcount++;/*increase value of refcount because we refered to this ptcb*/

  while(ptcb->exited==0 && ptcb->detached==0){
    kernel_wait(&ptcb->exit_cv,SCHED_USER);
  }
  ptcb->refcount--;
   if(exitval!=NULL){
    *exitval=ptcb->exitval;
   }

   if(ptcb->refcount==0){
    rlist_remove(&ptcb->ptcb_node_list)
    free(ptcb);
   }


	return -1;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
  PTCB* ptcb= (PTCB*) tid;

  if(rlist_find(&CURPROC->ptcb_list_node,ptcb,NULL)==NULL){
    return -1;
  }
	return -1;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{

}

