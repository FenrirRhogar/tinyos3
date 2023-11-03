
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"

/** 
  @brief Create a new thread in the current process.
  */
TCB* create_thread(Task task, int argl, void* args) {
    // Allocate memory for the Thread Control Block (TCB)
    TCB* new_thread = (TCB*)malloc(sizeof(TCB));

    if (new_thread == NULL) {
        return NULL;  // Failed to allocate memory for TCB
    }

    // Initialize the fields of the TCB
    new_thread->tid = generate_unique_tid();  // Implement your own Tid generation logic
    new_thread->state = THREAD_READY;
    new_thread->stack = malloc(STACK_SIZE);  // Allocate memory for the thread's stack
    new_thread->stack_size = STACK_SIZE;      // Set the stack size (define your STACK_SIZE)
    new_thread->context = initialize_thread_context(task, args, argl);

    if (new_thread->stack == NULL || new_thread->context == NULL) {
        // Failed to allocate stack or initialize context
        free(new_thread->stack);
        free(new_thread->context);
        free(new_thread);
        return NULL;
    }

    // Any other initialization steps you might have

    return new_thread;
}

Tid_t sys_CreateThread(Task task, int argl, void* args) {
    // Check if the current process is allowed to create a new thread
    if (!is_valid_process()) {
        return NOTHREAD;
    }

    // Check if the provided task is valid
    if (task == NULL) {
        return NOTHREAD;
    }

    // Create a new TCB (Thread Control Block) for the new thread
    TCB* new_thread = create_thread(task, argl, args);

    if (new_thread == NULL) {
        return NOTHREAD;
    }

    // Get the current process's PTCB (Process Thread Control Block)
    PTCB* current_ptcb = get_current_ptcb();

    // Add the new thread to the current process's list of threads
    if (add_thread_to_ptcb(current_ptcb, new_thread) != 0) {
        // Failed to add the thread to the process's list
        destroy_thread(new_thread);
        return NOTHREAD;
    }

    // Return the Tid of the newly created thread
    return new_thread->tid;
}


/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t) cur_thread();
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

