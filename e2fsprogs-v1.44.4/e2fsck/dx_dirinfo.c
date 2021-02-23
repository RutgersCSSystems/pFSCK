/*
 * dirinfo.c --- maintains the directory information table for e2fsck.
 *
 * Copyright (C) 1993 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#include "config.h"
#include "e2fsck.h"

pthread_rwlock_t rwlock; // Static read/write lock

/* We use a static rwlock as dynamically allocated mutexes
 * or rwlocks held within the dx_dir_info struct in ctx
 * lead to a case where a thread waits on a lock that is 
 * not locked or unaccquired. Still unsure why
 */

#ifdef _PIPELINE_PARALLELISM
/* This function initializes a dx_dir list within the main context
 * Assumes this will be called before any multithreading
 */
void e2fsck_init_dx_dir(e2fsck_t ctx){

	if (!ctx->dx_dir_info) {
		printf("Initializing dx_dir\n");
		ctx->dx_dir_info_count = 0;
		ctx->dx_dir_info_size = 100; /* Guess */
		ctx->dx_dir_info  = (struct dx_dir_info **)
			e2fsck_allocate_memory(ctx, ctx->dx_dir_info_size
					       * sizeof (struct dx_dir_info*),
					       "directory map");
	}
}
#endif


/*
 * This subroutine is called during pass1 to create a directory info
 * entry.  During pass1, the passed-in parent is 0; it will get filled
 * in during pass2.
 */
void e2fsck_add_dx_dir(e2fsck_t ctx, ext2_ino_t ino, int num_blocks)
{
	struct dx_dir_info *dir;
	int		i, j;
	errcode_t	retval;
	unsigned long	old_size;

#if 0
	printf("add_dx_dir_info for inode %lu...\n", ino);
#endif

#ifndef _PIPELINE_PARALLEISM
	if (!ctx->dx_dir_info) {
		ctx->dx_dir_info_count = 0;
		ctx->dx_dir_info_size = 100; /* Guess */
		ctx->dx_dir_info  = (struct dx_dir_info **)
			e2fsck_allocate_memory(ctx, ctx->dx_dir_info_size
					       * sizeof (struct dx_dir_info*),
					       "directory map");
	}
#endif

#ifdef _PIPELINE_PARALLELISM
		pthread_rwlock_wrlock(&rwlock);
#endif


	if (ctx->dx_dir_info_count >= ctx->dx_dir_info_size) {
		old_size = ctx->dx_dir_info_size * sizeof(struct dx_dir_info*);
		ctx->dx_dir_info_size += 10;

		retval = ext2fs_resize_mem(old_size, ctx->dx_dir_info_size *
					   sizeof(struct dx_dir_info*),
					   &ctx->dx_dir_info);

		if (retval) {
			fprintf(stderr, "Couldn't reallocate dx_dir_info "
				"structure to %d entries\n",
				ctx->dx_dir_info_size);
			fatal_error(ctx, 0);
			ctx->dx_dir_info_size -= 10;

#ifdef _PIPELINE_PARALLELISM
			pthread_rwlock_unlock(&rwlock);
#endif		
			return;
		}
	}

	// Create new dx_dir
	dir = (struct dx_dir_info*) malloc(sizeof(struct dx_dir_info));
	dir->ino = ino;
	dir->numblocks = num_blocks;
	dir->hashversion = 0;
	dir->dx_block = e2fsck_allocate_memory(ctx, num_blocks
				       * sizeof (struct dx_dirblock_info),
				       "dx_block info array");

	/*
	 * Normally, add_dx_dir_info is called with each inode in
	 * sequential order; but once in a while (like when pass 3
	 * needs to recreate the root directory or lost+found
	 * directory) it is called out of order.  In those cases, we
	 * need to move the dx_dir_info entries down to make room, since
	 * the dx_dir_info array needs to be sorted by inode number for
	 * get_dx_dir_info()'s sake.
	 */
	if (ctx->dx_dir_info_count &&
	    ctx->dx_dir_info[ctx->dx_dir_info_count-1]->ino >= ino) {
		for (i = ctx->dx_dir_info_count-1; i > 0; i--)
			if (ctx->dx_dir_info[i-1]->ino < ino)
				break;
		//dir = &ctx->dx_dir_info[i];
		if (ctx->dx_dir_info[i]->ino != ino){
			for (j = ctx->dx_dir_info_count++; j > i; j--)
				ctx->dx_dir_info[j] = ctx->dx_dir_info[j-1];
			ctx->dx_dir_info[i] = dir;
		}else{
			printf("Shouldn not get here, adding same dx_dir_info ino\n");
		}
	} else{
		//dir = &ctx->dx_dir_info[ctx->dx_dir_info_count++];
		ctx->dx_dir_info[ctx->dx_dir_info_count++] = dir;
	}

	// if there exists entries in the dx_dir_info and
	// the current ino being added is LESS or EQUAL than the ino of the last dx_dir_info added:
	//    find the spot where current ino is greater than in list
	//    grab the ino 
	
	//dir->ino = ino;
	//dir->numblocks = num_blocks;
	//dir->hashversion = 0;
	//dir->dx_block = e2fsck_allocate_memory(ctx, num_blocks
	//			       * sizeof (struct dx_dirblock_info),
	//			       "dx_block info array");
#ifdef _PIPELINE_PARALLELISM
	pthread_rwlock_unlock(&rwlock);
#endif

}


