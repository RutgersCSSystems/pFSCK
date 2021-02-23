/*
 * e2fsck.c - a consistency checker for the new extended file system.
 *
 * Copyright (C) 1993, 1994, 1995, 1996, 1997 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include "config.h"
#include <errno.h>

#include "e2fsck.h"
#include "problem.h"

#include <time.h>

//int continue_single_threaded = 0;

/*
 * This function allocates an e2fsck context
 */
errcode_t e2fsck_allocate_context(e2fsck_t *ret)
{
	e2fsck_t	context;
	errcode_t	retval;
	char		*time_env;

	retval = ext2fs_get_mem(sizeof(struct e2fsck_struct), &context);
	if (retval)
		return retval;

	memset(context, 0, sizeof(struct e2fsck_struct));

	context->process_inode_size = 256;
	context->ext_attr_ver = 2;
	context->blocks_per_page = 1;
	context->htree_slack_percentage = 255;

	time_env = getenv("E2FSCK_TIME");
	if (time_env)
		context->now = strtoul(time_env, NULL, 0);
	else {
		context->now = time(0);
		if (context->now < 1262322000) /* January 1 2010 */
			context->flags |= E2F_FLAG_TIME_INSANE;
	}

	*ret = context;
	return 0;
}

/*
 * This function resets an e2fsck context; it is called when e2fsck
 * needs to be restarted.
 */
errcode_t e2fsck_reset_context(e2fsck_t ctx)
{
	int	i;

	ctx->flags &= E2F_RESET_FLAGS;
	ctx->lost_and_found = 0;
	ctx->bad_lost_and_found = 0;
	if (ctx->inode_used_map) {
		ext2fs_free_inode_bitmap(ctx->inode_used_map);
		ctx->inode_used_map = 0;
	}
	if (ctx->inode_dir_map) {
		ext2fs_free_inode_bitmap(ctx->inode_dir_map);
		ctx->inode_dir_map = 0;
	}
	if (ctx->inode_reg_map) {
		ext2fs_free_inode_bitmap(ctx->inode_reg_map);
		ctx->inode_reg_map = 0;
	}
	if (ctx->block_found_map) {
		ext2fs_free_block_bitmap(ctx->block_found_map);
		ctx->block_found_map = 0;
	}
	if (ctx->inode_link_info) {
		ext2fs_free_icount(ctx->inode_link_info);
		ctx->inode_link_info = 0;
	}
	if (ctx->journal_io) {
		if (ctx->fs && ctx->fs->io != ctx->journal_io)
			io_channel_close(ctx->journal_io);
		ctx->journal_io = 0;
	}
	if (ctx->fs && ctx->fs->dblist) {
		ext2fs_free_dblist(ctx->fs->dblist);
		ctx->fs->dblist = 0;
	}
	e2fsck_free_dir_info(ctx);
	e2fsck_free_dx_dir_info(ctx);
	if (ctx->refcount) {
		ea_refcount_free(ctx->refcount);
		ctx->refcount = 0;
	}
	if (ctx->refcount_extra) {
		ea_refcount_free(ctx->refcount_extra);
		ctx->refcount_extra = 0;
	}
	if (ctx->ea_block_quota_blocks) {
		ea_refcount_free(ctx->ea_block_quota_blocks);
		ctx->ea_block_quota_blocks = 0;
	}
	if (ctx->ea_block_quota_inodes) {
		ea_refcount_free(ctx->ea_block_quota_inodes);
		ctx->ea_block_quota_inodes = 0;
	}
	if (ctx->ea_inode_refs) {
		ea_refcount_free(ctx->ea_inode_refs);
		ctx->ea_inode_refs = 0;
	}
	if (ctx->block_dup_map) {
		ext2fs_free_block_bitmap(ctx->block_dup_map);
		ctx->block_dup_map = 0;
	}
	if (ctx->block_ea_map) {
		ext2fs_free_block_bitmap(ctx->block_ea_map);
		ctx->block_ea_map = 0;
	}
	if (ctx->block_metadata_map) {
		ext2fs_free_block_bitmap(ctx->block_metadata_map);
		ctx->block_metadata_map = 0;
	}
	if (ctx->inode_bb_map) {
		ext2fs_free_inode_bitmap(ctx->inode_bb_map);
		ctx->inode_bb_map = 0;
	}
	if (ctx->inode_bad_map) {
		ext2fs_free_inode_bitmap(ctx->inode_bad_map);
		ctx->inode_bad_map = 0;
	}
	if (ctx->inode_imagic_map) {
		ext2fs_free_inode_bitmap(ctx->inode_imagic_map);
		ctx->inode_imagic_map = 0;
	}
	if (ctx->dirs_to_hash) {
		ext2fs_u32_list_free(ctx->dirs_to_hash);
		ctx->dirs_to_hash = 0;
	}

	/*
	 * Clear the array of invalid meta-data flags
	 */
	if (ctx->invalid_inode_bitmap_flag) {
		ext2fs_free_mem(&ctx->invalid_inode_bitmap_flag);
		ctx->invalid_inode_bitmap_flag = 0;
	}
	if (ctx->invalid_block_bitmap_flag) {
		ext2fs_free_mem(&ctx->invalid_block_bitmap_flag);
		ctx->invalid_block_bitmap_flag = 0;
	}
	if (ctx->invalid_inode_table_flag) {
		ext2fs_free_mem(&ctx->invalid_inode_table_flag);
		ctx->invalid_inode_table_flag = 0;
	}
	if (ctx->encrypted_dirs) {
		ext2fs_u32_list_free(ctx->encrypted_dirs);
		ctx->encrypted_dirs = 0;
	}
	if (ctx->inode_count) {
		ext2fs_free_icount(ctx->inode_count);
		ctx->inode_count = 0;
	}

	/* Clear statistic counters */
	ctx->fs_directory_count = 0;
	ctx->fs_regular_count = 0;
	ctx->fs_blockdev_count = 0;
	ctx->fs_chardev_count = 0;
	ctx->fs_links_count = 0;
	ctx->fs_symlinks_count = 0;
	ctx->fs_fast_symlinks_count = 0;
	ctx->fs_fifo_count = 0;
	ctx->fs_total_count = 0;
	ctx->fs_badblocks_count = 0;
	ctx->fs_sockets_count = 0;
	ctx->fs_ind_count = 0;
	ctx->fs_dind_count = 0;
	ctx->fs_tind_count = 0;
	ctx->fs_fragmented = 0;
	ctx->fs_fragmented_dir = 0;
	ctx->large_files = 0;

	for (i=0; i < MAX_EXTENT_DEPTH_COUNT; i++)
		ctx->extent_depth_count[i] = 0;

	/* Reset the superblock to the user's requested value */
	ctx->superblock = ctx->use_superblock;

	return 0;
}

