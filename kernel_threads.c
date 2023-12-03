
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"

#include "kernel_cc.h"
#include "kernel_streams.h"
#include "util.h"

#include <assert.h>

/**
  @brief Create a new thread in the current process.
  */

/*void start_main_thread_process()
{
  int exitval;

  TCB* tcb = cur_thread();

  Task call = tcb->ptcb->task;
  int argl = tcb->ptcb->argl;
  void *args = tcb->ptcb->args;

  exitval = call(argl, args);
  sys_ThreadExit(exitval);
}*/

Tid_t sys_CreateThread(Task task, int argl, void *args)
{
  if(task!=NULL){
  /* Initialize and return a new TCB */
  PCB *pcb = CURPROC;
  TCB *tcb;
  tcb = spawn_thread(pcb, start_main_thread_process);
  /*  and acquire a new PTCB */
  PTCB *ptcb;
  ptcb = (PTCB *)xmalloc(sizeof(PTCB)); /* Memory allocation for the new PTCB */

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
  //ptcb->ptcb_node_list = *rlnode_init(&ptcb->ptcb_node_list, ptcb);
  rlist_push_back(&CURPROC->ptcb_list, &ptcb->ptcb_node_list);
  pcb->thread_count++;
  wakeup(tcb);

  return (Tid_t)ptcb;
}
return NOTHREAD;
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
  return (Tid_t)cur_thread()->ptcb;
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int *exitval)
{
  PTCB *ptcb = (PTCB *)tid;
  //PCB* pcb= CURPROC;

  if (rlist_find(&CURPROC->ptcb_list, ptcb, NULL) == NULL) /*search the list of ptcbs looking for ptcb with the given id(key)*/
  {
    return -1; /*if it's null return error.*/
  }
  if (sys_ThreadSelf() == tid) /*if id of thread calling thread join is the same as the given id return error.*/
  {
    return -1;
  }
  if (ptcb->detached == 1) /*if the thread that calling thread wants to join is detached return error*/
  {
    return -1;
  }

  /*if (ptcb->exited == 1)
  {
    return -1;
  }*/

  ptcb->refcount++; /*increase value of refcount because we refered to this ptcb*/

  while (ptcb->exited == 0 && ptcb->detached == 0)
  {
    kernel_wait(&ptcb->exit_cv, SCHED_USER);
  }
  ptcb->refcount--;

  if (exitval != NULL)
  {
    *exitval = ptcb->exitval;
  }

  if (ptcb->refcount == 0)
  {
    rlist_remove(&ptcb->ptcb_node_list);
    free(ptcb);
  }

  return 0;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
  PTCB *ptcb = (PTCB *)tid;

  if (rlist_find(&CURPROC->ptcb_list, ptcb, NULL) == NULL)
  {
    return -1;
  }

  if (ptcb->exited == 1)
  { // Check if the flag exited is on.If it is return error.
    return -1;
  }

  /*if(ptcb->detached==1){
    return -1;
  }*/

  ptcb->detached = 1; // if everything is right make detach flag on.
  kernel_broadcast(&ptcb->exit_cv);
  //ptcb->refcount=0;

  return 0;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{
  PCB *curproc = CURPROC;
  TCB *tcb = cur_thread();
  PTCB *ptcb = tcb->ptcb;

  curproc->thread_count--; // Thread is going to get deleted
  ptcb->exited = 1;
  ptcb->exitval = exitval;
  kernel_broadcast(&ptcb->exit_cv); // Leave kernel_wait() from ThreadJoin

  if (curproc->thread_count == 0)
  {
    if (get_pid(curproc) != 1)
    {
      /* Moved from sys_Exit from kernel_proc.c */
      /* Reparent any children of the exiting process to the
           initial task */
      PCB *initpcb = get_pcb(1);
      while (!is_rlist_empty(&curproc->children_list))
      {
        rlnode *child = rlist_pop_front(&curproc->children_list);
        child->pcb->parent = initpcb;
        rlist_push_front(&initpcb->children_list, child);
      }

      /* Add exited children to the initial task's exited list
         and signal the initial task */
      if (!is_rlist_empty(&curproc->exited_list))
      {
        rlist_append(&initpcb->exited_list, &curproc->exited_list);
        kernel_broadcast(&initpcb->child_exit);
      }

      /* Put me into my parent's exited list */
      rlist_push_front(&curproc->parent->exited_list, &curproc->exited_node);
      kernel_broadcast(&curproc->parent->child_exit);
    }

    assert(is_rlist_empty(&curproc->children_list));
    assert(is_rlist_empty(&curproc->exited_list));

    /*
      Do all the other cleanup we want here, close files etc.
     */

    /* Release the args data */
    if (curproc->args)
    {
      free(curproc->args);
      curproc->args = NULL;
    }

    /* Clean up FIDT */
    for (int i = 0; i < MAX_FILEID; i++)
    {
      if (curproc->FIDT[i] != NULL)
      {
        FCB_decref(curproc->FIDT[i]);
        curproc->FIDT[i] = NULL;
      }
    }

    /* Disconnect my main_thread */
    curproc->main_thread = NULL;

    /* Now, mark the process as exited. */
    curproc->pstate = ZOMBIE;
  }

  /* Bye-bye cruel world */
  kernel_sleep(EXITED, SCHED_USER);
}
