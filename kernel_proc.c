
#include <assert.h>
#include "kernel_cc.h"
#include "kernel_proc.h"
#include "kernel_streams.h"

/*
 The process table and related system calls:
 - Exec
 - Exit
 - WaitPid
 - GetPid
 - GetPPid

 */

/* The process table */
PCB PT[MAX_PROC];
unsigned int process_count;

PCB *get_pcb(Pid_t pid)
{
  return PT[pid].pstate == FREE ? NULL : &PT[pid];
}

Pid_t get_pid(PCB *pcb)
{
  return pcb == NULL ? NOPROC : pcb - PT;
}

/* Initialize a PCB */
static inline void initialize_PCB(PCB *pcb)
{
  pcb->pstate = FREE;
  pcb->argl = 0;
  pcb->args = NULL;

  for (int i = 0; i < MAX_FILEID; i++)
    pcb->FIDT[i] = NULL;

  rlnode_init(&pcb->children_list, NULL);
  rlnode_init(&pcb->exited_list, NULL);
  rlnode_init(&pcb->children_node, pcb);
  rlnode_init(&pcb->exited_node, pcb);
  pcb->child_exit = COND_INIT;

  rlnode_init(&pcb->ptcb_list, NULL);
  pcb->thread_count = 0;
}

static PCB *pcb_freelist;

void initialize_processes()
{
  /* initialize the PCBs */
  for (Pid_t p = 0; p < MAX_PROC; p++)
  {
    initialize_PCB(&PT[p]);
  }

  /* use the parent field to build a free list */
  PCB *pcbiter;
  pcb_freelist = NULL;
  for (pcbiter = PT + MAX_PROC; pcbiter != PT;)
  {
    --pcbiter;
    pcbiter->parent = pcb_freelist;
    pcb_freelist = pcbiter;
  }

  process_count = 0;

  /* Execute a null "idle" process */
  if (Exec(NULL, 0, NULL) != 0)
    FATAL("The scheduler process does not have pid==0");
}

/*
  Must be called with kernel_mutex held
*/
PCB *acquire_PCB()
{
  PCB *pcb = NULL;

  if (pcb_freelist != NULL)
  {
    pcb = pcb_freelist;
    pcb->pstate = ALIVE;
    pcb_freelist = pcb_freelist->parent;
    process_count++;
  }

  return pcb;
}

/*
  Must be called with kernel_mutex held
*/
void release_PCB(PCB *pcb)
{
  pcb->pstate = FREE;
  pcb->parent = pcb_freelist;
  pcb_freelist = pcb;
  process_count--;
}

/*
 *
 * Process creation
 *
 */

/*
  This function is provided as an argument to spawn,
  to execute the main thread of a process.
*/
void start_main_thread()
{
  int exitval;

  Task call = CURPROC->main_task;
  int argl = CURPROC->argl;
  void *args = CURPROC->args;

  exitval = call(argl, args);
  Exit(exitval);
}

void start_main_thread_process()
{
  int exitval;

  Task call = cur_thread()->ptcb->task;
  int argl = cur_thread()->ptcb->argl;
  void *args = cur_thread()->ptcb->args;

  exitval = call(argl, args);
  sys_ThreadExit(exitval);
}

/*
  System call to create a new process.
 */