///////////////////////////////////////////////////////////////////////////////

struct vgroup{
  int start_group;
  int start_offset;
  int end_group;
  int end_offset;
  int start_inode;
  int end_inode;
  int total_inodes;

  int ret;
  void* dir_info;
  ext2_dblist dblist;
  void* ctx;

};



#ifdef _SCHEDULER
//////////////
// Policies //
//////////////////////////////////////////////////////////////////////
int adjust_core_count(int cores, int prev_cores, float total_util, float process_util){

	// There are 4 cases:
	// 1. High total CPU, High process Util
	// 	-> keep core budget
	// 2. High total CPU, Low process Util
	// 	-> lower core budget
	// 3. Low total CPU, low or high process Util
	// 	-> increase core budget
	
	float expected_process_util = 100.0 * (float) prev_cores - 100.0;
	int effective_cores_used = (int) (total_util / 100.0);
	float percent_per_core = 100.0 / (float) cores;
	int extra_cores;
	int core_budget;

	printf("Total CPU Utilization: %f\n", total_util);
	printf("Process Utilization: %f\n", process_util);

	if(total_util > 100.0 - percent_per_core){
		
		if(process_util < expected_process_util){
			// CPU High and Process Low, so lower it
			// to the effective cores used 
			//core_budget = prev_cores / 2;//effective_cores_used + 1;
			core_budget = effective_cores_used + 1;
			//if(prev_cores % 2){
			//	core_budget++;
			//}
			if(core_budget <= 0){
				core_budget = 1;
			}
			printf("Lowering core budget from %d to %d\n", prev_cores, core_budget);
		}else{
			// CPU High and Process High, maintain core count;
			core_budget = prev_cores;	
		}

	}else{
		extra_cores = (100.0 - total_util)/percent_per_core;
		if(extra_cores){
			core_budget = prev_cores + 1; //extra_cores - 1;
		}
		if(core_budget > cores){
			core_budget = cores;
		}
		printf("Increasing core budget form %d to %d\n", prev_cores, core_budget);
	}

	return core_budget;


}

