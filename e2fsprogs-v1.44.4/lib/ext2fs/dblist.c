/*
 * dblist.c -- directory block list functions
 *
 * Copyright 1997 by Theodore Ts'o
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"
#include <stdio.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <string.h>
#include <time.h>

#include "ext2_fs.h"
#include "ext2fsP.h"

static EXT2_QSORT_TYPE dir_block_cmp(const void *a, const void *b);
static EXT2_QSORT_TYPE dir_block_cmp2(const void *a, const void *b);
static EXT2_QSORT_TYPE (*sortfunc32)(const void *a, const void *b);

/*
 * helper function for making a new directory block list (for
 * initialize and copy).
 */
//static 
errcode_t make_dblist(ext2_filsys fs, ext2_ino_t size,
			     ext2_ino_t count,
			     struct ext2_db_entry2 *list,
			     ext2_dblist *ret_dblist)
{
	ext2_dblist	dblist = NULL;
	errcode_t	retval;
	ext2_ino_t	num_dirs;
	size_t		len;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	if ((ret_dblist == 0) && fs->dblist &&
	    (fs->dblist->magic == EXT2_ET_MAGIC_DBLIST))
		return 0;

	retval = ext2fs_get_mem(sizeof(struct ext2_struct_dblist), &dblist);
	if (retval)
		goto cleanup;
	memset(dblist, 0, sizeof(struct ext2_struct_dblist));

	dblist->magic = EXT2_ET_MAGIC_DBLIST;
	dblist->fs = fs;

#ifdef _PIPELINE_PARALLELISM
	dblist->last_checked = 0;
	pthread_rwlock_init(&dblist->rwlock, NULL);
#endif

	if (size)
		dblist->size = size;
	else {
		retval = ext2fs_get_num_dirs(fs, &num_dirs);
		if (retval)
			goto cleanup;
		dblist->size = (num_dirs * 2) + 12;
	}
	len = (size_t) sizeof(struct ext2_db_entry2) * dblist->size;
	dblist->count = count;
	retval = ext2fs_get_array(dblist->size, sizeof(struct ext2_db_entry2),
		&dblist->list);
	if (retval)
		goto cleanup;

	if (list)
		memcpy(dblist->list, list, len);
	else
		memset(dblist->list, 0, len);
	if (ret_dblist)
		*ret_dblist = dblist;
	else
		fs->dblist = dblist;
	return 0;
cleanup:
	if (dblist)
		ext2fs_free_mem(&dblist);
	return retval;
}

/*
 * Initialize a directory block list
 */
errcode_t ext2fs_init_dblist(ext2_filsys fs, ext2_dblist *ret_dblist)
{
	ext2_dblist	dblist;
	errcode_t	retval;

	retval = make_dblist(fs, 0, 0, 0, &dblist);
	if (retval)
		return retval;

	dblist->sorted = 1;
	if (ret_dblist)
		*ret_dblist = dblist;
	else
		fs->dblist = dblist;

	return 0;
}

/*
 * Copy a directory block list
 */
errcode_t ext2fs_copy_dblist(ext2_dblist src, ext2_dblist *dest)
{
	ext2_dblist	dblist;
	errcode_t	retval;

	retval = make_dblist(src->fs, src->size, src->count, src->list,
			     &dblist);
	if (retval)
		return retval;
	dblist->sorted = src->sorted;
	*dest = dblist;
	return 0;
}

/*
 * Close a directory block list
 *
 * (moved to closefs.c)
 */


/*
 * Add a directory block to the directory block list
 */
errcode_t ext2fs_add_dir_block2(ext2_dblist dblist, ext2_ino_t ino,
				blk64_t blk, e2_blkcnt_t blockcnt)
{
	struct ext2_db_entry2 	*new_entry;
	errcode_t		retval;
	unsigned long		old_size;

	EXT2_CHECK_MAGIC(dblist, EXT2_ET_MAGIC_DBLIST);

	if (dblist->count >= dblist->size) {
		old_size = dblist->size * sizeof(struct ext2_db_entry2);
		dblist->size += dblist->size > 200 ? dblist->size / 2 : 100;
#ifdef _PIPELINE_PARALLELISM
		//fsck_lock(&dblist->dblist_lock);
		pthread_rwlock_wrlock(&dblist->rwlock);
#endif
		retval = ext2fs_resize_mem(old_size, (size_t) dblist->size *
					   sizeof(struct ext2_db_entry2),
					   &dblist->list);
#ifdef _PIPELINE_PARALLELISM
		//fsck_unlock(&dblist->dblist_lock);
		pthread_rwlock_unlock(&dblist->rwlock);
#endif
		if (retval) {
			dblist->size = old_size / sizeof(struct ext2_db_entry2);
			return retval;
		}
	}
	new_entry = dblist->list + ( dblist->count++);
	new_entry->blk = blk;
	new_entry->ino = ino;
	new_entry->blockcnt = blockcnt;

	dblist->sorted = 0;

	return 0;
}