Pid_t sys_Exec(Task call, int argl, void *args)
{
  PCB *curproc, *newproc;

  /* The new process PCB */
  newproc = acquire_PCB();

  if (newproc == NULL)
    goto finish; /* We have run out of PIDs! */

  if (get_pid(newproc) <= 1)
  {
    /* Processes with pid<=1 (the scheduler and the init process)
       are parentless and are treated specially. */
    newproc->parent = NULL;
  }
  else
  {
    /* Inherit parent */
    curproc = CURPROC;

    /* Add new process to the parent's child list */
    newproc->parent = curproc;
    rlist_push_front(&curproc->children_list, &newproc->children_node);

    /* Inherit file streams from parent */
    for (int i = 0; i < MAX_FILEID; i++)
    {
      newproc->FIDT[i] = curproc->FIDT[i];
      if (newproc->FIDT[i])
        FCB_incref(newproc->FIDT[i]);
    }
  }

  /* Set the main thread's function */
  newproc->main_task = call;

  /* Copy the arguments to new storage, owned by the new process */
  newproc->argl = argl;
  if (args != NULL)
  {
    newproc->args = malloc(argl);
    memcpy(newproc->args, args, argl);
  }
  else
    newproc->args = NULL;

  /*
    Create and wake up the thread for the main function. This must be the last thing
    we do, because once we wakeup the new thread it may run! so we need to have finished
    the initialization of the PCB.
   */
  if (call != NULL)
  {
    newproc->main_thread = spawn_thread(newproc, start_main_thread);

    /* Allocate memory*/
    PTCB *ptcb = (PTCB *)xmalloc(sizeof(PTCB));
    newproc->main_thread->ptcb = ptcb;
    ptcb->tcb = newproc->main_thread;

    /* PTCB initialization */
    ptcb->task = call;
    ptcb->args = args;
    ptcb->argl = argl;
    ptcb->exited = 0;
    ptcb->detached = 0;
    ptcb->exit_cv = COND_INIT;
    ptcb->refcount = 0;
    newproc->thread_count++;
    ptcb->ptcb_node_list = *rlnode_init(&ptcb->ptcb_node_list, ptcb);
    rlist_push_back(&newproc->ptcb_list, &ptcb->ptcb_node_list);

    wakeup(newproc->main_thread);
  }

finish:
  return get_pid(newproc);
}

/* System call */
Pid_t sys_GetPid()
{
  return get_pid(CURPROC);
}

Pid_t sys_GetPPid()
{
  return get_pid(CURPROC->parent);
}

static void cleanup_zombie(PCB *pcb, int *status)
{
  if (status != NULL)
    *status = pcb->exitval;

  rlist_remove(&pcb->children_node);
  rlist_remove(&pcb->exited_node);

  release_PCB(pcb);
}

static Pid_t wait_for_specific_child(Pid_t cpid, int *status)
{

  /* Legality checks */
  if ((cpid < 0) || (cpid >= MAX_PROC))
  {
    cpid = NOPROC;
    goto finish;
  }

  PCB *parent = CURPROC;
  PCB *child = get_pcb(cpid);
  if (child == NULL || child->parent != parent)
  {
    cpid = NOPROC;
    goto finish;
  }

  /* Ok, child is a legal child of mine. Wait for it to exit. */
  while (child->pstate == ALIVE)
    kernel_wait(&parent->child_exit, SCHED_USER);

  cleanup_zombie(child, status);

finish:
  return cpid;
}

static Pid_t wait_for_any_child(int *status)
{
  Pid_t cpid;

  PCB *parent = CURPROC;

  /* Make sure I have children! */
  int no_children, has_exited;
  while (1)
  {
    no_children = is_rlist_empty(&parent->children_list);
    if (no_children)
      break;

    has_exited = !is_rlist_empty(&parent->exited_list);
    if (has_exited)
      break;

    kernel_wait(&parent->child_exit, SCHED_USER);
  }

  if (no_children)
    return NOPROC;

  PCB *child = parent->exited_list.next->pcb;
  assert(child->pstate == ZOMBIE);
  cpid = get_pid(child);
  cleanup_zombie(child, status);

  return cpid;
}

Pid_t sys_WaitChild(Pid_t cpid, int *status)
{
  /* Wait for specific child. */
  if (cpid != NOPROC)
  {
    return wait_for_specific_child(cpid, status);
  }
  /* Wait for any child */
  else
  {
    return wait_for_any_child(status);
  }
}

void sys_Exit(int exitval)
{

  PCB *curproc = CURPROC; /* cache for efficiency */

  /* First, store the exit status */
  curproc->exitval = exitval;

  /*
    Here, we must check that we are not the init task.
    If we are, we must wait until all child processes exit.
   */

  if (get_pid(curproc) == 1)
  {
    while (sys_WaitChild(NOPROC, NULL) != NOPROC);
  }
  sys_ThreadExit(exitval);
}

