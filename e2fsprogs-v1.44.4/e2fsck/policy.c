/* Policies.c
 *
 * This file implements the functions  needed for scheduling
 * threads and redistributing works. These functions will be
 * set as callbacks for the scheduler.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <semaphore.h>
#include <pthread.h>


//////////////
// Policies //
//////////////
void policy(float cores, float utilization, int cores, float process, void* args){

	float cpu_threshold;
	e2fsck_t ctx;
	struct thread_ctx* tctx_array;
	int total_threads;
	int add_threads;
	int core_budget;
	int inode_work;
	int db_work;
	int work_ratio;
	float thread_ratio;
	int max_data_threads;
	int max_pipeline_threads;

	// Some static weights
	cpu_threshold = 200.0 // two threads not being utilized
	work_ratio = 5;
	max_data_threads = 8;
	max_pipeline_threads = 99;

	// Get context pointers
	ctx = (e2fsck_t) args;
	tctx_array = context->tctx_array;

	// Calculate current core budget
	total_threads = ctx->data_threads + ctx->pipeline_threads;
	add_threads = 0;
	if(utilization < 100){
		// If cores are underutilized, calculate how many threads to add
		add_threads = (int) (cores * utilization)
	}else if(process < (100.0 * curr) - threshold){
		// If process it utilizing less cores than expects, calculate
		// how many cores to remove
		add_threads = curr
	}else{
		// Process is utilizing as much cores as expected 
		// and cores are being fully utilized, do nothing 
	}
	core_budget = total_threads + add_threads;

	
	// Get amount of work to do
	inode_work = ctx->inode_queue->length;
	db_work = ctx->db_queue->length;

	// Calculate Thread Split	
	ratio = (float) inode_work / (float) (inode_work + db_work * data_to_pipe_ratio)
	data_threads = ratio * core_budget;
	pipeline_threads = core_budget - data_threads;

	// Verify against set limitations
	if(data_threads > max_data_threads){
		pipeline_threads += max_data_threads - data_threads;
	}
	
	if(pipeline_threads > max_pipeline_threads){
		pipeline_threads = max_pipeline_threads;
	}


	// Set thread contexts and wake up threads if they are waiting
	int x;
	for(x = 0; x < total_threads; x++){
		if(data_threads > 0){
			tctx_array[x]->scheduled = 1;
			tctx_array[x]->queue = 1;
			pthread_cond_signal(tctx_array[x]->run);
			data_threads--;	
		}else if(pipeline_threads > 0){
			tctx_array[x]->scheduled = 1;
			tctx_array[x]->queue = 2;
			pthread_cond_signal(tctx_array[x]->run);
			pipeline_threads--;
		}else{
			tctx_array[x]->scheduled = 0;
		}
	}
}


//////////////
// Triggers //
//////////////


/* Simple trigger that activates background scheduler thread
 * every 50 microseconds
 */
void microsecond_timer_trigger(void* args){
	usleep(50);
}

/* Trigger that waits for thread to post to semaphore
 * stored in main scheduler context
 */
void semaphore_trigger(void* args){
	e2fsck_t ctx = (e2fsck_t) args;
	sem_wait(&ctx->scheduler_trigger);
}