void policy(float utilization, float process, void* args){

	int inode_tasks, inodes_per_task = 0, inode_work; // Inode vars
	int db_tasks, db_per_task = 0, db_work;          // db vars
	int ideal_data_threads, ideal_pipeline_threads;      // ideal vars
	int tpool1_alive_count, tpool2_alive_count;
	int tpool1_add_threads, tpool2_add_threads;
	float float_ideal_data_threads, float_ideal_pipeline_threads;

	e2fsck_t ctx;

	// Some limits
	int max_data = 4;
	int max_pipeline = 99;

	// cast main context;
	ctx = (e2fsck_t) args;

	// Get inode work
	inode_tasks = get_queue_length(ctx->tpool_1);
	inodes_per_task = ctx->vgroups[0].total_inodes;
	inode_work = inode_tasks * inodes_per_task;
	//printf("Inode tasks: %d Inodes per: %d\n", inode_tasks, inodes_per_task);

	// Get DB work
	db_tasks = get_queue_length(ctx->tpool_2);
#ifdef _DBLIST_THRESHOLD
	db_per_task = _DBLIST_THRESHOLD;
#else
	db_per_task = 5000;
	printf("[WARNING] Scheduler unaware of dblist granularity, using %d\n", db_per_task);
#endif
	db_work = db_tasks * db_per_task;
	//printf("db tasks: %d, db per task: %d\n", db_tasks, db_per_task);


	// Calculate Ideal Thread Count
	int weight = 100;
	int core_budget; 
	int total_work;

	// Call adjustment core count if shceduler is core aware
	if(ctx->core_aware){
		//if(ctx->single_thread == 1){
		// 	continue_single_threaded = 1;
		//	core_budget = adjust_core_count(ctx->max_threads, ctx->core_budget, 50.0, process);
		//}else{
			core_budget = adjust_core_count(ctx->max_threads, ctx->core_budget, utilization, process);
		//}
		ctx->core_budget = core_budget;
	}else{
		core_budget = ctx->max_threads;
	}


	// Set weights
	inode_work = inode_work;
	db_work = db_work * 9;

	//Calculate total work
	total_work = inode_work + db_work;

	//printf("inode: %d db: %d total: %d\n", inode_work, db_work, total_work);

	// Calculate ideal threads
	if(total_work == 0){
		ideal_data_threads = 0;
		ideal_pipeline_threads = 0;
	}else{
		float_ideal_data_threads = (float) inode_work * core_budget / total_work;
		float_ideal_pipeline_threads = (float) db_work * core_budget / total_work;
		ideal_data_threads = (int) float_ideal_data_threads;
		ideal_pipeline_threads = (int) float_ideal_pipeline_threads;

		// Round
		if(float_ideal_data_threads - (float) ideal_data_threads >= 0.5){
			ideal_data_threads++;
		}
		if(float_ideal_pipeline_threads - (float) ideal_pipeline_threads >= 0.5){
			ideal_pipeline_threads++;
		}

	}

	// Enforce limitations
	/* Limit: no more than 4 data threads due to scalability */
	if(ideal_data_threads > 4){
		printf("Enforce max data thread policy of 4\n");
		ideal_pipeline_threads += ideal_data_threads - 4;
		ideal_data_threads = 4;
	}

	if(ideal_pipeline_threads > 8){
		printf("Enforce max pipe thread policy of 8\n");
		ideal_pipeline_threads = 8;
	}

	//ideal_data_threads = 2;
	//ideal_pipeline_threads = 6;

	// Capture how many threads currently assigned
	// Should be saved in state, we don't want to capture wrong
	// information is a thread was previously migrating
	//
	tpool1_alive_count = ctx->tpool_1_count;
	tpool2_alive_count = ctx->tpool_2_count;

	//printf("Tpool1: %d, Tpool2: %d\n", tpool1_alive_count, tpool2_alive_count);
	//printf("Queues: %d, %d\n", inode_tasks, db_tasks);

	//printf("Ideal: %d, %d\n", ideal_data_threads, ideal_pipeline_threads);

	tpool1_add_threads = 0;
	tpool2_add_threads = 0;

	tpool1_add_threads = ideal_data_threads - tpool1_alive_count;
	tpool2_add_threads = ideal_pipeline_threads - tpool2_alive_count;
	

	// THIS IS TRICKY, during thread migration, work can be done as quickly
	// during migration. What should be migrated to first?
	// We expect threads to migrate from pass 1 to pass 2 in general
	// Given thread contention in pass1
	//
	// Give priority to data threads
	//
	// Note: To ensure we don't go over core budget Borrow before adding

	// Borrow data for pipeline threads
	if(tpool2_add_threads > 0 && tpool1_add_threads < 0){
		while(tpool2_add_threads != 0 && tpool1_add_threads != 0){
			borrow_threads(ctx->tpool_1, ctx->tpool_2, 1);
			tpool2_add_threads--;
			ctx->tpool_2_count++;
			tpool1_add_threads++;
			ctx->tpool_1_count--;
		}
	}

	// Borrow pipeline for data threads
	if(tpool1_add_threads > 0 && tpool2_add_threads < 0){
		while(tpool1_add_threads != 0 && tpool2_add_threads != 0){
			borrow_threads(ctx->tpool_2, ctx->tpool_1, 1);
			tpool1_add_threads--;
			ctx->tpool_1_count++;
			tpool2_add_threads++;
			ctx->tpool_2_count--;
		}
	}

	// Handle rest of pipeline threads
	while(tpool2_add_threads != 0){
		if(tpool2_add_threads > 0){
			borrow_threads(ctx->tpool_idle, ctx->tpool_2, 1);
			tpool2_add_threads--;
			ctx->tpool_2_count++;
		}else if(tpool2_add_threads < 0){
			release_borrowed_threads(ctx->tpool_2, 1);
			tpool2_add_threads++;
			ctx->tpool_2_count--;
		}else{
			printf("SHOULD NOT GET HERE\n");
		}
	}

	// Handle rest of data threads
	while(tpool1_add_threads != 0){
		if(tpool1_add_threads > 0){
			borrow_threads(ctx->tpool_idle, ctx->tpool_1, 1);
			tpool1_add_threads--;
			ctx->tpool_1_count++;
		}else if(tpool1_add_threads < 0){
			release_borrowed_threads(ctx->tpool_1, 1);
			tpool1_add_threads++;
			ctx->tpool_1_count--;
		}else{
			printf("SHOULD NOT GET HERE\n");
		}
	}

	//printf("Policy done\n");

}

