/*
 * dirblock.c --- directory block routines.
 *
 * Copyright (C) 1995, 1996 Theodore Ts'o.
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
#include "ext2fs.h"

#include <sys/syscall.h>
#include <sched.h>

#include <pthread.h>
static int cache_hits = 0;
static int read_blocks = 0;
static pthread_mutex_t cache_lock;

void print_dir_block_cache_statistics(){
	printf("Total Dir blocks Read in = %d\n", read_blocks);
	printf("Cache Hits = %d\n", cache_hits);
	printf("Cache Hit Ratio = %f\n", (((double) cache_hits) / ((double) read_blocks)));
}


errcode_t ext2fs_read_dir_block5(ext2_filsys fs, blk64_t block,
				 void *buf, int flags EXT2FS_ATTR((unused)),
				 ext2_ino_t ino, int read_count)
{
	errcode_t	retval;
	int		corrupt = 0;

#ifdef _READ_DIR_BLOCKS
        //printf("Thread %d going to read %llu ", syscall(SYS_gettid), block); //;sched_getcpu());	
	//pthread_mutex_lock(&cache_lock);
	//printf("Gonna read %d dir blocks\n", _READ_DIR_BLOCKS);
	//printf("Gonna Read Block %lu on CPU %d\n", block, sched_getcpu());
	// check cache for block and copy into buf
	//printf("passing in buf %p\n", buf);	
	if(!(io_channel_check_cache_blk(fs->io, block, buf))){
		//read_blocks += _READ_DIR_BLOCKS;
		// if not in cache try and read that and how many blocks ahead
		//printf("We can read ahead %d blocks\n", read_count);
		//printf("(MISS) reading in %d blocks\n", read_count);
#ifdef _MIN_READ
		retval = io_channel_read_blk64(fs->io, block, read_count, buf);
#else
		retval = io_channel_read_blk64(fs->io, block, _READ_DIR_BLOCKS, buf);
#endif
	}else{
		//cache_hits++;
		// make sure retval si good if we suceedfuly got it from cache
		//printf("(HIT)\n");
		retval = 0;
	}
	//pthread_mutex_unlock(&cache_lock);
#else
	retval = io_channel_read_blk64(fs->io, block, 1, buf);
#endif

	if (retval)
		return retval;

	if (!(fs->flags & EXT2_FLAG_IGNORE_CSUM_ERRORS) &&
	    !ext2fs_dir_block_csum_verify(fs, ino,
					  (struct ext2_dir_entry *)buf))
		corrupt = 1;

#ifdef WORDS_BIGENDIAN
	retval = ext2fs_dirent_swab_in(fs, buf, flags);
#endif
	if (!retval && corrupt)
		retval = EXT2_ET_DIR_CSUM_INVALID;
	return retval;
}


errcode_t ext2fs_read_dir_block4(ext2_filsys fs, blk64_t block,
				  void *buf, int flags EXT2FS_ATTR((unused)),
				  ext2_ino_t ino)
{
	return ext2fs_read_dir_block5(fs, block, buf, flags, ino, 1);
}

errcode_t ext2fs_read_dir_block3(ext2_filsys fs, blk64_t block,
				 void *buf, int flags EXT2FS_ATTR((unused)))
{
	return ext2fs_read_dir_block4(fs, block, buf, flags, 0);
}

errcode_t ext2fs_read_dir_block2(ext2_filsys fs, blk_t block,
				 void *buf, int flags EXT2FS_ATTR((unused)))
{
	return ext2fs_read_dir_block3(fs, block, buf, flags);
}

errcode_t ext2fs_read_dir_block(ext2_filsys fs, blk_t block,
				 void *buf)
{
	return ext2fs_read_dir_block3(fs, block, buf, 0);
}


errcode_t ext2fs_write_dir_block4(ext2_filsys fs, blk64_t block,
				  void *inbuf, int flags EXT2FS_ATTR((unused)),
				  ext2_ino_t ino)
{
	errcode_t	retval;
	char		*buf = inbuf;

#ifdef WORDS_BIGENDIAN
	retval = ext2fs_get_mem(fs->blocksize, &buf);
	if (retval)
		return retval;
	memcpy(buf, inbuf, fs->blocksize);
	retval = ext2fs_dirent_swab_out(fs, buf, flags);
	if (retval)
		return retval;
#endif
	retval = ext2fs_dir_block_csum_set(fs, ino,
					   (struct ext2_dir_entry *)buf);
	if (retval)
		goto out;

	retval = io_channel_write_blk64(fs->io, block, 1, buf);

out:
#ifdef WORDS_BIGENDIAN
	ext2fs_free_mem(&buf);
#endif
	return retval;
}

errcode_t ext2fs_write_dir_block3(ext2_filsys fs, blk64_t block,
				  void *inbuf, int flags EXT2FS_ATTR((unused)))
{
	return ext2fs_write_dir_block4(fs, block, inbuf, flags, 0);
}

errcode_t ext2fs_write_dir_block2(ext2_filsys fs, blk_t block,
				 void *inbuf, int flags EXT2FS_ATTR((unused)))
{
	return ext2fs_write_dir_block3(fs, block, inbuf, flags);
}

errcode_t ext2fs_write_dir_block(ext2_filsys fs, blk_t block,
				 void *inbuf)
{
	return ext2fs_write_dir_block3(fs, block, inbuf, 0);
}