/*--------------------------System Info--------------------------*/

int procinfo_read(void* procInfo_t, char* buf, unsigned int size);
int procinfo_close();

typedef struct procinfo_cb
{
  procinfo procinfo;
  int PCB_cursor;
}procinfo_cb;

// File operations for procinfo
static file_ops procinfo_file_ops = {
  .Read = procinfo_read,
  .Close = procinfo_close
};

Fid_t sys_OpenInfo()
{
  Fid_t fid[1];
  FCB* fcb[1];

  // Search for one FCB and one Fid
  if(FCB_reserve(1, fid, fcb) == 0){
      return NOFILE;
  }

  // Memory allocation for new info
  procinfo_cb* info = (procinfo_cb*)xmalloc(sizeof(procinfo_cb));

  // Checking if allocation was succesful
  if(info == NULL){
    return NOFILE;
  }

  // Initialization
  info->PCB_cursor=0;

  // Setting the FCB
  fcb[0]->streamobj = info;
  fcb[0]->streamfunc = &procinfo_file_ops;

  return fid[0];
}

int procinfo_read(void* procinfo1, char* buf, unsigned int size)
{
  if(procinfo1 == NULL){
    return -1;
  }

  procinfo_cb* proc_cb=(procinfo_cb*) procinfo1;  // Cast the void* to procinfo_cb*

  if(proc_cb == NULL){
    return -1;
  }

  /*Because we call it in tinyos_shell so it can reach MAX_PROC*/
  if(proc_cb->PCB_cursor==MAX_PROC){  // If we reach the end of the table, we return 0
    return 0;
  }

  if(proc_cb->PCB_cursor>MAX_PROC){   // If we went over the end of the table, we return -1
    return -1;
  }

  /*Cross the PT table until we find a process that is not FREE*/
  while(PT[proc_cb->PCB_cursor].pstate == FREE){

    // Move to the next PCB
    proc_cb->PCB_cursor++;

    // If we reach the end of the table, we return 0
    if(proc_cb->PCB_cursor==MAX_PROC){
      return 0;
    }
  }

  proc_cb->procinfo.pid=proc_cb->PCB_cursor;

  /*Get the pid of the parent*/
  proc_cb->procinfo.ppid=get_pid((PT[proc_cb->PCB_cursor]).parent);

  /*We make the pstate of the current process to be ALIVE and we put it in the information of the procinfo*/
  proc_cb->procinfo.alive=(PT[proc_cb->PCB_cursor].pstate==ALIVE);

  /*Make thread_count of the procinfo (tinyos.h) to be the threadcount of the current process*/
  proc_cb->procinfo.thread_count=PT[proc_cb->PCB_cursor].thread_count;

  /*Make the main_task of the procinfo (tinyos.h) to be the main_task of the current process*/
  proc_cb->procinfo.main_task=PT[proc_cb->PCB_cursor].main_task;

  /*Make the argl of the procinfo (tinyos.h) to be the argl of the current process*/
  proc_cb->procinfo.argl=PT[proc_cb->PCB_cursor].argl;


  int size_of_argl;

  if(proc_cb->procinfo.argl>PROCINFO_MAX_ARGS_SIZE){
    size_of_argl=PROCINFO_MAX_ARGS_SIZE;
  }else{
    size_of_argl=proc_cb->procinfo.argl;
  }


  memcpy(proc_cb->procinfo.args,(char*)PT[proc_cb->PCB_cursor].args, sizeof(char)*size_of_argl);
  memcpy(buf, (char*)&proc_cb->procinfo,sizeof(procinfo));

  /*Move to the next PCB*/
  proc_cb->PCB_cursor++;

  return 1;
}

int procinfo_close(void* procinfo_cb)
{
  if(procinfo_cb==NULL){
    return -1;
  }

  free(procinfo_cb);

  return 1;
}