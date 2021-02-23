#include "config.h"
#include "e2fsck.h"
#include "problem.h"
#include "pthread.h"
#include "pfsck_problem.h"

#include <sys/syscall.h>


pthread_mutexattr_t Attr;
pthread_mutexattr_t Attr2;
int current_tid;

int pfsck_fix_init(e2fsck_t ctx){
	// need to intialize mutex as rentrent for nested fixes
	pthread_mutexattr_init(&Attr);
	pthread_mutexattr_init(&Attr2);
	pthread_mutexattr_settype(&Attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutexattr_settype(&Attr2, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&ctx->fix_lock, &Attr);
	pthread_mutex_init(&ctx->active_lock, &Attr2);

}

int pfsck_fix_destroy(e2fsck_t ctx){
	// destroy any initialized items
}


/* Prevents pfsck pre and post fix to be called next
 * time fix_problem is executed
 * 
 * pre and post will decrement the disabled counter by 1 
 * resulting is disabled to be zero
 */
void pfsck_disable_fix(e2fsck_t ctx, int num_fixes){

	// check if threading is on
	if(!ctx->extra_threads) return;

	printf("Disabling next %d fixes\n", num_fixes);
	ctx->next_fix_disabled += 2 * num_fixes;
}

/* Synchronizes and halts other thread execution and waits until all 
 * other threads stop before attempting to fix.
 *
 */
void pfsck_pre_fix(e2fsck_t ctx){

	// check if threading is on
	if(!ctx->extra_threads) return;
	
	// Don't run if disabled for a single fix
	if(ctx->next_fix_disabled){
		ctx->next_fix_disabled--;
		return;
	}

	// Increment number of fixers
	pthread_mutex_lock(&ctx->fix_lock);
	ctx->pending_fixes++;
	pthread_mutex_unlock(&ctx->fix_lock);

	// Decrement the number of active threads
	pthread_mutex_lock(&ctx->active_lock);
	ctx->active_count--;

	// Wait until all active threads are stalled
	if(ctx->active_count > 0){
		//printf("Active count positive, waiting on work stalled\n");
		pthread_cond_wait(&ctx->work_stalled, &ctx->active_lock);
	}else{
		//printf("prefix work stalled, signalling\n");
		pthread_cond_broadcast(&ctx->work_stalled);
	}
	pthread_mutex_unlock(&ctx->active_lock);

	// Threads that need to do fixes should be synced at this point //

	// Run fixes in lockstep
	pthread_mutex_lock(&ctx->fix_lock);

	struct thread_ctx* tctx = (struct thread_ctx*) pthread_getspecific(ctx->tctx_key);
	if(tctx != NULL){
		current_tid = tctx->tid;
	}
}

/* Routine called at the end of fix_problem.
 *
 * Decremenents fix counter, and signals other threads with pending fixes 
 * to carry out. If not, signal stalled threads to continue execution
 *
 */
void pfsck_post_fix(e2fsck_t ctx){

	// Check if threading is on
	if(!ctx->extra_threads) return;

	// check if disabled
	if(ctx->next_fix_disabled){
		ctx->next_fix_disabled--;
		//printf("Disabled should be zero, currrent val: %d\n", \
				ctx->next_fix_disabled);
		return;
	}

	// Decrement fix counter
	ctx->pending_fixes--;

	struct thread_ctx *tctx = (struct thread_ctx*) pthread_getspecific(ctx->tctx_key);

	// Wait until fixes are finished
	if(ctx->pending_fixes){
		//printf("Fixes not Done, left %d\n", ctx->pending_fixes);
		if(tctx != NULL && current_tid == tctx->tid){
			goto cont;
		}
		pthread_cond_wait(&ctx->fixes_done, &ctx->fix_lock);
	}else{
		//printf("Fixes done, signalling fixes done\n");
		pthread_cond_broadcast(&ctx->fixes_done);
	}

cont:

	pthread_mutex_unlock(&ctx->fix_lock);

	if(tctx == NULL){
		return;
	}

	// Increment active count before return
	pthread_mutex_lock(&ctx->active_lock);
	ctx->active_count++;
	pthread_mutex_unlock(&ctx->active_lock);

}


/* Presync. called at the beginning of routine to check filesystem object to 
 * synchronize, and disallow any fixed from occuring until sync_fix
 *
 */
int pfsck_sync_start(e2fsck_t ctx){

	// check if threading is on
	if(!ctx->extra_threads) return 1;
	
	//printf("Entered sync start routine\n");
	//return 0;

	// incrememnt active count
	pthread_mutex_lock(&ctx->active_lock);
	ctx->active_count++;
	pthread_mutex_unlock(&ctx->active_lock);

}

/* Stop syncing, called at the end of routine checking multiple filesystem objects.
 */ 
int pfsck_sync_stop(e2fsck_t ctx){

	// check if threading is on
	if(!ctx->extra_threads) return 1;

	// dercrement active count 
	pthread_mutex_lock(&ctx->active_lock);
	ctx->active_count--;
	pthread_mutex_unlock(&ctx->active_lock);

}

/* Called at end of routine to check filesystem object to synchronize
 * Synchronizes thread execution to perform exclusive fix
 *
 * If threre are no outstanding fixes, simply return 
 *
 * The idea is that when processing elements in a loop, 
 * it is hard to determine every terminating statment, 
 * so simply have a single sync point at the beginning of processing
 * a new object
 *
 * Note: ensure no locks are hed upon entry into pfsck_syn_fix
 * 
 */
int pfsck_sync_fix(e2fsck_t ctx){

	// check if threading is on
	if(!ctx->extra_threads) return 1;

	// Return immediately if there are no fixes to be done
	if (!ctx->pending_fixes){
	       	return 0;
	}

	// decrement active count
	pthread_mutex_lock(&ctx->active_lock);
	ctx->active_count--;
	pthread_mutex_unlock(&ctx->active_lock);

	if(ctx->active_count <= 0){
		//printf("Work stalled, signalling\n");
		pthread_cond_broadcast(&ctx->work_stalled);
	}

	//printf("Going to wait (tid: %d)\n",  syscall(SYS_gettid));
	
	pthread_mutex_lock(&ctx->fix_lock);
	if(ctx->pending_fixes){
		pthread_cond_wait(&ctx->fixes_done, &ctx->fix_lock);
	}
	pthread_mutex_unlock(&ctx->fix_lock);

	pthread_mutex_lock(&ctx->active_lock);
	ctx->active_count++;

	//printf("Sync fix continue (tid: %d) (fixes: %d)\n",  syscall(SYS_gettid), ctx->pending_fixes);

	pthread_mutex_unlock(&ctx->active_lock);


	return 0;
}