//////////////
// Triggers //
//////////////


/* Simple trigger that activates background scheduler thread
 * every 50 microseconds
 */
void microsecond_timer_trigger(void* args){
	//printf("Gonna sleep\n");
	//usleep(250000);
	  usleep(500000);
}

/* Trigger that waits for thread to post to semaphore
 * stored in main scheduler context
 */
void semaphore_trigger(void* args){
	e2fsck_t ctx = (e2fsck_t) args;
	//sem_wait(&ctx->scheduler_trigger);
	printf("NOT IMPLEMENT SEMAPHORE TRIGGER");
}
#endif

void e2fsck_free_context(e2fsck_t ctx)
{
	if (!ctx)
		return;

	e2fsck_reset_context(ctx);
	if (ctx->blkid)
		blkid_put_cache(ctx->blkid);

	if (ctx->profile)
		profile_release(ctx->profile);

	if (ctx->filesystem_name)
		ext2fs_free_mem(&ctx->filesystem_name);

	if (ctx->device_name)
		ext2fs_free_mem(&ctx->device_name);

	if (ctx->log_fn)
		free(ctx->log_fn);

	if (ctx->logf)
		fclose(ctx->logf);

	ext2fs_free_mem(&ctx);
}

// END POLICIES //////////////////////////////////////////

/*
 * This function runs through the e2fsck passes and calls them all,
 * returning restart, abort, or cancel as necessary...
 */
typedef void (*pass_t)(e2fsck_t ctx);

static pass_t e2fsck_passes[] = {
	e2fsck_pass1, e2fsck_pass1e, e2fsck_pass2, e2fsck_pass3,
	e2fsck_pass4, e2fsck_pass5, 0 };

//////////////////////////////////