/*
 * get_dx_dir_info() --- given an inode number, try to find the directory
 * information entry for it.
 */
struct dx_dir_info *e2fsck_get_dx_dir_info(e2fsck_t ctx, ext2_ino_t ino)
{
	int	low, high, mid;

	struct dx_dir_info *ret = NULL;

	if (!ctx->dx_dir_info)
		return 0;

#ifdef _PIPELINE_PARALLELISM
	pthread_rwlock_rdlock(&rwlock);
#endif	

	low = 0;
	high = ctx->dx_dir_info_count-1;
	
	if (ino == ctx->dx_dir_info[low]->ino){

		ret = ctx->dx_dir_info[low];	
#ifdef _PIPELINE_PARALLELISM
		pthread_rwlock_unlock(&rwlock);
#endif 
		return ret;
		//return ctx->dx_dir_info[low];
	}
	if  (ino == ctx->dx_dir_info[high]->ino){

		ret = ctx->dx_dir_info[high];		
#ifdef _PIPELINE_PARALLELISM
		pthread_rwlock_unlock(&rwlock);
#endif 
		return ret;
		//return ctx->dx_dir_info[high];
	}

	while (low < high) {
		mid = (low+high)/2;
		if (mid == low || mid == high)
			break;
		if (ino == ctx->dx_dir_info[mid]->ino){		

			ret = ctx->dx_dir_info[mid];
#ifdef _PIPELINE_PARALLELISM
			pthread_rwlock_unlock(&rwlock);
#endif 
			return ret;
			//return ctx->dx_dir_info[mid];
		}
		if (ino < ctx->dx_dir_info[mid]->ino)
			high = mid;
		else
			low = mid;
	}

#ifdef _PIPELINE_PARALLELISM
	pthread_rwlock_unlock(&rwlock);
#endif 

	return 0;
}

/*
 * Free the dx_dir_info structure when it isn't needed any more.
 */
void e2fsck_free_dx_dir_info(e2fsck_t ctx)
{
	int	i;
	struct dx_dir_info *dir;

	// No locking here as we assume this will be done serially

	if (ctx->dx_dir_info) {
		for (i=0; i < ctx->dx_dir_info_count; i++) {
			dir = ctx->dx_dir_info[i];
			if (dir->dx_block) {
				ext2fs_free_mem(&dir->dx_block);
				dir->dx_block = 0;
			}
			free(dir);
		}
		ext2fs_free_mem(&ctx->dx_dir_info);
		ctx->dx_dir_info = 0;
	}
	ctx->dx_dir_info_size = 0;
	ctx->dx_dir_info_count = 0;
}

/*
 * Return the count of number of directories in the dx_dir_info structure
 */
int e2fsck_get_num_dx_dirinfo(e2fsck_t ctx)
{
	return ctx->dx_dir_info_count;
}

/*
 * A simple interator function
 */
struct dx_dir_info *e2fsck_dx_dir_info_iter(e2fsck_t ctx, int *control)
{
	if (*control >= ctx->dx_dir_info_count)
		return 0;

	return(ctx->dx_dir_info[(*control)++]);

}