/*
 * Change the directory block to the directory block list
 */
errcode_t ext2fs_set_dir_block2(ext2_dblist dblist, ext2_ino_t ino,
				blk64_t blk, e2_blkcnt_t blockcnt)
{
	dgrp_t			i;

	EXT2_CHECK_MAGIC(dblist, EXT2_ET_MAGIC_DBLIST);

	for (i=0; i < dblist->count; i++) {
		if ((dblist->list[i].ino != ino) ||
		    (dblist->list[i].blockcnt != blockcnt))
			continue;
		dblist->list[i].blk = blk;
		dblist->sorted = 0;
		return 0;
	}
	return EXT2_ET_DB_NOT_FOUND;
}

void ext2fs_dblist_sort2(ext2_dblist dblist,
			 EXT2_QSORT_TYPE (*sortfunc)(const void *,
						     const void *))
{
	if (!sortfunc)
		sortfunc = dir_block_cmp2;
	qsort(dblist->list, (size_t) dblist->count,
	      sizeof(struct ext2_db_entry2), sortfunc);
	dblist->sorted = 1;
}

/* old ext2ext2fs_dblist_iterate3
 * This function iterates over the directory block list
 */
errcode_t ext2fs_dblist_iterate4(ext2_dblist dblist,
				 int (*func)(ext2_filsys fs,
					     struct ext2_db_entry2 *db_info,
					     void	*priv_data),
				 unsigned long long start,
				 unsigned long long count,
				 void *priv_data, ext2_filsys fs)
{
	unsigned long long	i, end;
	int		ret;

	EXT2_CHECK_MAGIC(dblist, EXT2_ET_MAGIC_DBLIST);

	end = start + count;

#ifdef _PIPELINE_PARALLELISM
	//fsck_lock(&dblist->dblist_lock);
#endif

#ifndef _PIPELINE_PARALLELISM
	if (!dblist->sorted){
		//printf("WAS gonna sort dblist\n");
		ext2fs_dblist_sort2(dblist, 0);
	}
#endif
	if (end > dblist->count)
		end = dblist->count;

#ifdef _PIPELINE_PARALLELISM
	//fsck_unlock(&dblist->dblist_lock);
#endif
	
#ifdef _MIN_READ
	int j;
	int last_db_index_read = 0;
	int blocks_to_read;
	blk_t starting_blk;
	blk_t next_blk;
	blk_t next_db_blk;
	blk_t max_blk; 

	//printf("Gonna Check %d db entries\n", end - start);
#endif

	for (i = start; i < end; i++) {
#ifdef _PIPELINE_PARALLELISM
		//fsck_lock(&dblist->dblist_lock);
		pthread_rwlock_rdlock(&dblist->rwlock);
#endif

#ifdef _READ_DIR_BLOCKS
#ifdef _MIN_READ
		if(last_db_index_read < i){
			starting_blk = dblist->list[i].blk;
			blocks_to_read = 1;
			j = i + 1;
			next_blk = starting_blk + 1;	
			max_blk = starting_blk + (_READ_DIR_BLOCKS  - 1);  
			while(j < end  && j - i < _READ_DIR_BLOCKS){
				next_db_blk = dblist->list[j].blk;	
				if (next_db_blk < starting_blk || next_db_blk > max_blk){ 
					break;
				}
				if (next_db_blk != next_blk){
					break;
				}
				last_db_index_read = j;
				j++;
		       		next_blk++;
		       		blocks_to_read++;	
			}
			dblist->list[i].read_size = blocks_to_read;	
		}else{
			dblist->list[i].read_size = 1;
		}
#endif
#endif

#ifndef _PIPELINE_PARALLELISM
		ret = (*func)(dblist->fs, &dblist->list[i], priv_data);
#else
		if(fs){
			ret = (*func)(fs, &dblist->list[i], priv_data);
		} else{
			ret = (*func)(dblist->fs, &dblist->list[i], priv_data);
		}
#endif

#ifdef _PIPELINE_PARALLELISM
		//fsck_unlock(&dblist->dblist_lock);
		pthread_rwlock_unlock(&dblist->rwlock);
#endif
		if (ret & DBLIST_ABORT)
			return 0;
	}
	return 0;
}


