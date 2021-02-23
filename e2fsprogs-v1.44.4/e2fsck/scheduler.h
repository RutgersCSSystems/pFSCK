/* Scheduler.c 
 *
 * Scheduler.c implements scheduler function
 *
 * The scheduler can be used two ways:
 *  1. Manually invoked via schedule()
 *  2. Via a scheduler background thread
 *
 * The scheduler relies on a generic policy which must be 
 * set as callback. The policy must specify given the current
 * core utilization and the number core utilization of the 
 * process, how to modify thread contexts and schedule threads.
 *
 * Using as a background scheduler thread:
 * In order to use the background scheduler thread,
 * a trigger callback must be set to trigger the
 * scheduler. This callback can be either a simple timer
 * function that puts the scheduler to sleep for periods
 * at a time, or even a semphaphore, allowing other threads
 * to post the the scheduler thread and activate scheduling. 
 */


#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

struct scheduler;

////////////////////////////////////
// Scheduler Management Functions //
////////////////////////////////////

/* Inittializes a new scheduler
 *
 * Allocates a new scheduler struct and sets the default scheduler
 * values
 *
 * @arg  sched_pointer  address to scheduler pointer
 */
void initialize_scheduler(struct scheduler** sched_pointer);

/* Destroys the scheduler
 *
 * Stops any background scheduler thread as well as
 * frees the main scheduler struct
 *
 * @arg  sched_pointer  address to scheduler pointer
 */
void destroy_scheduler(struct scheduler** sched_pointer);


///////////////////////
// Scheduler Setters //
///////////////////////

/* Sets the scheduler policy to be used
 *
 * @arg  sched   pointer to scheduler struct
 * @arg  policy  pointer to policy callback function 
 */
void set_policy(struct scheduler* sched, void* policy);

/* Set the context which is tto be used with the policy
 *
 * @arg  sched    pointer to scheduler struct
 * @arg  context  pointer to context to be used with policy
 */
void set_context(struct scheduler* sched, void* context);

/* Sets the scheduler trigger 
 *
 * @arg  sched    pointer to scheduler struct
 * @arg  trigger  pointer to trigger callback function
 */
void set_trigger(struct scheduler* sched, void* trigger);



////////////////////
// Schedule Logic //
////////////////////

/* Main schedule logic
 * 
 * Gather cpu utilization information and calls the policy callback.
 * Can be invoked by background scheduler thread of called explicity.
 *
 * @arg  sched  pointer to scheduler struct
 */
void schedule(struct scheduler* sched);



////////////////////////////////////
// Background Scheduler Functions //
////////////////////////////////////

/* Background scheduler thread function
 *
 * @arg  args  the scheduler pointer
 *
 */
void scheduler_thread(void* args);

/* Starts the background scheduler thread
 *
 * @arg  sched  scheduler pointer
 */
void run_scheduler_thread(struct scheduler* sched);

void kill_scheduler_thread(struct scheduler* sched);
