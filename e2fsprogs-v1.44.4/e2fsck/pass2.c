/*
 * pass2.c --- check directory structure
 *
 * Copyright (C) 1993, 1994, 1995, 1996, 1997 Theodore Ts'o
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 *
 * Pass 2 of e2fsck iterates through all active directory inodes, and
 * applies to following tests to each directory entry in the directory
 * blocks in the inodes:
 *
 *	- The length of the directory entry (rec_len) should be at
 * 		least 8 bytes, and no more than the remaining space
 * 		left in the directory block.
 * 	- The length of the name in the directory entry (name_len)
 * 		should be less than (rec_len - 8).
 *	- The inode number in the directory entry should be within
 * 		legal bounds.
 * 	- The inode number should refer to a in-use inode.
 *	- The first entry should be '.', and its inode should be
 * 		the inode of the directory.
 * 	- The second entry should be '..'.
 *
 * To minimize disk seek time, the directory blocks are processed in
 * sorted order of block numbers.
 *
 * Pass 2 also collects the following information:
 * 	- The inode numbers of the subdirectories for each directory.
 *
 * Pass 2 relies on the following information from previous passes:
 * 	- The directory information collected in pass 1.
 * 	- The inode_used_map bitmap
 * 	- The inode_bad_map bitmap
 * 	- The inode_dir_map bitmap
 *
 * Pass 2 frees the following data structures
 * 	- The inode_bad_map bitmap
 * 	- The inode_reg_map bitmap
 */

#define _GNU_SOURCE 1 /* get strnlen() */
#include "config.h"
#include <string.h>

#include "e2fsck.h"
#include "problem.h"
#include "support/dict.h"

#include <sys/syscall.h>

#ifdef NO_INLINE_FUNCS
#define _INLINE_
#else
#define _INLINE_ inline
#endif

/* #define DX_DEBUG */

/*
 * Keeps track of how many times an inode is referenced.
 */
static void deallocate_inode(e2fsck_t ctx, ext2_ino_t ino, char* block_buf);
static int check_dir_block2(ext2_filsys fs,
			   struct ext2_db_entry2 *dir_blocks_info,
			   void *priv_data);
static int check_dir_block(ext2_filsys fs,
			   struct ext2_db_entry2 *dir_blocks_info,
			   void *priv_data);
static int allocate_dir_block(e2fsck_t ctx,
			      struct ext2_db_entry2 *dir_blocks_info,
			      char *buf, struct problem_context *pctx);
static void clear_htree(e2fsck_t ctx, ext2_ino_t ino);
static int htree_depth(struct dx_dir_info *dx_dir,
		       struct dx_dirblock_info *dx_db);
static EXT2_QSORT_TYPE special_dir_block_cmp(const void *a, const void *b);

struct check_dir_struct {
	char *buf;
	struct problem_context	pctx;
	int	count, max;
	e2fsck_t ctx;
	unsigned long long list_offset;
	unsigned long long ra_entries;
	unsigned long long next_ra_off;

	struct dir_info_db* dir_info_ptr; // pointer to dir_info

};

static void update_parents(struct dx_dir_info *dx_dir, int type)
{
	struct dx_dirblock_info *dx_db, *dx_parent, *dx_previous;
	int b;

	for (b = 0, dx_db = dx_dir->dx_block;
	     b < dx_dir->numblocks;
	     b++, dx_db++) {
		dx_parent = &dx_dir->dx_block[dx_db->parent];
		if (dx_db->type != type)
			continue;

		/*
		 * XXX Make sure dx_parent->min_hash > dx_db->min_hash
		*/
		if (dx_db->flags & DX_FLAG_FIRST) {
			dx_parent->min_hash = dx_db->min_hash;
			if (dx_parent->previous) {
				dx_previous =
					&dx_dir->dx_block[dx_parent->previous];
				dx_previous->node_max_hash =
					dx_parent->min_hash;
			}
		}
		/*
		 * XXX Make sure dx_parent->max_hash < dx_db->max_hash
		 */
		if (dx_db->flags & DX_FLAG_LAST) {
			dx_parent->max_hash = dx_db->max_hash;
		}
	}
}


/*
 * Directory block iterator definition
 */
struct ext2_struct_dblist {
	int			magic;
	ext2_filsys		fs;
	unsigned long long	size;
	unsigned long long	count;
	int			sorted;
	struct ext2_db_entry2 *	list;

#ifdef _PIPELINE_PARALLELISM
	unsigned long long last_checked;
	fsck_lock_t dblist_lock;
	pthread_rwlock_t  rwlock;
#endif
};

/*	Context struct for threads checking blocks
 *	hold references to structures needed to check blocks as well as
 *  arguments to iterate over only a subsection of the dblist
 */
struct check_blocks_ctx{

	// Common information
	struct check_dir_struct cd;	// check_dir_struct
	e2fsck_t ctx;								// main context pointer

	// Task information
	struct ext2_struct_dblist dblist;	// directory block list
	ext2_dblist dblist_ptr;
	struct dir_info_db* dir_info_ptr; // pointer to dir_info
	int tid;										// thread id
	int num_blocks;							// number of blocks to process for this thread
	int total_blocks;						// total number of blocks in dblist
	int stride;
	int start;
	int end;

#ifdef _PIPELINE_PARALLELISM
	ext2_filsys fs;
#endif

};

/*	Thread function that applies check_dir_block function to a
 *	subsection of the dblist
 */
void check_blocks_thread(void* args){

	// Timers for time.h statistics
	struct timeval start_t, stop_t;

	// Start timer
	gettimeofday(&start_t,NULL);

	// Cast Context
	struct check_blocks_ctx* context = (struct check_blocks_ctx*) args;

	// Calculate number of to process
	int start = context->tid * context->num_blocks;
	int end = start + context->num_blocks;
	int count = context->num_blocks;
	if(end > context->total_blocks){
		count -= end - context->total_blocks;
		end = context->total_blocks;
	}


	// start syncing to fixes
	pfsck_sync_start(context->ctx);

#ifdef _PASS2_STRIDE
	// STRIDE
	 context->cd.pctx.errcode = ext2fs_dblist_iterate2_stride(&context->dblist, check_dir_block, &context->cd, context->stride, context->tid);

#else
	// CONTIGUOUS
	// Apply check_dir_block to subsection of dblist
	context->cd.pctx.errcode = ext2fs_dblist_iterate3(&context->dblist, check_dir_block, start, count, &context->cd);
#endif

	// stop syncing to fixes
	pfsck_sync_stop(context->ctx);

	// Clean up context buffer
	ext2fs_free_mem(&context->cd.buf);

	// End timer
	gettimeofday(&stop_t,NULL);

	// Print extra things from this thread
	if(context->ctx->extra_output){
		printf("Thread %d ran in %f seconds.\n", context->tid, ((double) (1000000 * (stop_t.tv_sec - start_t.tv_sec) + stop_t.tv_usec - start_t.tv_usec) / 1000000));
	}

}

int update_count = 0;
int total_blocks_checked = 0;

#ifdef _PIPELINE_PARALLELISM


struct update_parent_task{
	ext2_ino_t dir_ino;
	ext2_ino_t parent_ino;
};

struct update_parent_task* update_tasks;
int update_size = 0;
int size_jump = 8000001;
fsck_lock_t update_tasks_lock;

int add_update_count = 0;

int add_update_parent_task(ext2_ino_t dir_ino, ext2_ino_t parent_ino){

	fsck_lock(&update_tasks_lock);

	// inital list malloc
	if(update_size == 0){
		update_tasks = (struct update_parent_task*) malloc(sizeof(struct update_parent_task) * size_jump);
		update_size += size_jump;
	}else if (update_count == update_size){
		//resize
		update_tasks = (struct update_parent_task*) realloc(update_tasks, sizeof(struct update_parent_task) * (update_size + size_jump));
		update_size += size_jump;
	}

	// add to list
	int index = update_count;
	update_tasks[index].dir_ino = dir_ino;
	update_tasks[index].parent_ino = parent_ino;
	//fsck_lock(&update_tasks_lock);
	update_count++;
	//fsck_unlock(&update_tasks_lock);

	fsck_unlock(&update_tasks_lock);
}

int do_update_parent_task(e2fsck_t ctx, struct update_parent_task* update_task, struct check_dir_struct* cd){
	int subdir_parent;

	// If directory is not marked in bitmap, (then idk?)
	if(!ext2fs_test_inode_bitmap2(ctx->inode_dir_map, update_task->dir_ino)){
		// Should not get here unless directory was cleaned up?
		return;
	}

	// If dir_info of parent was not created.
	if (e2fsck_dir_info_get_parent(ctx, update_task->dir_ino, &subdir_parent)) {
		cd->pctx.ino = update_task->dir_ino;
		fix_problem(ctx, PR_2_NO_DIRINFO, &cd->pctx);
		//goto abort_free_dict;
		// DO SOMETHING  <--fill in
	}
	// If subdir parent is not null;
	if (subdir_parent) {
		cd->pctx.ino2 = 0;
	} 
		// Update parent
		(void) e2fsck_dir_info_set_parent(ctx,
					update_task->dir_ino, update_task->parent_ino);
}

int process_update_tasks_range(e2fsck_t ctx, struct update_parent_task* tasks, int start, int end, struct check_dir_struct* cd){
	int x;

	for(x = start; x <= end; x++){
		do_update_parent_task(ctx, &tasks[x], cd);
	}
	return 0;
}

int free_parent_tasks(){
	free(update_tasks);
}

struct check_blocks_ctx* get_blocks_ctx(e2fsck_t ctx){
 	struct check_blocks_ctx *ret;
 	ret = (struct check_blocks_ctx*) malloc(sizeof(struct check_blocks_ctx));

 	// allocate buffer

#ifdef _READ_DIR_BLOCKS
	ret->cd.buf = (char *) e2fsck_allocate_memory(ctx, _READ_DIR_BLOCKS*ctx->fs->blocksize + 1, "directory scan buffer");
#else
 	ret->cd.buf = (char *) e2fsck_allocate_memory(ctx, 2*ctx->fs->blocksize, "directory scan buffer");
#endif

	// set common information
	ret->ctx = ctx;

	ret->cd.ctx = ctx;

	return ret;
}

void destroy_check_blocks_ctx(struct check_blocks_ctx* bctx){
	//free buffer
	ext2fs_free_mem(&bctx->cd.buf);

	// free struct
	free(bctx);
}

int start_checking = 0;

void check_blocks_task(void* args){

	// Cast void args
	struct check_blocks_ctx *context = (struct check_blocks_ctx *) args;

	int count = context->end - context->start;

	total_blocks_checked += count;

	struct thread_ctx *tctx = get_tctx(context->ctx);
	context->fs = tctx->fs;


	context->cd.pctx.errcode = ext2fs_dblist_iterate4(context->dblist_ptr, check_dir_block, context->start, count, &context->cd, context->fs);

	destroy_check_blocks_ctx(context);

}

int fs_counter = 0;
fsck_lock_t fs_lock;

void process_db_list_so_far(e2fsck_t ctx, ext2_dblist dblist, struct dir_info_db* dir_info){

	if(ctx->pipeline_threads == 0){
		return;
	}

	if(dblist->count == dblist->last_checked){
		return;
	}

	struct check_blocks_ctx* context = get_blocks_ctx(ctx);

	context->start = dblist->last_checked;
	context->end = dblist->count;
	dblist->last_checked = dblist->count;
	context->dblist_ptr = dblist;
	context->dir_info_ptr = dir_info;
	context->cd.dir_info_ptr = dir_info;
	
	// Use a seperate DBList	
	int i;
	int offset = context->start;
	int count = context->end - context->start;
	ext2_dblist dblistcpy;
	make_dblist(dblist->fs, count, count, 0, &dblistcpy);
	memcpy(dblistcpy->list, &dblist->list[context->start], sizeof(struct ext2_db_entry2) * count);
	

	context->end = count;
	context->start = 0;
	context->dblist_ptr = dblistcpy;
	
#ifdef _SCHEDULER
	if(ctx->scheduler){
		thpool_add_work(ctx->tpool_2, (void*) check_blocks_task, (void*) context);
	}else{
		thpool_add_work(ctx->tpool, (void*) check_blocks_task, (void*) context);
	}
#else
	thpool_add_work(ctx->tpool, (void*) check_blocks_task, (void*) context);
#endif
}