errcode_t ext2fs_dblist_iterate3(ext2_dblist dblist,
				 int (*func)(ext2_filsys fs,
					     struct ext2_db_entry2 *db_info,
					     void	*priv_data),
				 unsigned long long start,
				 unsigned long long count,
				 void *priv_data){

	return ext2fs_dblist_iterate4(dblist, func, start, count,
				      priv_data, NULL);
}

/*
 * This function iterates over the directory block list
 */
errcode_t ext2fs_dblist_iterate3_stride(ext2_dblist dblist,
				 int (*func)(ext2_filsys fs,
					     struct ext2_db_entry2 *db_info,
					     void	*priv_data),
				 unsigned long long start,
				 unsigned long long count,
				 void *priv_data, int stride)
{
	unsigned long long	i, end;
	int		ret;

	EXT2_CHECK_MAGIC(dblist, EXT2_ET_MAGIC_DBLIST);

	end = start + count;
	if (!dblist->sorted)
		ext2fs_dblist_sort2(dblist, 0);
	if (end > dblist->count)
		end = dblist->count;
	for (i = start; i < end; i += stride) {
		ret = (*func)(dblist->fs, &dblist->list[i], priv_data);
		if (ret & DBLIST_ABORT)
			return 0;
	}
	return 0;
}


errcode_t ext2fs_dblist_iterate2(ext2_dblist dblist,
				 int (*func)(ext2_filsys fs,
					     struct ext2_db_entry2 *db_info,
					     void	*priv_data),
				 void *priv_data)
{
	return ext2fs_dblist_iterate3(dblist, func, 0, dblist->count,
				      priv_data);
}

errcode_t ext2fs_dblist_iterate2_stride(ext2_dblist dblist,
				 int (*func)(ext2_filsys fs,
					     struct ext2_db_entry2 *db_info,
					     void	*priv_data),
				 void *priv_data, int stride, int start)
{
	return ext2fs_dblist_iterate3_stride(dblist, func, start, dblist->count,
				      priv_data, stride);
}

static EXT2_QSORT_TYPE dir_block_cmp2(const void *a, const void *b)
{
	const struct ext2_db_entry2 *db_a =
		(const struct ext2_db_entry2 *) a;
	const struct ext2_db_entry2 *db_b =
		(const struct ext2_db_entry2 *) b;

	if (db_a->blk != db_b->blk)
		return (int) (db_a->blk - db_b->blk);

	if (db_a->ino != db_b->ino)
		return (int) (db_a->ino - db_b->ino);

	return (db_a->blockcnt - db_b->blockcnt);
}

blk64_t ext2fs_dblist_count2(ext2_dblist dblist)
{
	return dblist->count;
}

errcode_t ext2fs_dblist_get_last2(ext2_dblist dblist,
				  struct ext2_db_entry2 **entry)
{
	EXT2_CHECK_MAGIC(dblist, EXT2_ET_MAGIC_DBLIST);

	if (dblist->count == 0)
		return EXT2_ET_DBLIST_EMPTY;

	if (entry)
		*entry = dblist->list + ( dblist->count-1);
	return 0;
}

errcode_t ext2fs_dblist_drop_last(ext2_dblist dblist)
{
	EXT2_CHECK_MAGIC(dblist, EXT2_ET_MAGIC_DBLIST);

	if (dblist->count == 0)
		return EXT2_ET_DBLIST_EMPTY;

	dblist->count--;
	return 0;
}

/*
 * Legacy 32-bit versions
 */

/*
 * Add a directory block to the directory block list
 */
errcode_t ext2fs_add_dir_block(ext2_dblist dblist, ext2_ino_t ino, blk_t blk,
			       int blockcnt)
{
	return ext2fs_add_dir_block2(dblist, ino, blk, blockcnt);
}

/*
 * Change the directory block to the directory block list
 */