void print_optimizations(e2fsck_t ctx){

	// Print optimizations being used
	printf("Using the following Optimizations: ");
	if(!ctx->pass1_opt && !ctx->pass2_opt && 
	   !ctx->pass3_opt && !ctx->pass4_opt && 
	   !ctx->pass5_opt){
		printf("none");
	} else{
		if(ctx->pass1_opt){printf("pass1 ");}
		if(ctx->pass2_opt){printf("pass2 ");}
		if(ctx->pass3_opt){printf("pass3 ");}
		if(ctx->pass4_opt){printf("pass4 ");}
		if(ctx->pass5_opt){printf("pass5 ");}
	}
	printf("\n");


#ifdef _READ_DIR_BLOCKS
	printf("GOing to read directory blocks %d at a time\n", _READ_DIR_BLOCKS);
#endif

}


/* Sets up thread pools
 *  Sets up only 1 thread pool for data/pipeline parallelism but with scheduler
 *  sets up multiple thread pools, one for each pass as well as an IDLE thread 
 *  pool
 */
void setup_thread_pools(e2fsck_t ctx){
	int threads;

#ifdef _SCHEDULER
	if(ctx->scheduler == 1){
		if(ctx->max_threads <= 0){
			ctx->max_threads = get_nprocs();
			ctx->tctx_count = ctx->max_threads;
			printf("[Scheduler] Using Core Awareness, setting max threads to %d (%d cores available)\n", ctx->max_threads, get_nprocs());
			set_pid(ctx->sched, getpid());
			ctx->core_aware = 1;
		}else{
			set_pid(ctx->sched, 0);
		}

		// Setup idle threadpool
		printf("[Thread Pool] Setting up IDLE thread pool.\n");
		ctx->tpool_idle = thpool_init(ctx->max_threads);
		
		// setup first thread pool
		printf("[Thread Pool] Setting up first thread pool.\n");
		ctx->tpool_1 = thpool_init(0);
		ctx->tpool_1_count = 0;

		// setup seconds thread pool
		printf("[Thread Pool] Setting up second thread pool.\n");
		ctx->tpool_2 = thpool_init(0);
		ctx->tpool_2_count = 0;

		return;
	}
#endif

	if(ctx->extra_threads){
		printf("[Thread Pool] Setting up main thread pool with %d threads.\n", ctx->extra_threads);
		ctx->tpool = thpool_init(ctx->extra_threads);
		return;
	}

}


void setup_thread_contexts(e2fsck_t ctx){


	// Allocation Thread Contexts
	printf("Allocating %d Thread Contexts\n", ctx->tctx_count);
	ctx->tctx_array = (struct thread_ctx*) malloc(sizeof(struct thread_ctx)*(ctx->tctx_count));

	// initialize thread keys
	pthread_key_create(&ctx->tctx_key, NULL);
	pthread_key_create(&ctx->fs_key, NULL);
	pfsck_fix_init(ctx);

	// assign, ctx, and fs
	int x;
	for(x = 0; x < ctx->tctx_count; x++){
		ctx->tctx_array[x].ctx = ctx;
		if (x == 0){
			ctx->tctx_array[x].fs = ctx->fs; // let at least one tctx use main fs
		}else{
			ctx->tctx_array[x].fs = ctx->fss[x-1];
		}

		// Set pass1 context ref to null
		ctx->tctx_array[x].pass1_ctx = NULL;
		ctx->tctx_array[x].tid = 0;


	}
	
}

void print_times(e2fsck_t ctx, double *pass_times){
	int i;
	double total_time = 0;

	// Print Out Extra Timing Statistics
	if(ctx->extra_timing){
		printf("\ntime.h timing statistics:\n-------------------------\n");
		for (i = 0; i < 6; i++){
						if(i == 0){
							printf("Pass 1 time: ");
						}else if(i == 1){
							printf("Pass 1e time: ");
						}else{
							printf("Pass %d time: ", i);
						}

#ifdef _PIPELINE_PARALLELISM
						if(i == 0){
							printf("%f seconds\n", ctx->pass1_time);
							total_time += ctx->pass1_time;
						}else if (i == 2){
							printf("%f seconds\n", ctx->pass2_time);
							total_time += ctx->pass2_time;
						}else {
							printf("%f seconds\n", pass_times[i]);
							total_time += pass_times[i];
						}
#else
						printf("%f seconds\n", pass_times[i]);
						total_time += pass_times[i];
#endif

		}
	}
}
//////////////////////////////////