void e2fsck_pass2_task(void* args){
	printf("Got task");
}

void setup_pass2_threads(e2fsck_t ctx, int count){
	int x;

	// setup icount (note: hint is NULL)
	e2fsck_setup_icount(ctx, "inode_count",
				EXT2_ICOUNT_OPT_INCREMENT,
				NULL, &ctx->inode_count);

#ifdef _ICOUNT_MERGE_B
	for(x = 0; x < ctx->extra_threads; x++){
		e2fsck_setup_icount(ctx, "inode_count",
				EXT2_ICOUNT_OPT_INCREMENT,
				NULL, &ctx->tctx_array[x].inode_count);
		ctx->tctx_array[x].fs_links_count = 0;
		ctx->tctx_array[x].fs_total_count = 0;
	}
#endif

}

void aggregate_pass2_threads(e2fsck_t ctx){

#ifdef _ICOUNT_MERGE_B
	int x;
	struct thread_ctx* tctx;

	for (x = 0; x < ctx->extra_threads; x++){
		tctx = &ctx->tctx_array[x];
//#ifdef _ICOUNT_MERGE
		ext2fs_icount_merge(tctx->inode_count,
				ctx->inode_count);
		ext2fs_free_icount(tctx->inode_count);
//#endif
		ctx->fs_links_count += tctx->fs_links_count;
		ctx->fs_total_count += tctx->fs_total_count;
	}
#endif


}	

void breakdown_pass2_threads(){
	printf("Breaking down pass 2 threads\n");
}

#endif


static const char *read_dir_block_string;

