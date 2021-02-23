#include "config.h"
#include <string.h>
#include <errno.h>
#include <time.h>
#include "sys/syscall.h"
#include "thpool.h"
#include <setjmp.h>
#include "e2fsck.h"
#include <ext2fs/ext2_ext_attr.h>
#include "problem.h"




/* Fetches thread specific thread context
 */
struct thread_ctx* get_tctx(e2fsck_t ctx){
	int x, tid;

	struct thread_ctx* tctx = (struct thread_ctx*) pthread_getspecific(ctx->tctx_key);

	if(tctx == NULL){
		tid = syscall(SYS_gettid);
		fsck_lock(&ctx->tctx_array_lock);
		for(x = 0; x < ctx->tctx_count; x++){
			if(ctx->tctx_array[x].tid == 0){
				tctx = &ctx->tctx_array[x];
				tctx->tid = tid;
				pthread_setspecific(ctx->tctx_key, (void*) tctx);
				pthread_setspecific(ctx->fs_key, (void*) tctx->fs);
				break;
			}
		}
		fsck_unlock(&ctx->tctx_array_lock);
	}

	return tctx;
}

/* Generic Thread Runner
 *
 */ 
void run_thread(void* args){

	struct thread_ctx* tctx = (struct thread_ctx*) args;
	e2fsck_t ctx = tctx->ctx;

	//while(tctx->scheduled){
	while(1){	
		
		//wait(tctx->queue);

		//if(task == NULL){
		//	break;
		//}

		// How to mark when queue is done being processed?
	}

	//ctx->threads_running--;
}

void schedule_data_thread(e2fsck_t ctx){
	// set context
	// add work, with generic threadpool,
	if(ctx->scheduled_threads < ctx->threads){ 
		//thpool_add_work(ctx->tpool, (void*) run_thread, (void*) tctx);
	}else{
		printf("Tried to schedule more threads than available.\n");
		printf("Should not get here.\n");
	}
}
void unschedule_data_thread(e2fsck_t ctx){
	if(ctx->active_pipeline_threads > 0){
		//queue_push(ctx->inode_queue, NULL);
	}else{
		printf("Tried to unschedule data thread, when there is non.\n");
		printf("Shouldn't get here.\n");
	}
}

void schedule_pipeline_thread(e2fsck_t ctx){

	if(ctx->scheduled_threads > ctx->threads){
		//thpool_add_work(ctx->tpool, (void*) run_thread, (void*) tctx);
	}else{
		printf("Tried to schedule more threads than available.\n");
		printf("Shouldn't get here.\n");
	}
}

void unschedule_pipeline_thread(e2fsck_t ctx){
	if(ctx->active_pipeline_threads > 0){
		//queue_push(ctx->inode_queue, NULL);
	}else{
		printf("Tried to unschedule pipeline thread, when there is none");
		printf("Shouldn't get here.\n");
	}
}

void turn_data_to_pipe(e2fsck_t ctx){
	unschedule_data_thread(ctx);
	schedule_pipeline_thread(ctx);
}

void turn_pipe_to_data(e2fsck_t ctx){
	unschedule_pipeline_thread(ctx);
	schedule_data_thread(ctx);
}