errcode_t ext2fs_set_dir_block(ext2_dblist dblist, ext2_ino_t ino, blk_t blk,
			       int blockcnt)
{
	return ext2fs_set_dir_block2(dblist, ino, blk, blockcnt);
}

void ext2fs_dblist_sort(ext2_dblist dblist,
			EXT2_QSORT_TYPE (*sortfunc)(const void *,
						    const void *))
{
	if (sortfunc) {
		sortfunc32 = sortfunc;
		sortfunc = dir_block_cmp;
	} else
		sortfunc = dir_block_cmp2;
	qsort(dblist->list, (size_t) dblist->count,
	      sizeof(struct ext2_db_entry2), sortfunc);
	dblist->sorted = 1;
}

/*
 * This function iterates over the directory block list
 */
struct iterate_passthrough {
	int (*func)(ext2_filsys fs,
		    struct ext2_db_entry *db_info,
		    void	*priv_data);
	void *priv_data;
};

static int passthrough_func(ext2_filsys fs,
			    struct ext2_db_entry2 *db_info,
			    void	*priv_data)
{
	struct iterate_passthrough *p = priv_data;
	struct ext2_db_entry db;
	int ret;

	db.ino = db_info->ino;
	db.blk = (blk_t) db_info->blk;
	db.blockcnt = (int) db_info->blockcnt;
	ret = (p->func)(fs, &db, p->priv_data);
	db_info->ino = db.ino;
	db_info->blk = db.blk;
	db_info->blockcnt = db.blockcnt;
	return ret;
}

errcode_t ext2fs_dblist_iterate(ext2_dblist dblist,
				int (*func)(ext2_filsys fs,
					    struct ext2_db_entry *db_info,
					    void	*priv_data),
				void *priv_data)
{
	struct iterate_passthrough pass;

	EXT2_CHECK_MAGIC(dblist, EXT2_ET_MAGIC_DBLIST);
	pass.func = func;
	pass.priv_data = priv_data;

	return ext2fs_dblist_iterate2(dblist, passthrough_func, &pass);
}

static EXT2_QSORT_TYPE dir_block_cmp(const void *a, const void *b)
{
	const struct ext2_db_entry2 *db_a =
		(const struct ext2_db_entry2 *) a;
	const struct ext2_db_entry2 *db_b =
		(const struct ext2_db_entry2 *) b;

	struct ext2_db_entry a32, b32;

	a32.ino = db_a->ino;  a32.blk = db_a->blk;
	a32.blockcnt = db_a->blockcnt;

	b32.ino = db_b->ino;  b32.blk = db_b->blk;
	b32.blockcnt = db_b->blockcnt;

	return sortfunc32(&a32, &b32);
}

int ext2fs_dblist_count(ext2_dblist dblist)
{
	return dblist->count;
}

errcode_t ext2fs_dblist_get_last(ext2_dblist dblist,
				 struct ext2_db_entry **entry)
{
	static struct ext2_db_entry ret_entry;
	struct ext2_db_entry2 *last;

	EXT2_CHECK_MAGIC(dblist, EXT2_ET_MAGIC_DBLIST);

	if (dblist->count == 0)
		return EXT2_ET_DBLIST_EMPTY;

	if (!entry)
		return 0;

	last = dblist->list + dblist->count -1;

	ret_entry.ino = last->ino;
	ret_entry.blk = last->blk;
	ret_entry.blockcnt = last->blockcnt;
	*entry = &ret_entry;

	return 0;
}

// Merges source list into destination dblist
// does not free source dblist
void ext2fs_dblist_merge(ext2_dblist dest, ext2_dblist source){

	dest->sorted = 0;

	int new_size = dest->count + source->count;
	struct ext2_db_entry2 *new_list;
	struct ext2_db_entry2 *sec_start;

	new_list = (struct ext2_db_entry2 *) malloc(sizeof(struct ext2_db_entry2) * new_size);

	memcpy(new_list, dest->list, sizeof(struct ext2_db_entry2) * dest->count);	// copy first list
  memcpy(&new_list[dest->count], source->list, sizeof(struct ext2_db_entry2) * source->count);	 // copy source

	// free original list
	free(dest->list);

	// Increment count
	dest->size = new_size;
	dest->count = new_size;

	// Assign new list
	dest->list = new_list;

}

void 	ext2fs_dblist_set_fs(ext2_dblist dblist, ext2_filsys fs){
	dblist->fs = fs;
}