void e2fsck_pass2(e2fsck_t ctx)
{
	printf("Running Pass 2\n");
	struct timeval time_start, time_stop;
	gettimeofday(&time_start, NULL); // get start clock

	struct ext2_super_block *sb = ctx->fs->super;
	struct problem_context	pctx;
	ext2_filsys 		fs = ctx->fs;
	char			*buf = NULL;
#ifdef RESOURCE_TRACK
	struct resource_track	rtrack;
#endif
	struct check_dir_struct cd;
	struct dx_dir_info	*dx_dir;
	struct dx_dirblock_info	*dx_db;
	int			b;
	int			i, depth;
	problem_t		code;
	int			bad_dir;
	int (*check_dir_func)(ext2_filsys fs,
			      struct ext2_db_entry2 *dir_blocks_info,
			      void *priv_data);

	// need to set it null
	cd.dir_info_ptr = NULL;

	init_resource_track(&rtrack, ctx->fs->io);
	clear_problem_context(&cd.pctx);

#ifdef MTRACE
	mtrace_print("Pass 2");
#endif

	// Disable any pfsck_fix syncing for intial fix problem
	pfsck_disable_fix(ctx, 1);

	if (!(ctx->options & E2F_OPT_PREEN))
		fix_problem(ctx, PR_2_PASS_HEADER, &cd.pctx);

#ifdef _PIPELINE_PARALLELISM
	if(!ctx->pipeline_threads){
		cd.pctx.errcode = e2fsck_setup_icount(ctx, "inode_count",
					EXT2_ICOUNT_OPT_INCREMENT,
					ctx->inode_link_info, &ctx->inode_count);
	}
#else
	// make icount for serial or data parallel
	cd.pctx.errcode = e2fsck_setup_icount(ctx, "inode_count",
				EXT2_ICOUNT_OPT_INCREMENT,
				ctx->inode_link_info, &ctx->inode_count);
#endif

	if (cd.pctx.errcode) {
		fix_problem(ctx, PR_2_ALLOCATE_ICOUNT, &cd.pctx);
		ctx->flags |= E2F_FLAG_ABORT;
		goto cleanup;
	}

#ifdef _READ_DIR_BLOCKS
	buf = (char *) e2fsck_allocate_memory(ctx, _READ_DIR_BLOCKS*ctx->fs->blocksize + 1, "directory scan buffer");
#else
	buf = (char *) e2fsck_allocate_memory(ctx, 2*fs->blocksize,
					      "directory scan buffer");
#endif

	/*
	 * Set up the parent pointer for the root directory, if
	 * present.  (If the root directory is not present, we will
	 * create it in pass 3.)
	 */
	(void) e2fsck_dir_info_set_parent(ctx, EXT2_ROOT_INO, EXT2_ROOT_INO);

	cd.buf = buf;
	cd.ctx = ctx;
	cd.count = 1;
	cd.max = ext2fs_dblist_count2(fs->dblist);
	cd.list_offset = 0;
	cd.ra_entries = ctx->readahead_kb * 1024 / ctx->fs->blocksize;
	cd.next_ra_off = 0;

	if (ctx->progress)
		(void) (ctx->progress)(ctx, 2, 0, cd.max);

#ifndef _PIPELINE_PARALLELISM
	if (ext2fs_has_feature_dir_index(fs->super))
		ext2fs_dblist_sort2(fs->dblist, special_dir_block_cmp);
#else
	if (ext2fs_has_feature_dir_index(fs->super) & !ctx->pipeline_threads)
		ext2fs_dblist_sort2(fs->dblist, special_dir_block_cmp);
#endif

	check_dir_func = cd.ra_entries ? check_dir_block2 : check_dir_block;

#ifdef _REDUCE_OP_STRINGS
	// Get translation once
	read_dir_block_string = _("reading directory block");
#endif

	//printf("Blocks checked by threads: %d\n", total_blocks_checked);
	//printf("Blocks in dblist %d\n", ctx->fs->dblist->count);

#ifdef _PIPELINE_PARALLELISM

#ifdef _SCHEDULER
	if(ctx->pipeline_threads || ctx->scheduler){
#else
	if(ctx->pipeline_threads){
#endif
		
		aggregate_pass2_threads(ctx);
		
		process_update_tasks_range(ctx, update_tasks, 0, update_count - 1, &cd);

		free_parent_tasks();

		goto after_db_iterate;
	}

#endif

	if(ctx->pass2_opt && ctx->data_threads){

		// Store the number of threads
		int number_of_threads = ctx->data_threads;

		// Allocate contexts
		struct check_blocks_ctx* check_blocks_contexts = (struct check_blocks_ctx*) malloc(sizeof(struct check_blocks_ctx) * number_of_threads);

		// Calculate number of blocks to check per thread
		int total_number_of_blocks = ext2fs_dblist_count(fs->dblist);
		int blocks_per_thread = total_number_of_blocks / number_of_threads;
		if(total_number_of_blocks % number_of_threads > 0){
			blocks_per_thread++;
		}

		// Set up thread contexts
		int x;

#ifdef _ICOUNT_MERGE_B	
		for(x = 0; x < ctx->extra_threads; x++){
			e2fsck_setup_icount(ctx, "inode_count",
					EXT2_ICOUNT_OPT_INCREMENT,
					NULL, &ctx->tctx_array[x].inode_count);
			ctx->tctx_array[x].fs_links_count = 0;
			ctx->tctx_array[x].fs_total_count = 0;
		}
#endif

		for(x = 0; x < number_of_threads; x++){
			check_blocks_contexts[x].tid = x;
			check_blocks_contexts[x].num_blocks = blocks_per_thread;
			check_blocks_contexts[x].total_blocks = total_number_of_blocks;
			memcpy(&check_blocks_contexts[x].dblist, fs->dblist, sizeof(struct ext2_struct_dblist));
			
			struct thread_ctx* tctx = get_tctx(ctx);
			check_blocks_contexts[x].dblist.fs = tctx->fs;
			
			//check_blocks_contexts[x].dblist
			check_blocks_contexts[x].ctx = ctx;
			check_blocks_contexts[x].stride = number_of_threads;
			memcpy(&(check_blocks_contexts[x].cd), &cd, sizeof(struct check_dir_struct));
#ifdef _READ_DIR_BLOCKS
			check_blocks_contexts[x].cd.buf= (char *) e2fsck_allocate_memory(ctx, _READ_DIR_BLOCKS*ctx->fs->blocksize + 1, "directory scan buffer");
#else
			check_blocks_contexts[x].cd.buf = (char *) e2fsck_allocate_memory(ctx, 2*fs->blocksize, "directory scan buffer");
#endif
			check_blocks_contexts[x].cd.dir_info_ptr = NULL;
		}

		// Dispatch Work
		for(x = 0; x < number_of_threads -1; x++){
			thpool_add_work(ctx->tpool, check_blocks_thread, &check_blocks_contexts[x]);
		}
		check_blocks_thread(&check_blocks_contexts[number_of_threads - 1]);

		// Wait for Threads To Finish
		if(ctx->tpool){
			thpool_wait(ctx->tpool);
		}

#ifdef _PIPELINE_PARALLELISM
		// Aggregate Results
		aggregate_pass2_threads(ctx);
#endif 

		for(x = 0; x < number_of_threads; x++){
			if(check_blocks_contexts[x].cd.pctx.errcode){
				cd.pctx.errcode = check_blocks_contexts[x].cd.pctx.errcode;
				break;
			}
		}

		// Clean Up Thread Contexts
		free(check_blocks_contexts); // clean up all check structs

		// Jump to after original dblist_iterate
		goto after_db_iterate;
	}

	// Regular  db iterate call
	cd.pctx.errcode = ext2fs_dblist_iterate2(fs->dblist, check_dir_func,
						 &cd);


after_db_iterate:
	if (ctx->flags & E2F_FLAG_RESTART_LATER) {
		ctx->flags |= E2F_FLAG_RESTART;
		ctx->flags &= ~E2F_FLAG_RESTART_LATER;
	}

	if (ctx->flags & E2F_FLAG_RUN_RETURN)
		goto cleanup;

	if (cd.pctx.errcode) {
		fix_problem(ctx, PR_2_DBLIST_ITERATE, &cd.pctx);
		ctx->flags |= E2F_FLAG_ABORT;
		goto cleanup;
	}

	for (i=0; (dx_dir = e2fsck_dx_dir_info_iter(ctx, &i)) != 0;) {
		if (ctx->flags & E2F_FLAG_SIGNAL_MASK)
			goto cleanup;
		if (e2fsck_dir_will_be_rehashed(ctx, dx_dir->ino) ||
		    dx_dir->numblocks == 0)
			continue;
		clear_problem_context(&pctx);
		bad_dir = 0;
		pctx.dir = dx_dir->ino;
		dx_db = dx_dir->dx_block;
		if (dx_db->flags & DX_FLAG_REFERENCED)
			dx_db->flags |= DX_FLAG_DUP_REF;
		else
			dx_db->flags |= DX_FLAG_REFERENCED;
		/*
		 * Find all of the first and last leaf blocks, and
		 * update their parent's min and max hash values
		 */
		update_parents(dx_dir, DX_DIRBLOCK_LEAF);

		/* for 3 level htree: update 2 level parent's min
		 * and max hash values */
		update_parents(dx_dir, DX_DIRBLOCK_NODE);

		for (b=0, dx_db = dx_dir->dx_block;
		     b < dx_dir->numblocks;
		     b++, dx_db++) {
			pctx.blkcount = b;
			pctx.group = dx_db->parent;
			code = 0;
			if (!(dx_db->flags & DX_FLAG_FIRST) &&
			    (dx_db->min_hash < dx_db->node_min_hash)) {
				pctx.blk = dx_db->min_hash;
				pctx.blk2 = dx_db->node_min_hash;
				code = PR_2_HTREE_MIN_HASH;
				fix_problem(ctx, code, &pctx);
				bad_dir++;
			}
			if (dx_db->type == DX_DIRBLOCK_LEAF) {
				depth = htree_depth(dx_dir, dx_db);
				if (depth != dx_dir->depth) {
					pctx.num = dx_dir->depth;
					code = PR_2_HTREE_BAD_DEPTH;
					fix_problem(ctx, code, &pctx);
					bad_dir++;
				}
			}
			/*
			 * This test doesn't apply for the root block
			 * at block #0
			 */
			if (b &&
			    (dx_db->max_hash > dx_db->node_max_hash)) {
				pctx.blk = dx_db->max_hash;
				pctx.blk2 = dx_db->node_max_hash;
				code = PR_2_HTREE_MAX_HASH;
				fix_problem(ctx, code, &pctx);
				bad_dir++;
			}
			if (!(dx_db->flags & DX_FLAG_REFERENCED)) {
				code = PR_2_HTREE_NOTREF;
				fix_problem(ctx, code, &pctx);
				bad_dir++;
			} else if (dx_db->flags & DX_FLAG_DUP_REF) {
				code = PR_2_HTREE_DUPREF;
				fix_problem(ctx, code, &pctx);
				bad_dir++;
			}
		}
		if (bad_dir && fix_problem(ctx, PR_2_HTREE_CLEAR, &pctx)) {
			clear_htree(ctx, dx_dir->ino);
			dx_dir->numblocks = 0;
		}
	}
	e2fsck_free_dx_dir_info(ctx);

	ext2fs_free_mem(&buf);
	ext2fs_free_dblist(fs->dblist);

	if (ctx->inode_bad_map) {
		ext2fs_free_inode_bitmap(ctx->inode_bad_map);
		ctx->inode_bad_map = 0;
	}
	if (ctx->inode_reg_map) {
		ext2fs_free_inode_bitmap(ctx->inode_reg_map);
		ctx->inode_reg_map = 0;
	}
	if (ctx->encrypted_dirs) {
		ext2fs_u32_list_free(ctx->encrypted_dirs);
		ctx->encrypted_dirs = 0;
	}

	clear_problem_context(&pctx);
	if (ctx->large_files) {
		if (!ext2fs_has_feature_large_file(sb) &&
		    fix_problem(ctx, PR_2_FEATURE_LARGE_FILES, &pctx)) {
			ext2fs_set_feature_large_file(sb);
			fs->flags &= ~EXT2_FLAG_MASTER_SB_ONLY;
			ext2fs_mark_super_dirty(fs);
		}
		if (sb->s_rev_level == EXT2_GOOD_OLD_REV &&
		    fix_problem(ctx, PR_1_FS_REV_LEVEL, &pctx)) {
			ext2fs_update_dynamic_rev(fs);
			ext2fs_mark_super_dirty(fs);
		}
	}

	print_resource_track(ctx, _("Pass 2"), &rtrack, fs->io);
cleanup:
	ext2fs_free_mem(&buf);

	gettimeofday(&time_stop, NULL); // get end clock
	ctx->pass2_time = ((double) (1000000 * (time_stop.tv_sec - time_start.tv_sec)\
										 + time_stop.tv_usec - time_start.tv_usec) / 1000000);
}

#define MAX_DEPTH 32000
static int htree_depth(struct dx_dir_info *dx_dir,
		       struct dx_dirblock_info *dx_db)
{
	int	depth = 0;

	while (dx_db->type != DX_DIRBLOCK_ROOT && depth < MAX_DEPTH) {
		dx_db = &dx_dir->dx_block[dx_db->parent];
		depth++;
	}
	return depth;
}

static int dict_de_cmp(const void *a, const void *b)
{
	const struct ext2_dir_entry *de_a, *de_b;
	int	a_len, b_len;

	de_a = (const struct ext2_dir_entry *) a;
	a_len = ext2fs_dirent_name_len(de_a);
	de_b = (const struct ext2_dir_entry *) b;
	b_len = ext2fs_dirent_name_len(de_b);

	if (a_len != b_len)
		return (a_len - b_len);

	return memcmp(de_a->name, de_b->name, a_len);
}

/*
 * This is special sort function that makes sure that directory blocks
 * with a dirblock of zero are sorted to the beginning of the list.
 * This guarantees that the root node of the htree directories are
 * processed first, so we know what hash version to use.
 */
static EXT2_QSORT_TYPE special_dir_block_cmp(const void *a, const void *b)
{
	const struct ext2_db_entry2 *db_a =
		(const struct ext2_db_entry2 *) a;
	const struct ext2_db_entry2 *db_b =
		(const struct ext2_db_entry2 *) b;

	if (db_a->blockcnt && !db_b->blockcnt)
		return 1;

	if (!db_a->blockcnt && db_b->blockcnt)
		return -1;

	if (db_a->blk != db_b->blk)
		return (int) (db_a->blk - db_b->blk);

	if (db_a->ino != db_b->ino)
		return (int) (db_a->ino - db_b->ino);

	return (int) (db_a->blockcnt - db_b->blockcnt);
}


/*
 * Make sure the first entry in the directory is '.', and that the
 * directory entry is sane.
 */
static int check_dot(e2fsck_t ctx,
		     struct ext2_dir_entry *dirent,
		     ext2_ino_t ino, struct problem_context *pctx)
{
	struct ext2_dir_entry *nextdir;
	unsigned int	rec_len, new_len;
	int		status = 0;
	int		created = 0;
	problem_t	problem = 0;

	if (!dirent->inode)
		problem = PR_2_MISSING_DOT;
	else if ((ext2fs_dirent_name_len(dirent) != 1) ||
		 (dirent->name[0] != '.'))
		problem = PR_2_1ST_NOT_DOT;
	else if (dirent->name[1] != '\0')
		problem = PR_2_DOT_NULL_TERM;

	(void) ext2fs_get_rec_len(ctx->fs, dirent, &rec_len);
	if (problem) {
		if (fix_problem(ctx, problem, pctx)) {
			if (rec_len < 12)
				rec_len = dirent->rec_len = 12;
			dirent->inode = ino;
			ext2fs_dirent_set_name_len(dirent, 1);
			ext2fs_dirent_set_file_type(dirent, EXT2_FT_UNKNOWN);
			dirent->name[0] = '.';
			dirent->name[1] = '\0';
			status = 1;
			created = 1;
		}
	}
	if (dirent->inode != ino) {
		if (fix_problem(ctx, PR_2_BAD_INODE_DOT, pctx)) {
			dirent->inode = ino;
			status = 1;
		}
	}
	if (rec_len > 12) {
		new_len = rec_len - 12;
		if (new_len > 12) {
			if (created ||
			    fix_problem(ctx, PR_2_SPLIT_DOT, pctx)) {
				nextdir = (struct ext2_dir_entry *)
					((char *) dirent + 12);
				dirent->rec_len = 12;
				(void) ext2fs_set_rec_len(ctx->fs, new_len,
							  nextdir);
				nextdir->inode = 0;
				ext2fs_dirent_set_name_len(nextdir, 0);
				ext2fs_dirent_set_file_type(nextdir,
							    EXT2_FT_UNKNOWN);
				status = 1;
			}
		}
	}
	return status;
}

/* old check_dotdot
 * Make sure the second entry in the directory is '..', and that the
 * directory entry is sane.  We do not check the inode number of '..'
 * here; this gets done in pass 3.
 */
static int check_dotdot(e2fsck_t ctx,
			struct ext2_dir_entry *dirent,
			ext2_ino_t ino, struct problem_context *pctx, struct dir_info_db* dir_info)
{
	problem_t	problem = 0;
	unsigned int	rec_len;

	if (!dirent->inode)
		problem = PR_2_MISSING_DOT_DOT;
	else if ((ext2fs_dirent_name_len(dirent) != 2) ||
		 (dirent->name[0] != '.') ||
		 (dirent->name[1] != '.'))
		problem = PR_2_2ND_NOT_DOT_DOT;
	else if (dirent->name[2] != '\0')
		problem = PR_2_DOT_DOT_NULL_TERM;

	(void) ext2fs_get_rec_len(ctx->fs, dirent, &rec_len);
	if (problem) {
		if (fix_problem(ctx, problem, pctx)) {
			if (rec_len < 12)
				dirent->rec_len = 12;
			/*
			 * Note: we don't have the parent inode just
			 * yet, so we will fill it in with the root
			 * inode.  This will get fixed in pass 3.
			 */
			dirent->inode = EXT2_ROOT_INO;
			ext2fs_dirent_set_name_len(dirent, 2);
			ext2fs_dirent_set_file_type(dirent, EXT2_FT_UNKNOWN);
			dirent->name[0] = '.';
			dirent->name[1] = '.';
			dirent->name[2] = '\0';
			return 1;
		}
		return 0;
	}

	if(dir_info){
		if (e2fsck_dir_info_set_dotdot_directly(ctx, ino, dirent->inode, dir_info)) {
			fix_problem(ctx, PR_2_NO_DIRINFO, pctx);
			return -1;
		}
	} else {
		if (e2fsck_dir_info_set_dotdot(ctx, ino, dirent->inode)) {
			fix_problem(ctx, PR_2_NO_DIRINFO, pctx);
			return -1;
		}
	}
	return 0;
}

/*
 * Check to make sure a directory entry doesn't contain any illegal
 * characters.
 */
static int check_name(e2fsck_t ctx,
		      struct ext2_dir_entry *dirent,
		      struct problem_context *pctx)
{
	int	i;
	int	fixup = -1;
	int	ret = 0;

	for ( i = 0; i < ext2fs_dirent_name_len(dirent); i++) {
		if (dirent->name[i] != '/' && dirent->name[i] != '\0')
			continue;
		if (fixup < 0)
			fixup = fix_problem(ctx, PR_2_BAD_NAME, pctx);
		if (fixup == 0)
			return 0;
		dirent->name[i] = '.';
		ret = 1;
	}
	return ret;
}

static int encrypted_check_name(e2fsck_t ctx,
				struct ext2_dir_entry *dirent,
				struct problem_context *pctx)
{
	if (ext2fs_dirent_name_len(dirent) < EXT4_CRYPTO_BLOCK_SIZE) {
		if (fix_problem(ctx, PR_2_BAD_ENCRYPTED_NAME, pctx)) {
			dirent->inode = 0;
			return 1;
		}
		ext2fs_unmark_valid(ctx->fs);
	}
	return 0;
}

/*
 * Check the directory filetype (if present)
 */
static _INLINE_ int check_filetype(e2fsck_t ctx,
				   struct ext2_dir_entry *dirent,
				   ext2_ino_t dir_ino EXT2FS_ATTR((unused)),
				   struct problem_context *pctx)
{
	int	filetype = ext2fs_dirent_file_type(dirent);
	int	should_be = EXT2_FT_UNKNOWN;
	struct ext2_inode	inode;

	if (!ext2fs_has_feature_filetype(ctx->fs->super)) {
		if (filetype == 0 ||
		    !fix_problem(ctx, PR_2_CLEAR_FILETYPE, pctx))
			return 0;
		ext2fs_dirent_set_file_type(dirent, EXT2_FT_UNKNOWN);
		return 1;
	}

	if (ext2fs_test_inode_bitmap2(ctx->inode_dir_map, dirent->inode)) {
		should_be = EXT2_FT_DIR;
	} else if (ext2fs_test_inode_bitmap2(ctx->inode_reg_map,
					    dirent->inode)) {
		should_be = EXT2_FT_REG_FILE;
	} else if (ctx->inode_bad_map &&
		   ext2fs_test_inode_bitmap2(ctx->inode_bad_map,
					    dirent->inode))
		should_be = 0;
	else {
		e2fsck_read_inode(ctx, dirent->inode, &inode,
				  "check_filetype");
		should_be = ext2_file_type(inode.i_mode);
	}
	if (filetype == should_be)
		return 0;
	pctx->num = should_be;

	if (fix_problem(ctx, filetype ? PR_2_BAD_FILETYPE : PR_2_SET_FILETYPE,
			pctx) == 0)
		return 0;

	ext2fs_dirent_set_file_type(dirent, should_be);
	return 1;
}

static void parse_int_node(ext2_filsys fs,
			   struct ext2_db_entry2 *db,
			   struct check_dir_struct *cd,
			   struct dx_dir_info	*dx_dir,
			   char *block_buf, int failed_csum)
{
	struct 		ext2_dx_root_info  *root;
	struct 		ext2_dx_entry *ent;
	struct		ext2_dx_countlimit *limit;
	struct dx_dirblock_info	*dx_db;
	int		i, expect_limit, count;
	blk_t		blk;
	ext2_dirhash_t	min_hash = 0xffffffff;
	ext2_dirhash_t	max_hash = 0;
	ext2_dirhash_t	hash = 0, prev_hash;
	int		csum_size = 0;

	if (db->blockcnt == 0) {
		root = (struct ext2_dx_root_info *) (block_buf + 24);

#ifdef DX_DEBUG
		printf("Root node dump:\n");
		printf("\t Reserved zero: %u\n", root->reserved_zero);
		printf("\t Hash Version: %d\n", root->hash_version);
		printf("\t Info length: %d\n", root->info_length);
		printf("\t Indirect levels: %d\n", root->indirect_levels);
		printf("\t Flags: %d\n", root->unused_flags);
#endif

		ent = (struct ext2_dx_entry *) (block_buf + 24 + root->info_length);

		if (failed_csum &&
		    (e2fsck_dir_will_be_rehashed(cd->ctx, cd->pctx.ino) ||
		     fix_problem(cd->ctx, PR_2_HTREE_ROOT_CSUM_INVALID,
				&cd->pctx)))
			goto clear_and_exit;
	} else {
		ent = (struct ext2_dx_entry *) (block_buf+8);

		if (failed_csum &&
		    (e2fsck_dir_will_be_rehashed(cd->ctx, cd->pctx.ino) ||
		     fix_problem(cd->ctx, PR_2_HTREE_NODE_CSUM_INVALID,
				&cd->pctx)))
			goto clear_and_exit;
	}

	limit = (struct ext2_dx_countlimit *) ent;

#ifdef DX_DEBUG
	printf("Number of entries (count): %d\n",
	       ext2fs_le16_to_cpu(limit->count));
	printf("Number of entries (limit): %d\n",
	       ext2fs_le16_to_cpu(limit->limit));
#endif

	count = ext2fs_le16_to_cpu(limit->count);
	if (ext2fs_has_feature_metadata_csum(fs->super))
		csum_size = sizeof(struct ext2_dx_tail);
	expect_limit = (fs->blocksize -
			(csum_size + ((char *) ent - block_buf))) /
		       sizeof(struct ext2_dx_entry);
	if (ext2fs_le16_to_cpu(limit->limit) != expect_limit) {
		cd->pctx.num = ext2fs_le16_to_cpu(limit->limit);
		if (fix_problem(cd->ctx, PR_2_HTREE_BAD_LIMIT, &cd->pctx))
			goto clear_and_exit;
	}
	if (count > expect_limit) {
		cd->pctx.num = count;
		if (fix_problem(cd->ctx, PR_2_HTREE_BAD_COUNT, &cd->pctx))
			goto clear_and_exit;
		count = expect_limit;
	}

	for (i=0; i < count; i++) {
		prev_hash = hash;
		hash = i ? (ext2fs_le32_to_cpu(ent[i].hash) & ~1) : 0;
#ifdef DX_DEBUG
		printf("Entry #%d: Hash 0x%08x, block %u\n", i,
		       hash, ext2fs_le32_to_cpu(ent[i].block));
#endif
		blk = ext2fs_le32_to_cpu(ent[i].block) & EXT4_DX_BLOCK_MASK;
		/* Check to make sure the block is valid */
		
		if (blk >= (blk_t) dx_dir->numblocks) {
			cd->pctx.blk = blk;
			if (fix_problem(cd->ctx, PR_2_HTREE_BADBLK,
					&cd->pctx))
				goto clear_and_exit;
			continue;
		}
		if (hash < prev_hash &&
		    fix_problem(cd->ctx, PR_2_HTREE_HASH_ORDER, &cd->pctx))
			goto clear_and_exit;
		dx_db = &dx_dir->dx_block[blk];
		if (dx_db->flags & DX_FLAG_REFERENCED) {
			dx_db->flags |= DX_FLAG_DUP_REF;
		} else {
			dx_db->flags |= DX_FLAG_REFERENCED;
			dx_db->parent = db->blockcnt;
		}

		dx_db->previous =
			i ? (ext2fs_le32_to_cpu(ent[i-1].block) &
			     EXT4_DX_BLOCK_MASK) : 0;

		if (hash < min_hash)
			min_hash = hash;
		if (hash > max_hash)
			max_hash = hash;
		dx_db->node_min_hash = hash;
		if ((i+1) < count)
			dx_db->node_max_hash =
			  ext2fs_le32_to_cpu(ent[i+1].hash) & ~1;
		else {
			dx_db->node_max_hash = 0xfffffffe;
			dx_db->flags |= DX_FLAG_LAST;
		}
		if (i == 0)
			dx_db->flags |= DX_FLAG_FIRST;
	}
#ifdef DX_DEBUG
	printf("Blockcnt = %d, min hash 0x%08x, max hash 0x%08x\n",
	       db->blockcnt, min_hash, max_hash);
#endif
	dx_db = &dx_dir->dx_block[db->blockcnt];
	dx_db->min_hash = min_hash;
	dx_db->max_hash = max_hash;
	return;

clear_and_exit:
	clear_htree(cd->ctx, cd->pctx.ino);
	dx_dir->numblocks = 0;
	e2fsck_rehash_dir_later(cd->ctx, cd->pctx.ino);
}

/*
 * Given a busted directory, try to salvage it somehow.
 *
 */
static void salvage_directory(ext2_filsys fs,
			      struct ext2_dir_entry *dirent,
			      struct ext2_dir_entry *prev,
			      unsigned int *offset,
			      unsigned int block_len)
{
	char	*cp = (char *) dirent;
	int left;
	unsigned int rec_len, prev_rec_len;
	unsigned int name_len;

	/*
	 * If the space left for the entry is too small to be an entry,
	 * we can't access dirent's fields, so plumb in the values needed
	 * so that the previous entry absorbs this one.
	 */
	if (block_len - *offset < EXT2_DIR_ENTRY_HEADER_LEN) {
		name_len = 0;
		rec_len = block_len - *offset;
	} else {
		name_len = ext2fs_dirent_name_len(dirent);
		(void) ext2fs_get_rec_len(fs, dirent, &rec_len);
	}
	left = block_len - *offset - rec_len;

	/*
	 * Special case of directory entry of size 8: copy what's left
	 * of the directory block up to cover up the invalid hole.
	 */
	if ((left >= 12) && (rec_len == EXT2_DIR_ENTRY_HEADER_LEN)) {
		memmove(cp, cp+EXT2_DIR_ENTRY_HEADER_LEN, left);
		memset(cp + left, 0, EXT2_DIR_ENTRY_HEADER_LEN);
		return;
	}
	/*
	 * If the directory entry overruns the end of the directory
	 * block, and the name is small enough to fit, then adjust the
	 * record length.
	 */
	if ((left < 0) &&
	    ((int) rec_len + left > EXT2_DIR_ENTRY_HEADER_LEN) &&
	    ((int) name_len + EXT2_DIR_ENTRY_HEADER_LEN <= (int) rec_len + left) &&
	    dirent->inode <= fs->super->s_inodes_count &&
	    strnlen(dirent->name, name_len) == name_len) {
		(void) ext2fs_set_rec_len(fs, (int) rec_len + left, dirent);
		return;
	}
	/*
	 * If the record length of the directory entry is a multiple
	 * of four, and not too big, such that it is valid, let the
	 * previous directory entry absorb the invalid one.
	 */
	if (prev && rec_len && (rec_len % 4) == 0 &&
	    (*offset + rec_len <= block_len)) {
		(void) ext2fs_get_rec_len(fs, prev, &prev_rec_len);
		prev_rec_len += rec_len;
		(void) ext2fs_set_rec_len(fs, prev_rec_len, prev);
		*offset += rec_len;
		return;
	}
	/*
	 * Default salvage method --- kill all of the directory
	 * entries for the rest of the block.  We will either try to
	 * absorb it into the previous directory entry, or create a
	 * new empty directory entry the rest of the directory block.
	 */
	if (prev) {
		(void) ext2fs_get_rec_len(fs, prev, &prev_rec_len);
		prev_rec_len += block_len - *offset;
		(void) ext2fs_set_rec_len(fs, prev_rec_len, prev);
		*offset = fs->blocksize;
	} else {
		rec_len = block_len - *offset;
		(void) ext2fs_set_rec_len(fs, rec_len, dirent);
		ext2fs_dirent_set_name_len(dirent, 0);
		ext2fs_dirent_set_file_type(dirent, EXT2_FT_UNKNOWN);
		dirent->inode = 0;
	}
}

#define NEXT_DIRENT(d)	((void *)((char *)(d) + (d)->rec_len))
static errcode_t insert_dirent_tail(ext2_filsys fs, void *dirbuf)
{
	struct ext2_dir_entry *d;
	void *top;
	struct ext2_dir_entry_tail *t;

	d = dirbuf;
	top = EXT2_DIRENT_TAIL(dirbuf, fs->blocksize);

	while (d->rec_len && !(d->rec_len & 0x3) && NEXT_DIRENT(d) <= top)
		d = NEXT_DIRENT(d);

	if (d != top) {
		unsigned int min_size = EXT2_DIR_REC_LEN(
				ext2fs_dirent_name_len(dirbuf));
		if (min_size > (char *)top - (char *)d)
			return EXT2_ET_DIR_NO_SPACE_FOR_CSUM;
		d->rec_len = (char *)top - (char *)d;
	}

	t = (struct ext2_dir_entry_tail *)top;
	if (t->det_reserved_zero1 ||
	    t->det_rec_len != sizeof(struct ext2_dir_entry_tail) ||
	    t->det_reserved_name_len != EXT2_DIR_NAME_LEN_CSUM)
		ext2fs_initialize_dirent_tail(fs, t);

	return 0;
}
#undef NEXT_DIRENT

static errcode_t fix_inline_dir_size(e2fsck_t ctx, ext2_ino_t ino,
				     size_t *inline_data_size,
				     struct problem_context *pctx,
				     char *buf)
{
	ext2_filsys fs = ctx->fs;
	struct ext2_inode inode;
	size_t new_size, old_size;
	errcode_t retval;

	old_size = *inline_data_size;
	/*
	 * If there's not enough bytes to start the "second" dir block
	 * (in the EA space) then truncate everything to the first block.
	 */
	if (old_size > EXT4_MIN_INLINE_DATA_SIZE &&
	    old_size < EXT4_MIN_INLINE_DATA_SIZE +
		       EXT2_DIR_REC_LEN(1)) {
		old_size = EXT4_MIN_INLINE_DATA_SIZE;
		new_size = old_size;
	} else
		/* Increase to the next four-byte boundary for salvaging */
		new_size = old_size + (4 - (old_size & 3));
	memset(buf + old_size, 0, new_size - old_size);
	retval = ext2fs_inline_data_set(fs, ino, 0, buf, new_size);
	if (retval == EXT2_ET_INLINE_DATA_NO_SPACE) {
		/* Or we can't, so truncate. */
		new_size -= 4;
		retval = ext2fs_inline_data_set(fs, ino, 0, buf, new_size);
		if (retval) {
			if (fix_problem(ctx, PR_2_FIX_INLINE_DIR_FAILED,
					pctx)) {
				new_size = 0;
				goto write_inode;
			}
			goto err;
		}
	} else if (retval) {
		if (fix_problem(ctx, PR_2_FIX_INLINE_DIR_FAILED,
				pctx)) {
			new_size = 0;
			goto write_inode;
		}
		goto err;
	}

write_inode:
	retval = ext2fs_read_inode(fs, ino, &inode);
	if (retval)
		goto err;

	retval = ext2fs_inode_size_set(fs, &inode, new_size);
	if (retval)
		goto err;
	if (new_size == 0)
		inode.i_flags &= ~EXT4_INLINE_DATA_FL;
	retval = ext2fs_write_inode(fs, ino, &inode);
	if (retval)
		goto err;
	*inline_data_size = new_size;

err:
	return retval;
}

static int check_dir_block2(ext2_filsys fs,
			   struct ext2_db_entry2 *db,
			   void *priv_data)
{
	int err;
	struct check_dir_struct *cd = priv_data;

#ifdef _PIPELINE_PARALLELISM
	if (!cd->ctx->pipeline_threads && cd->ra_entries && cd->list_offset >= cd->next_ra_off) {
		err = e2fsck_readahead_dblist(fs,
					E2FSCK_RA_DBLIST_IGNORE_BLOCKCNT,
					fs->dblist,
					cd->list_offset + cd->ra_entries / 8,
					cd->ra_entries);
		if (err)
			cd->ra_entries = 0;
		cd->next_ra_off = cd->list_offset + (cd->ra_entries * 7 / 8);
	}
#else
	if (cd->ra_entries && cd->list_offset >= cd->next_ra_off) {
		err = e2fsck_readahead_dblist(fs,
					E2FSCK_RA_DBLIST_IGNORE_BLOCKCNT,
					fs->dblist,
					cd->list_offset + cd->ra_entries / 8,
					cd->ra_entries);
		if (err)
			cd->ra_entries = 0;
		cd->next_ra_off = cd->list_offset + (cd->ra_entries * 7 / 8);
	}
#endif

	err = check_dir_block(fs, db, priv_data);
	cd->list_offset++;
	return err;
}


fsck_lock_t read_lock;
fsck_lock_t count_lock;
static int check_dir_block(ext2_filsys fs,
			   struct ext2_db_entry2 *db,
			   void *priv_data)
{
 	struct dx_dir_info	*dx_dir;
	struct dx_dirblock_info	*dx_db = 0;
	struct ext2_dir_entry 	*dirent, *prev, dot, dotdot;
	ext2_dirhash_t		hash;
	unsigned int		offset = 0;
	int			dir_modified = 0;
	int			dot_state;
	unsigned int		rec_len;
	blk64_t			block_nr = db->blk;
	ext2_ino_t 		ino = db->ino;
	ext2_ino_t 		subdir_parent;
	__u16			links;
	struct check_dir_struct	*cd;
	char			*buf, *ibuf;
	e2fsck_t		ctx;
	problem_t		problem;
	struct ext2_dx_root_info *root;
	struct ext2_dx_countlimit *limit;
	dict_t de_dict;
	struct problem_context	pctx;
	int	dups_found = 0;
	int	ret;
	int	dx_csum_size = 0, de_csum_size = 0;
	int	failed_csum = 0;
	int	is_leaf = 1;
	size_t	inline_data_size = 0;
	int	filetype = 0;
	int	encrypted = 0;
	size_t	max_block_size;

	cd = (struct check_dir_struct *) priv_data;
	ibuf = buf = cd->buf;
	ctx = cd->ctx;


	// If using parallelism, ensure we get thread specific fs
	if(ctx->extra_threads){
		struct thread_ctx *tctx = get_tctx(ctx);
		fs = tctx->fs;
	}

	// sync to fixes here 
	pfsck_sync_fix(ctx);

	if (ctx->flags & E2F_FLAG_RUN_RETURN)
		return DIRENT_ABORT;

	if (ctx->progress && (ctx->progress)(ctx, 2, cd->count++, cd->max))
		return DIRENT_ABORT;

	if (ext2fs_has_feature_metadata_csum(fs->super)) {
		dx_csum_size = sizeof(struct ext2_dx_tail);
		de_csum_size = sizeof(struct ext2_dir_entry_tail);
	}

	if (ext2fs_has_feature_filetype(fs->super))
		filetype = EXT2_FT_DIR << 8;

	/*
	 * Make sure the inode is still in use (could have been
	 * deleted in the duplicate/bad blocks pass.
	 */
#ifdef _PIPELINE_PARALLELISM
	if (!ctx->pipeline_threads && !(ext2fs_test_inode_bitmap2(ctx->inode_used_map, ino)))
		return 0;
#else
	if (!(ext2fs_test_inode_bitmap2(ctx->inode_used_map, ino)))
		return 0;
#endif

	cd->pctx.ino = ino;
	cd->pctx.blk = block_nr;
	cd->pctx.blkcount = db->blockcnt;
	cd->pctx.ino2 = 0;
	cd->pctx.dirent = 0;
	cd->pctx.num = 0;

	if (ext2fs_has_feature_inline_data(fs->super)) {
		errcode_t ec;

		ec = ext2fs_inline_data_size(fs, ino, &inline_data_size);
		if (ec && ec != EXT2_ET_NO_INLINE_DATA)
			return DIRENT_ABORT;
	}

	/* This will allow (at some point in the future) to punch out empty
	 * directory blocks and reduce the space used by a directory that grows
	 * very large and then the files are deleted. For now, all that is
	 * needed is to avoid e2fsck filling in these holes as part of
	 * feature flag. */
	if (db->blk == 0 && ext2fs_has_feature_largedir(fs->super))
		return 0;

	if (db->blk == 0 && !inline_data_size) {
		if (allocate_dir_block(ctx, db, buf, &cd->pctx))
			return 0;
		block_nr = db->blk;
	}

	if (db->blockcnt)
		dot_state = 2;
	else
		dot_state = 0;

	if (ctx->dirs_to_hash &&
	    ext2fs_u32_list_test(ctx->dirs_to_hash, ino))
		dups_found++;

#if 0
	printf("In process_dir_block block %lu, #%d, inode %lu\n", block_nr,
	       db->blockcnt, ino);
#endif

#ifdef _REDUCE_OP_STRINGS
		ehandler_operation(read_dir_block_string);
#else
		ehandler_operation(_("reading directory block"));
#endif

	//fsck_lock(&ctx->dx_dir_lock);

	if (inline_data_size) {
		memset(buf, 0, fs->blocksize - inline_data_size);
		cd->pctx.errcode = ext2fs_inline_data_get(fs, ino, 0, buf, 0);
		if (cd->pctx.errcode)
			goto inline_read_fail;
#ifdef WORDS_BIGENDIAN
		if (db->blockcnt)
			goto skip_first_read_swab;
		*((__u32 *)buf) = ext2fs_le32_to_cpu(*((__u32 *)buf));
		cd->pctx.errcode = ext2fs_dirent_swab_in2(fs,
				buf + EXT4_INLINE_DATA_DOTDOT_SIZE,
				EXT4_MIN_INLINE_DATA_SIZE - EXT4_INLINE_DATA_DOTDOT_SIZE,
				0);
		if (cd->pctx.errcode)
			goto inline_read_fail;
skip_first_read_swab:
		if (inline_data_size <= EXT4_MIN_INLINE_DATA_SIZE ||
		    !db->blockcnt)
			goto inline_read_fail;
		cd->pctx.errcode = ext2fs_dirent_swab_in2(fs,
				buf + EXT4_MIN_INLINE_DATA_SIZE,
				inline_data_size - EXT4_MIN_INLINE_DATA_SIZE,
				0);
#endif
	} else{

#ifdef _MIN_READ
		cd->pctx.errcode = ext2fs_read_dir_block5(fs, block_nr,
					                  buf, 0, ino, db->read_size);
#else
		cd->pctx.errcode = ext2fs_read_dir_block4(fs, block_nr,
							  buf, 0, ino);
#endif

	}
inline_read_fail:
	pctx.ino = ino;
	pctx.num = inline_data_size;
	if (((inline_data_size & 3) ||
	     (inline_data_size > EXT4_MIN_INLINE_DATA_SIZE &&
	      inline_data_size < EXT4_MIN_INLINE_DATA_SIZE +
				 EXT2_DIR_REC_LEN(1))) &&
	    fix_problem(ctx, PR_2_BAD_INLINE_DIR_SIZE, &pctx)) {
		errcode_t err = fix_inline_dir_size(ctx, ino,
						    &inline_data_size, &pctx,
						    buf);
		if (err)
			return DIRENT_ABORT;

	}
	ehandler_operation(0);
	if (cd->pctx.errcode == EXT2_ET_DIR_CORRUPTED)
		cd->pctx.errcode = 0; /* We'll handle this ourselves */
	else if (cd->pctx.errcode == EXT2_ET_DIR_CSUM_INVALID) {
		cd->pctx.errcode = 0; /* We'll handle this ourselves */
		failed_csum = 1;
	}
	if (cd->pctx.errcode) {
		char *buf2;
		if (!fix_problem(ctx, PR_2_READ_DIRBLOCK, &cd->pctx)) {
			ctx->flags |= E2F_FLAG_ABORT;
			return DIRENT_ABORT;
		}
		ext2fs_new_dir_block(fs, db->blockcnt == 0 ? ino : 0,
				     EXT2_ROOT_INO, &buf2);
		memcpy(buf, buf2, fs->blocksize);
		ext2fs_free_mem(&buf2);
	}
	
	dx_dir = e2fsck_get_dx_dir_info(ctx, ino);
	if (dx_dir && dx_dir->numblocks) {
		if (db->blockcnt >= dx_dir->numblocks) {
			pctx.dir = ino;
			if (fix_problem(ctx, PR_2_UNEXPECTED_HTREE_BLOCK,
					&pctx)) {
				clear_htree(ctx, ino);
				dx_dir->numblocks = 0;
				dx_db = 0;
				goto out_htree;
			}
			fatal_error(ctx, _("Can not continue."));
		}
		dx_db = &dx_dir->dx_block[db->blockcnt];
		dx_db->type = DX_DIRBLOCK_LEAF;
		dx_db->phys = block_nr;
		dx_db->min_hash = ~0;
		dx_db->max_hash = 0;

		dirent = (struct ext2_dir_entry *) buf;
		(void) ext2fs_get_rec_len(fs, dirent, &rec_len);
		limit = (struct ext2_dx_countlimit *) (buf+8);
		if (db->blockcnt == 0) {
			root = (struct ext2_dx_root_info *) (buf + 24);
			dx_db->type = DX_DIRBLOCK_ROOT;
			dx_db->flags |= DX_FLAG_FIRST | DX_FLAG_LAST;
			if ((root->reserved_zero ||
			     root->info_length < 8 ||
			     root->indirect_levels >=
			     ext2_dir_htree_level(fs)) &&
			    fix_problem(ctx, PR_2_HTREE_BAD_ROOT, &cd->pctx)) {
				clear_htree(ctx, ino);
				dx_dir->numblocks = 0;
				dx_db = NULL;
			}
			dx_dir->hashversion = root->hash_version;
			if ((dx_dir->hashversion <= EXT2_HASH_TEA) &&
			    (fs->super->s_flags & EXT2_FLAGS_UNSIGNED_HASH))
				dx_dir->hashversion += 3;
			dx_dir->depth = root->indirect_levels + 1;
		} else if ((dirent->inode == 0) &&
			   (rec_len == fs->blocksize) &&
			   (ext2fs_dirent_name_len(dirent) == 0) &&
			   (ext2fs_le16_to_cpu(limit->limit) ==
			    ((fs->blocksize - (8 + dx_csum_size)) /
			     sizeof(struct ext2_dx_entry)))) {
			dx_db->type = DX_DIRBLOCK_NODE;
		}
		is_leaf = dx_db ? (dx_db->type == DX_DIRBLOCK_LEAF) : 0;
	}
out_htree:
	//fsck_lock(&ctx->dx_dir_lock); before this needs to be locked or breaks

	/* Leaf node with no space for csum?  Rebuild dirs in pass 3A. */
	if (is_leaf && !inline_data_size && failed_csum &&
	    !ext2fs_dirent_has_tail(fs, (struct ext2_dir_entry *)buf)) {
		de_csum_size = 0;
		if (e2fsck_dir_will_be_rehashed(ctx, ino)) {
			failed_csum = 0;
			goto skip_checksum;
		}
		if (!fix_problem(cd->ctx, PR_2_LEAF_NODE_MISSING_CSUM,
				 &cd->pctx))
			goto skip_checksum;
		e2fsck_rehash_dir_later(ctx, ino);
		failed_csum = 0;
		goto skip_checksum;
	}
	/* htree nodes don't use fake dirents to store checksums */
	if (!is_leaf)
		de_csum_size = 0;


skip_checksum:
	if (inline_data_size) {
		if (db->blockcnt) {
			buf += EXT4_MIN_INLINE_DATA_SIZE;
			max_block_size = inline_data_size - EXT4_MIN_INLINE_DATA_SIZE;
			/* Zero-length second block, just exit */
			if (max_block_size == 0)
				return 0;
		} else {
			max_block_size = EXT4_MIN_INLINE_DATA_SIZE;
		}
	} else
		max_block_size = fs->blocksize - de_csum_size;

	if (ctx->encrypted_dirs)
		encrypted = ext2fs_u32_list_test(ctx->encrypted_dirs, ino);

	dict_init(&de_dict, DICTCOUNT_T_MAX, dict_de_cmp);
	prev = 0;
	do {
		dgrp_t group;
		ext2_ino_t first_unused_inode;
		unsigned int name_len;

		problem = 0;
		if (!inline_data_size || dot_state > 1) {
			dirent = (struct ext2_dir_entry *) (buf + offset);
			/*
			 * If there's not even space for the entry header,
			 * force salvaging this dir.
			 */
			if (max_block_size - offset < EXT2_DIR_ENTRY_HEADER_LEN)
				rec_len = EXT2_DIR_REC_LEN(1);
			else
				(void) ext2fs_get_rec_len(fs, dirent, &rec_len);
			cd->pctx.dirent = dirent;
			cd->pctx.num = offset;
			if ((offset + rec_len > max_block_size) ||
			    (rec_len < 12) ||
			    ((rec_len % 4) != 0) ||
			    (((unsigned) ext2fs_dirent_name_len(dirent) + EXT2_DIR_ENTRY_HEADER_LEN) > rec_len)) {
				if (fix_problem(ctx, PR_2_DIR_CORRUPTED,
						&cd->pctx)) {
#ifdef WORDS_BIGENDIAN
					/*
					 * On big-endian systems, if the dirent
					 * swap routine finds a rec_len that it
					 * doesn't like, it continues
					 * processing the block as if rec_len
					 * == EXT2_DIR_ENTRY_HEADER_LEN.  This means that the name
					 * field gets byte swapped, which means
					 * that salvage will not detect the
					 * correct name length (unless the name
					 * has a length that's an exact
					 * multiple of four bytes), and it'll
					 * discard the entry (unnecessarily)
					 * and the rest of the dirent block.
					 * Therefore, swap the rest of the
					 * block back to disk order, run
					 * salvage, and re-swap anything after
					 * the salvaged dirent.
					 */
					int need_reswab = 0;
					if (rec_len < EXT2_DIR_ENTRY_HEADER_LEN || rec_len % 4) {
						need_reswab = 1;
						ext2fs_dirent_swab_in2(fs,
							((char *)dirent) + EXT2_DIR_ENTRY_HEADER_LEN,
							max_block_size - offset - EXT2_DIR_ENTRY_HEADER_LEN,
							0);
					}
#endif
					salvage_directory(fs, dirent, prev,
							  &offset,
							  max_block_size);
#ifdef WORDS_BIGENDIAN
					if (need_reswab) {
						(void) ext2fs_get_rec_len(fs,
							dirent, &rec_len);
						ext2fs_dirent_swab_in2(fs,
							((char *)dirent) + offset + rec_len,
							max_block_size - offset - rec_len,
							0);
					}
#endif
					dir_modified++;
					continue;
				} else
					goto abort_free_dict;
			}
		} else {
			if (dot_state == 0) {
				memset(&dot, 0, sizeof(dot));
				dirent = &dot;
				dirent->inode = ino;
				dirent->rec_len = EXT2_DIR_REC_LEN(1);
				dirent->name_len = 1 | filetype;
				dirent->name[0] = '.';
			} else if (dot_state == 1) {
				memset(&dotdot, 0, sizeof(dotdot));
				dirent = &dotdot;
				dirent->inode =
					((struct ext2_dir_entry *)buf)->inode;
				dirent->rec_len = EXT2_DIR_REC_LEN(2);
				dirent->name_len = 2 | filetype;
				dirent->name[0] = '.';
				dirent->name[1] = '.';
			} else {
				fatal_error(ctx, _("Can not continue."));
			}
			cd->pctx.dirent = dirent;
			cd->pctx.num = offset;
		}

		if (dot_state == 0) {
			if (check_dot(ctx, dirent, ino, &cd->pctx))
				dir_modified++;
		} else if (dot_state == 1) {

			if(cd->dir_info_ptr){
				ret = check_dotdot(ctx, dirent, ino, &cd->pctx, cd->dir_info_ptr);
			} else{
				ret = check_dotdot(ctx, dirent, ino, &cd->pctx, NULL);
			}

			if (ret < 0)
				goto abort_free_dict;
			if (ret)
				dir_modified++;
		} else if (dirent->inode == ino) {
			problem = PR_2_LINK_DOT;
			if (fix_problem(ctx, PR_2_LINK_DOT, &cd->pctx)) {
				dirent->inode = 0;
				dir_modified++;
				goto next;
			}
		}
		if (!dirent->inode)
			goto next;

		/*
		 * Make sure the inode listed is a legal one.
		 */
		name_len = ext2fs_dirent_name_len(dirent);
		if (((dirent->inode != EXT2_ROOT_INO) &&
		     (dirent->inode < EXT2_FIRST_INODE(fs->super))) ||
		    (dirent->inode > fs->super->s_inodes_count)) {
			problem = PR_2_BAD_INO;
#ifdef _PIPELINE_PARALLELISM
		} else if (!ctx->pipeline_threads && ctx->inode_bb_map &&
			   (ext2fs_test_inode_bitmap2(ctx->inode_bb_map,
						     dirent->inode))) {
			/*
			 * If the inode is in a bad block, offer to
			 * clear it.
			 */
			problem = PR_2_BB_INODE;
#else
		} else if (ctx->inode_bb_map &&
				 (ext2fs_test_inode_bitmap2(ctx->inode_bb_map,
								 dirent->inode))) {
			/*
			 * If the inode is in a bad block, offer to
			 * clear it.
			 */
			problem = PR_2_BB_INODE;
#endif
		} else if ((dot_state > 1) && (name_len == 1) &&
			   (dirent->name[0] == '.')) {
			/*
			 * If there's a '.' entry in anything other
			 * than the first directory entry, it's a
			 * duplicate entry that should be removed.
			 */
			problem = PR_2_DUP_DOT;
		} else if ((dot_state > 1) && (name_len == 2) &&
			   (dirent->name[0] == '.') &&
			   (dirent->name[1] == '.')) {
			/*
			 * If there's a '..' entry in anything other
			 * than the second directory entry, it's a
			 * duplicate entry that should be removed.
			 */
			problem = PR_2_DUP_DOT_DOT;
		} else if ((dot_state > 1) &&
			   (dirent->inode == EXT2_ROOT_INO)) {
			/*
			 * Don't allow links to the root directory.
			 * We check this specially to make sure we
			 * catch this error case even if the root
			 * directory hasn't been created yet.
			 */
			problem = PR_2_LINK_ROOT;
		} else if ((dot_state > 1) && (name_len == 0)) {
			/*
			 * Don't allow zero-length directory names.
			 */
			problem = PR_2_NULL_NAME;
		}

		if (problem) {
			if (fix_problem(ctx, problem, &cd->pctx)) {
				dirent->inode = 0;
				dir_modified++;
				goto next;
			} else {
				ext2fs_unmark_valid(fs);
				if (problem == PR_2_BAD_INO)
					goto next;
			}
		}

		/*
		 * If the inode was marked as having bad fields in
		 * pass1, process it and offer to fix/clear it.
		 * (We wait until now so that we can display the
		 * pathname to the user.)
		 */
#ifdef _PIPELINE_PARALLELISM
		if (!ctx->pipeline_threads && ctx->inode_bad_map &&
		    ext2fs_test_inode_bitmap2(ctx->inode_bad_map,
					     dirent->inode)) {
			if (e2fsck_process_bad_inode(ctx, ino,
						     dirent->inode,
						     buf + fs->blocksize)) {
				dirent->inode = 0;
				dir_modified++;
				goto next;
			}
			if (ctx->flags & E2F_FLAG_SIGNAL_MASK)
				return DIRENT_ABORT;
		}
#else
		if (ctx->inode_bad_map &&
				ext2fs_test_inode_bitmap2(ctx->inode_bad_map,
							 dirent->inode)) {
			if (e2fsck_process_bad_inode(ctx, ino,
								 dirent->inode,
								 buf + fs->blocksize)) {
				dirent->inode = 0;
				dir_modified++;
				goto next;
			}
			if (ctx->flags & E2F_FLAG_SIGNAL_MASK)
				return DIRENT_ABORT;
		}
#endif

		group = ext2fs_group_of_ino(fs, dirent->inode);
		first_unused_inode = group * fs->super->s_inodes_per_group +
					1 + fs->super->s_inodes_per_group -
					ext2fs_bg_itable_unused(fs, group);
		cd->pctx.group = group;

		/*
		 * Check if the inode was missed out because
		 * _INODE_UNINIT flag was set or bg_itable_unused was
		 * incorrect.  If so, clear the _INODE_UNINIT flag and
		 * restart e2fsck.  In the future it would be nice if
		 * we could call a function in pass1.c that checks the
		 * newly visible inodes.
		 */
		if (ext2fs_bg_flags_test(fs, group, EXT2_BG_INODE_UNINIT)) {
			pctx.num = dirent->inode;
			if (fix_problem(ctx, PR_2_INOREF_BG_INO_UNINIT,
					&cd->pctx)){
				ext2fs_bg_flags_clear(fs, group,
						      EXT2_BG_INODE_UNINIT);
				ext2fs_mark_super_dirty(fs);
				ctx->flags |= E2F_FLAG_RESTART_LATER;
			} else {
				ext2fs_unmark_valid(fs);
				if (problem == PR_2_BAD_INO)
					goto next;
			}
		} else if (dirent->inode >= first_unused_inode) {
			pctx.num = dirent->inode;
			if (fix_problem(ctx, PR_2_INOREF_IN_UNUSED, &cd->pctx)){
				ext2fs_bg_itable_unused_set(fs, group, 0);
				ext2fs_mark_super_dirty(fs);
				ctx->flags |= E2F_FLAG_RESTART_LATER;
			} else {
				ext2fs_unmark_valid(fs);
				if (problem == PR_2_BAD_INO)
					goto next;
			}
		}

		/*
		 * Offer to clear unused inodes; if we are going to be
		 * restarting the scan due to bg_itable_unused being
		 * wrong, then don't clear any inodes to avoid zapping
		 * inodes that were skipped during pass1 due to an
		 * incorrect bg_itable_unused; we'll get any real
		 * problems after we restart.
		 */
#ifdef _PIPELINE_PARALLELISM
		if (!ctx->pipeline_threads && !(ctx->flags & E2F_FLAG_RESTART_LATER) &&
		    !(ext2fs_test_inode_bitmap2(ctx->inode_used_map,
						dirent->inode)))
			problem = PR_2_UNUSED_INODE;
#else
		if (!(ctx->flags & E2F_FLAG_RESTART_LATER) &&
				!(ext2fs_test_inode_bitmap2(ctx->inode_used_map,
						dirent->inode)))
			problem = PR_2_UNUSED_INODE;
#endif
		if (problem) {
			if (fix_problem(ctx, problem, &cd->pctx)) {
				dirent->inode = 0;
				dir_modified++;
				goto next;
			} else {
				ext2fs_unmark_valid(fs);
				if (problem == PR_2_BAD_INO)
					goto next;
			}
		}

		if (!encrypted && check_name(ctx, dirent, &cd->pctx))
			dir_modified++;

		if (encrypted && (dot_state) > 1 &&
		    encrypted_check_name(ctx, dirent, &cd->pctx)) {
			dir_modified++;
			goto next;
		}

#ifdef _PIPELINE_PARALLELISM
		if (!ctx->pipeline_threads && check_filetype(ctx, dirent, ino, &cd->pctx))
			dir_modified++;
#else
		if (check_filetype(ctx, dirent, ino, &cd->pctx))
			dir_modified++;
#endif

		if (dx_db) {
			ext2fs_dirhash(dx_dir->hashversion, dirent->name,
				       ext2fs_dirent_name_len(dirent),
				       fs->super->s_hash_seed, &hash, 0);
			if (hash < dx_db->min_hash)
				dx_db->min_hash = hash;
			if (hash > dx_db->max_hash)
				dx_db->max_hash = hash;
		}

		/*
		 * If this is a directory, then mark its parent in its
		 * dir_info structure.  If the parent field is already
		 * filled in, then this directory has more than one
		 * hard link.  We assume the first link is correct,
		 * and ask the user if he/she wants to clear this one.
		 */
#ifdef _PIPELINE_PARALLELISM
		if(!ctx->pipeline_threads){
			if ((dot_state > 1) &&
					(ext2fs_test_inode_bitmap2(ctx->inode_dir_map,
									dirent->inode))) {
				if (e2fsck_dir_info_get_parent(ctx, dirent->inode,
										 &subdir_parent)) {
					cd->pctx.ino = dirent->inode;
					fix_problem(ctx, PR_2_NO_DIRINFO, &cd->pctx);
					goto abort_free_dict;
				}
				if (subdir_parent) {
					cd->pctx.ino2 = subdir_parent;
					if (fix_problem(ctx, PR_2_LINK_DIR,
							&cd->pctx)) {
						dirent->inode = 0;
						dir_modified++;
						goto next;
					}
					cd->pctx.ino2 = 0;
				} else {
					update_count++;
					(void) e2fsck_dir_info_set_parent(ctx,
								dirent->inode, ino);
				}
			}
		} else{
			if(dot_state > 1){
				add_update_parent_task(dirent->inode, ino);
			}
		}
#else
		if ((dot_state > 1) &&
		    (ext2fs_test_inode_bitmap2(ctx->inode_dir_map,
					      dirent->inode))) {
			if (e2fsck_dir_info_get_parent(ctx, dirent->inode,
						       &subdir_parent)) {
				cd->pctx.ino = dirent->inode;
				fix_problem(ctx, PR_2_NO_DIRINFO, &cd->pctx);
				goto abort_free_dict;
			}
			if (subdir_parent) {
				cd->pctx.ino2 = subdir_parent;
				if (fix_problem(ctx, PR_2_LINK_DIR,
						&cd->pctx)) {
					dirent->inode = 0;
					dir_modified++;
					goto next;
				}
				cd->pctx.ino2 = 0;
			} else {
				update_count++;
				(void) e2fsck_dir_info_set_parent(ctx,
						  dirent->inode, ino);
			}
		}
#endif

		if (dups_found) {
			;
		} else if (dict_lookup(&de_dict, dirent)) {
			clear_problem_context(&pctx);
			pctx.ino = ino;
			pctx.dirent = dirent;
			fix_problem(ctx, PR_2_REPORT_DUP_DIRENT, &pctx);
			e2fsck_rehash_dir_later(ctx, ino);
			dups_found++;
		} else
			dict_alloc_insert(&de_dict, dirent, dirent);

#ifdef _ICOUNT_MERGE_B	

		if(ctx->extra_threads){
			int tid = syscall(SYS_gettid);
			int tctx_index = tid % ctx->extra_threads;
			struct thread_ctx *tctx = &ctx->tctx_array[tctx_index];
			ext2fs_icount_increment(tctx->inode_count, dirent->inode, &links);
			if (links > 1)
				tctx->fs_links_count++;
			tctx->fs_total_count++;
		}else{
			fsck_lock(&count_lock);
			ext2fs_icount_increment(ctx->inode_count, dirent->inode,
					&links);
			if (links > 1)
				ctx->fs_links_count++;
			ctx->fs_total_count++;
			fsck_unlock(&count_lock);
		}

#else
		fsck_lock(&count_lock);
		ext2fs_icount_increment(ctx->inode_count, dirent->inode,
					&links);
		if (links > 1)
			ctx->fs_links_count++;
		ctx->fs_total_count++;
		fsck_unlock(&count_lock);
#endif
	next:
		prev = dirent;
		if (dir_modified)
			(void) ext2fs_get_rec_len(fs, dirent, &rec_len);
		if (!inline_data_size || dot_state > 1) {
			offset += rec_len;
		} else {
			if (dot_state == 1) {
				offset = 4;
				/*
				 * If we get here, we're checking an inline
				 * directory and we've just checked a (fake)
				 * dotdot entry that we created on the stack.
				 * Therefore set 'prev' to NULL so that if we
				 * call salvage_directory on the next entry,
				 * it won't try to absorb the next entry into
				 * the on-stack dotdot entry.
				 */
				prev = NULL;
			}
		}
		dot_state++;
	} while (offset < max_block_size);

	
#if 0
	printf("\n");
#endif
	if (dx_db) {
#ifdef DX_DEBUG
		printf("db_block %d, type %d, min_hash 0x%0x, max_hash 0x%0x\n",
		       db->blockcnt, dx_db->type,
		       dx_db->min_hash, dx_db->max_hash);
#endif
		cd->pctx.dir = cd->pctx.ino;
		if ((dx_db->type == DX_DIRBLOCK_ROOT) ||
		    (dx_db->type == DX_DIRBLOCK_NODE))
			parse_int_node(fs, db, cd, dx_dir, buf, failed_csum);
	}
	
	if (offset != max_block_size) {
		cd->pctx.num = rec_len + offset - max_block_size;
		if (fix_problem(ctx, PR_2_FINAL_RECLEN, &cd->pctx)) {
			dirent->rec_len = cd->pctx.num;
			dir_modified++;
		}
	}

	//fsck_unlock(&ctx->dx_dir_lock);

	if (dir_modified) {
		int	flags, will_rehash;
		/* leaf block with no tail?  Rehash dirs later. */
		if (ext2fs_has_feature_metadata_csum(fs->super) &&
		    is_leaf &&
		    !inline_data_size &&
		    !ext2fs_dirent_has_tail(fs, (struct ext2_dir_entry *)buf)) {
			if (insert_dirent_tail(fs, buf) == 0)
				goto write_and_fix;
			e2fsck_rehash_dir_later(ctx, ino);
		}

write_and_fix:
		will_rehash = e2fsck_dir_will_be_rehashed(ctx, ino);
		if (will_rehash) {
			flags = ctx->fs->flags;
			ctx->fs->flags |= EXT2_FLAG_IGNORE_CSUM_ERRORS;
		}
		if (inline_data_size) {
			buf = ibuf;
#ifdef WORDS_BIGENDIAN
			if (db->blockcnt)
				goto skip_first_write_swab;
			*((__u32 *)buf) = ext2fs_le32_to_cpu(*((__u32 *)buf));
			cd->pctx.errcode = ext2fs_dirent_swab_out2(fs,
					buf + EXT4_INLINE_DATA_DOTDOT_SIZE,
					EXT4_MIN_INLINE_DATA_SIZE -
					EXT4_INLINE_DATA_DOTDOT_SIZE,
					0);
			if (cd->pctx.errcode)
				goto skip_second_write_swab;
skip_first_write_swab:
			if (inline_data_size <= EXT4_MIN_INLINE_DATA_SIZE ||
			    !db->blockcnt)
				goto skip_second_write_swab;
			cd->pctx.errcode = ext2fs_dirent_swab_out2(fs,
					buf + EXT4_MIN_INLINE_DATA_SIZE,
					inline_data_size -
					EXT4_MIN_INLINE_DATA_SIZE,
					0);
skip_second_write_swab:
			if (cd->pctx.errcode &&
			    !fix_problem(ctx, PR_2_WRITE_DIRBLOCK, &cd->pctx))
				goto abort_free_dict;
#endif
			cd->pctx.errcode =
				ext2fs_inline_data_set(fs, ino, 0, buf,
						       inline_data_size);
		} else
			cd->pctx.errcode = ext2fs_write_dir_block4(fs, block_nr,
								   buf, 0, ino);
		if (will_rehash)
			ctx->fs->flags = (flags &
					  EXT2_FLAG_IGNORE_CSUM_ERRORS) |
					 (ctx->fs->flags &
					  ~EXT2_FLAG_IGNORE_CSUM_ERRORS);
		if (cd->pctx.errcode) {
			if (!fix_problem(ctx, PR_2_WRITE_DIRBLOCK,
					 &cd->pctx))
				goto abort_free_dict;
		}
		ext2fs_mark_changed(fs);
	} else if (is_leaf && failed_csum && !dir_modified) {
		/*
		 * If a leaf node that fails csum makes it this far without
		 * alteration, ask the user if the checksum should be fixed.
		 */
		if (fix_problem(ctx, PR_2_LEAF_NODE_ONLY_CSUM_INVALID,
				&cd->pctx))
			goto write_and_fix;
	}

	//fsck_unlock(&ctx->dx_dir_lock);

	// Remember to free dict nodes
	dict_free_nodes(&de_dict);

	return 0;
abort_free_dict:
	//fsck_unlock(&ctx->dx_dir_lock);

	ctx->flags |= E2F_FLAG_ABORT;
	dict_free_nodes(&de_dict);
	return DIRENT_ABORT;
}

struct del_block {
	e2fsck_t	ctx;
	e2_blkcnt_t	num;
	blk64_t last_cluster;
};

/*
 * This function is called to deallocate a block, and is an interator
 * functioned called by deallocate inode via ext2fs_iterate_block().
 */
static int deallocate_inode_block(ext2_filsys fs,
				  blk64_t	*block_nr,
				  e2_blkcnt_t blockcnt EXT2FS_ATTR((unused)),
				  blk64_t ref_block EXT2FS_ATTR((unused)),
				  int ref_offset EXT2FS_ATTR((unused)),
				  void *priv_data)
{
	struct del_block *p = priv_data;
	blk64_t cluster = EXT2FS_B2C(fs, *block_nr);

	if (*block_nr == 0)
		return 0;

	if (cluster == p->last_cluster)
		return 0;

	p->last_cluster = cluster;
	if ((*block_nr < fs->super->s_first_data_block) ||
	    (*block_nr >= ext2fs_blocks_count(fs->super)))
		return 0;

        ext2fs_block_alloc_stats2(fs, *block_nr, -1);
	p->num++;
	return 0;
}

/*
 * This function deallocates an inode
 */
static void deallocate_inode(e2fsck_t ctx, ext2_ino_t ino, char* block_buf)
{
	ext2_filsys fs = ctx->fs;
	struct ext2_inode	inode;
	struct problem_context	pctx;
	__u32			count;
	struct del_block	del_block;

	e2fsck_read_inode(ctx, ino, &inode, "deallocate_inode");
	clear_problem_context(&pctx);
	pctx.ino = ino;

	/*
	 * Fix up the bitmaps...
	 */
	e2fsck_read_bitmaps(ctx);
	ext2fs_inode_alloc_stats2(fs, ino, -1, LINUX_S_ISDIR(inode.i_mode));

	if (ext2fs_file_acl_block(fs, &inode) &&
	    ext2fs_has_feature_xattr(fs->super)) {
		pctx.errcode = ext2fs_adjust_ea_refcount3(fs,
				ext2fs_file_acl_block(fs, &inode),
				block_buf, -1, &count, ino);
		if (pctx.errcode == EXT2_ET_BAD_EA_BLOCK_NUM) {
			pctx.errcode = 0;
			count = 1;
		}
		if (pctx.errcode) {
			pctx.blk = ext2fs_file_acl_block(fs, &inode);
			fix_problem(ctx, PR_2_ADJ_EA_REFCOUNT, &pctx);
			ctx->flags |= E2F_FLAG_ABORT;
			return;
		}
		if (count == 0) {
			ext2fs_block_alloc_stats2(fs,
				  ext2fs_file_acl_block(fs, &inode), -1);
		}
		ext2fs_file_acl_block_set(fs, &inode, 0);
	}

	if (!ext2fs_inode_has_valid_blocks2(fs, &inode))
		goto clear_inode;

	/* Inline data inodes don't have blocks to iterate */
	if (inode.i_flags & EXT4_INLINE_DATA_FL)
		goto clear_inode;

	if (LINUX_S_ISREG(inode.i_mode) &&
	    ext2fs_needs_large_file_feature(EXT2_I_SIZE(&inode)))
		ctx->large_files--;

	del_block.ctx = ctx;
	del_block.num = 0;
	del_block.last_cluster = 0;
	pctx.errcode = ext2fs_block_iterate3(fs, ino, 0, block_buf,
					     deallocate_inode_block,
					     &del_block);
	if (pctx.errcode) {
		fix_problem(ctx, PR_2_DEALLOC_INODE, &pctx);
		ctx->flags |= E2F_FLAG_ABORT;
		return;
	}
clear_inode:
	/* Inode may have changed by block_iterate, so reread it */
	e2fsck_read_inode(ctx, ino, &inode, "deallocate_inode");
	e2fsck_clear_inode(ctx, ino, &inode, 0, "deallocate_inode");
}

/*
 * This function clears the htree flag on an inode
 */
static void clear_htree(e2fsck_t ctx, ext2_ino_t ino)
{
	struct ext2_inode	inode;

	e2fsck_read_inode(ctx, ino, &inode, "clear_htree");
	inode.i_flags = inode.i_flags & ~EXT2_INDEX_FL;
	e2fsck_write_inode(ctx, ino, &inode, "clear_htree");
	if (ctx->dirs_to_hash)
		ext2fs_u32_list_add(ctx->dirs_to_hash, ino);
}


int e2fsck_process_bad_inode(e2fsck_t ctx, ext2_ino_t dir,
			     ext2_ino_t ino, char *buf)
{
	ext2_filsys fs = ctx->fs;
	struct ext2_inode	inode;
	int			inode_modified = 0;
	int			not_fixed = 0;
	unsigned char		*frag, *fsize;
	struct problem_context	pctx;
	problem_t		problem = 0;

	e2fsck_read_inode(ctx, ino, &inode, "process_bad_inode");

	clear_problem_context(&pctx);
	pctx.ino = ino;
	pctx.dir = dir;
	pctx.inode = &inode;

	if (ext2fs_file_acl_block(fs, &inode) &&
	    !ext2fs_has_feature_xattr(fs->super)) {
		if (fix_problem(ctx, PR_2_FILE_ACL_ZERO, &pctx)) {
			ext2fs_file_acl_block_set(fs, &inode, 0);
			inode_modified++;
		} else
			not_fixed++;
	}

	if (!LINUX_S_ISDIR(inode.i_mode) && !LINUX_S_ISREG(inode.i_mode) &&
	    !LINUX_S_ISCHR(inode.i_mode) && !LINUX_S_ISBLK(inode.i_mode) &&
	    !LINUX_S_ISLNK(inode.i_mode) && !LINUX_S_ISFIFO(inode.i_mode) &&
	    !(LINUX_S_ISSOCK(inode.i_mode)))
		problem = PR_2_BAD_MODE;
	else if (LINUX_S_ISCHR(inode.i_mode)
		 && !e2fsck_pass1_check_device_inode(fs, &inode))
		problem = PR_2_BAD_CHAR_DEV;
	else if (LINUX_S_ISBLK(inode.i_mode)
		 && !e2fsck_pass1_check_device_inode(fs, &inode))
		problem = PR_2_BAD_BLOCK_DEV;
	else if (LINUX_S_ISFIFO(inode.i_mode)
		 && !e2fsck_pass1_check_device_inode(fs, &inode))
		problem = PR_2_BAD_FIFO;
	else if (LINUX_S_ISSOCK(inode.i_mode)
		 && !e2fsck_pass1_check_device_inode(fs, &inode))
		problem = PR_2_BAD_SOCKET;
	else if (LINUX_S_ISLNK(inode.i_mode)
		 && !e2fsck_pass1_check_symlink(fs, ino, &inode, buf)) {
		problem = PR_2_INVALID_SYMLINK;
	}

	if (problem) {
		if (fix_problem(ctx, problem, &pctx)) {
			deallocate_inode(ctx, ino, 0);
			if (ctx->flags & E2F_FLAG_SIGNAL_MASK)
				return 0;
			return 1;
		} else
			not_fixed++;
		problem = 0;
	}

	if (inode.i_faddr) {
		if (fix_problem(ctx, PR_2_FADDR_ZERO, &pctx)) {
			inode.i_faddr = 0;
			inode_modified++;
		} else
			not_fixed++;
	}

	switch (fs->super->s_creator_os) {
	    case EXT2_OS_HURD:
		frag = &inode.osd2.hurd2.h_i_frag;
		fsize = &inode.osd2.hurd2.h_i_fsize;
		break;
	    default:
		frag = fsize = 0;
	}
	if (frag && *frag) {
		pctx.num = *frag;
		if (fix_problem(ctx, PR_2_FRAG_ZERO, &pctx)) {
			*frag = 0;
			inode_modified++;
		} else
			not_fixed++;
		pctx.num = 0;
	}
	if (fsize && *fsize) {
		pctx.num = *fsize;
		if (fix_problem(ctx, PR_2_FSIZE_ZERO, &pctx)) {
			*fsize = 0;
			inode_modified++;
		} else
			not_fixed++;
		pctx.num = 0;
	}

	if ((fs->super->s_creator_os == EXT2_OS_LINUX) &&
	    !ext2fs_has_feature_huge_file(fs->super) &&
	    (inode.osd2.linux2.l_i_blocks_hi != 0)) {
		pctx.num = inode.osd2.linux2.l_i_blocks_hi;
		if (fix_problem(ctx, PR_2_BLOCKS_HI_ZERO, &pctx)) {
			inode.osd2.linux2.l_i_blocks_hi = 0;
			inode_modified++;
		}
	}

	if ((fs->super->s_creator_os == EXT2_OS_LINUX) &&
	    !ext2fs_has_feature_64bit(fs->super) &&
	    inode.osd2.linux2.l_i_file_acl_high != 0) {
		pctx.num = inode.osd2.linux2.l_i_file_acl_high;
		if (fix_problem(ctx, PR_2_I_FILE_ACL_HI_ZERO, &pctx)) {
			inode.osd2.linux2.l_i_file_acl_high = 0;
			inode_modified++;
		} else
			not_fixed++;
	}

	if (ext2fs_file_acl_block(fs, &inode) &&
	    ((ext2fs_file_acl_block(fs, &inode) < fs->super->s_first_data_block) ||
	     (ext2fs_file_acl_block(fs, &inode) >= ext2fs_blocks_count(fs->super)))) {
		if (fix_problem(ctx, PR_2_FILE_ACL_BAD, &pctx)) {
			ext2fs_file_acl_block_set(fs, &inode, 0);
			inode_modified++;
		} else
			not_fixed++;
	}
	if (inode.i_size_high && !ext2fs_has_feature_largedir(fs->super) &&
	    LINUX_S_ISDIR(inode.i_mode)) {
		if (fix_problem(ctx, PR_2_DIR_SIZE_HIGH_ZERO, &pctx)) {
			inode.i_size_high = 0;
			inode_modified++;
		} else
			not_fixed++;
	}

	if (inode_modified)
		e2fsck_write_inode(ctx, ino, &inode, "process_bad_inode");
	if (!not_fixed && ctx->inode_bad_map)
		ext2fs_unmark_inode_bitmap2(ctx->inode_bad_map, ino);
	return 0;
}

/*
 * allocate_dir_block --- this function allocates a new directory
 * 	block for a particular inode; this is done if a directory has
 * 	a "hole" in it, or if a directory has a illegal block number
 * 	that was zeroed out and now needs to be replaced.
 */
static int allocate_dir_block(e2fsck_t ctx,
			      struct ext2_db_entry2 *db,
			      char *buf EXT2FS_ATTR((unused)),
			      struct problem_context *pctx)
{
	ext2_filsys fs = ctx->fs;
	blk64_t			blk = 0;
	char			*block;
	struct ext2_inode	inode;

	if (fix_problem(ctx, PR_2_DIRECTORY_HOLE, pctx) == 0)
		return 1;

	/*
	 * Read the inode and block bitmaps in; we'll be messing with
	 * them.
	 */
	e2fsck_read_bitmaps(ctx);

	/*
	 * First, find a free block
	 */
	e2fsck_read_inode(ctx, db->ino, &inode, "allocate_dir_block");
	pctx->errcode = ext2fs_map_cluster_block(fs, db->ino, &inode,
						 db->blockcnt, &blk);
	if (pctx->errcode || blk == 0) {
		blk = ext2fs_find_inode_goal(fs, db->ino, &inode, db->blockcnt);
		pctx->errcode = ext2fs_new_block2(fs, blk,
						  ctx->block_found_map, &blk);
		if (pctx->errcode) {
			pctx->str = "ext2fs_new_block";
			fix_problem(ctx, PR_2_ALLOC_DIRBOCK, pctx);
			return 1;
		}
	}
	ext2fs_mark_block_bitmap2(ctx->block_found_map, blk);
	ext2fs_mark_block_bitmap2(fs->block_map, blk);
	ext2fs_mark_bb_dirty(fs);

	/*
	 * Now let's create the actual data block for the inode
	 */
	if (db->blockcnt)
		pctx->errcode = ext2fs_new_dir_block(fs, 0, 0, &block);
	else
		pctx->errcode = ext2fs_new_dir_block(fs, db->ino,
						     EXT2_ROOT_INO, &block);

	if (pctx->errcode) {
		pctx->str = "ext2fs_new_dir_block";
		fix_problem(ctx, PR_2_ALLOC_DIRBOCK, pctx);
		return 1;
	}

	pctx->errcode = ext2fs_write_dir_block4(fs, blk, block, 0, db->ino);
	ext2fs_free_mem(&block);
	if (pctx->errcode) {
		pctx->str = "ext2fs_write_dir_block";
		fix_problem(ctx, PR_2_ALLOC_DIRBOCK, pctx);
		return 1;
	}

	/*
	 * Update the inode block count
	 */
	ext2fs_iblk_add_blocks(fs, &inode, 1);
	if (EXT2_I_SIZE(&inode) < ((__u64) db->blockcnt+1) * fs->blocksize) {
		pctx->errcode = ext2fs_inode_size_set(fs, &inode,
					(db->blockcnt+1) * fs->blocksize);
		if (pctx->errcode) {
			pctx->str = "ext2fs_inode_size_set";
			fix_problem(ctx, PR_2_ALLOC_DIRBOCK, pctx);
			return 1;
		}
	}
	e2fsck_write_inode(ctx, db->ino, &inode, "allocate_dir_block");

	/*
	 * Finally, update the block pointers for the inode
	 */
	db->blk = blk;
	pctx->errcode = ext2fs_bmap2(fs, db->ino, &inode, 0, BMAP_SET,
				     db->blockcnt, 0, &blk);
	if (pctx->errcode) {
		pctx->str = "ext2fs_block_iterate";
		fix_problem(ctx, PR_2_ALLOC_DIRBOCK, pctx);
		return 1;
	}

	return 0;
}