int e2fsck_run(e2fsck_t ctx)
{
	int	i;
	pass_t	e2fsck_pass;

	// Timers for time.h timing statistics
	struct timeval total_start, total_stop;
	struct timeval start, stop;
	double pass_times[6];

	// print optimizations
	print_optimizations(ctx);

	//ctx->single_thread = 0;

	//if(continue_single_threaded){
	//	printf("setting up as single Threaded continuation!\n");
	//	ctx->data_threads = 1;
	//	ctx->extra_threads = 0;
	//	ctx->pipeline_threads = 0;
	//	ctx->scheduler = 0;
	//}

#ifdef _SCHEDULER
	// PRORITIZES SCHEDULER
	if(ctx->scheduler == 1){
		printf("[SCHEDULER] Utilizing Scheduler, setting up threads automatically\n");
	
		//initialize scheduler
		initialize_scheduler(&ctx->sched);
		set_policy(ctx->sched, policy);
		set_trigger(ctx->sched, microsecond_timer_trigger);
		set_context(ctx->sched, ctx);

		// Setup numbers of threads
		ctx->tctx_count = ctx->max_threads;

		goto continue_setup_threads;
	}
#endif

	// if no scheduler setup via extra threads 
	//if(ctx->extra_threads){
	ctx->tctx_count = ctx->extra_threads + 1;	
	//}


continue_setup_threads:

	// If we are using any sort of parallelism, setup threads
	if(ctx->data_threads || ctx->extra_threads 
#ifdef _SCHEDULER
			|| ctx->max_threads
#endif	
			){
		setup_thread_pools(ctx);
		setup_thread_contexts(ctx);
#ifdef _PIPELINE_PARALLELISM
		setup_pass2_threads(ctx, ctx->pipeline_threads);// we can take this out maybe have this set up when threads starts up????
#endif

		// setup concurrent dx_dirinfo
		printf("Setting up concurrent dx_dirinfo\n");
#ifdef _PIPELINE_PARALLELISM
		e2fsck_init_dx_dir(ctx);
#endif
	}

	/* setup main statistics */
	if(ctx->extra_output){
		ctx->total_groups_checked = 0;
		ctx->total_inodes_checked = 0;
		ctx->total_blocks_checked = 0;
	}


#ifdef HAVE_SETJMP_H
	if (setjmp(ctx->abort_loc)) {
		ctx->flags &= ~E2F_FLAG_SETJMP_OK;
		return (ctx->flags & E2F_FLAG_RUN_RETURN);
	}
	ctx->flags |= E2F_FLAG_SETJMP_OK;
#endif

	gettimeofday(&total_start, NULL);

	for (i=0; (e2fsck_pass = e2fsck_passes[i]); i++) {
		if (ctx->flags & E2F_FLAG_RUN_RETURN)
			break;
		if (e2fsck_mmp_update(ctx->fs))
			fatal_error(ctx, 0);

#ifdef _PIPELINE_PARALLELISM

		//if(i == 2 && ctx->pipeline_threads){
		//	printf("Waiting for check dir blocks to finish\n");
		//	thpool_wait(ctx->tpool);
		//}

#endif


		gettimeofday(&start, NULL); // get start clock
		e2fsck_pass(ctx);
		gettimeofday(&stop, NULL); // get end clock
    pass_times[i] = ((double) (1000000 * (stop.tv_sec - start.tv_sec) + stop.tv_usec - start.tv_usec) / 1000000);

    		// Temporary: disable all pfsck hooks for passes past pass 2
		if (i == 2){
			pfsck_disable_fix(ctx, 99999);
		}

next_pass:
		if (ctx->progress)
			(void) (ctx->progress)(ctx, 0, 0, 0);
	}

	gettimeofday(&total_stop, NULL);


#ifdef _PIPELINE_PARALLELISM

	// Free allocated structures
	breakdown_pass2_threads();

#endif


	// Break down Thread Pool:
	if(ctx->extra_threads){
		thpool_destroy(ctx->tpool);
	}

	// Free thread contexts
	free(ctx->tctx_array);

	// print timing statistics
	print_times(ctx, &pass_times[0]);
		printf("Total Time: %f\n", ((double) (1000000 * (total_stop.tv_sec - total_start.tv_sec) + total_stop.tv_usec - total_start.tv_usec) / 1000000));




	ctx->flags &= ~E2F_FLAG_SETJMP_OK;

	if (ctx->flags & E2F_FLAG_RUN_RETURN)
		return (ctx->flags & E2F_FLAG_RUN_RETURN);
	return 0;
}
