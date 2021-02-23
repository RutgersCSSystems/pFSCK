/*
 * dirinfo.c --- maintains the directory information table for e2fsck.
 *
 * Copyright (C) 1993 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#undef DIRINFO_DEBUG

#include "config.h"
#include "e2fsck.h"
#include <sys/stat.h>
#include <fcntl.h>
#include "uuid/uuid.h"

#include "ext2fs/ext2fs.h"
#include <ext2fs/tdb.h>

struct dir_info_db {
	int		count;
	int		size;
	struct dir_info *array;
	struct dir_info *last_lookup;
#ifdef CONFIG_TDB
	char		*tdb_fn;
	TDB_CONTEXT	*tdb;
#endif

#ifdef _PIPELINE_PARALLELISM
	fsck_lock_t dir_info_lock;
	pthread_rwlock_t rwlock;
#endif

};

struct dir_info_iter {
	int	i;
#ifdef CONFIG_TDB
	TDB_DATA	tdb_iter;
#endif
};

struct dir_info_ent {
	ext2_ino_t		dotdot;	/* Parent according to '..' */
	ext2_ino_t		parent; /* Parent according to treewalk */
};


static void e2fsck_put_dir_info(e2fsck_t ctx, struct dir_info *dir);

#ifdef CONFIG_TDB
static void setup_tdb(e2fsck_t ctx, ext2_ino_t num_dirs)
{
	struct dir_info_db	*db = ctx->dir_info;
	unsigned int		threshold;
	errcode_t		retval;
	mode_t			save_umask;
	char			*tdb_dir, uuid[40];
	int			fd, enable;

	profile_get_string(ctx->profile, "scratch_files", "directory", 0, 0,
			   &tdb_dir);
	profile_get_uint(ctx->profile, "scratch_files",
			 "numdirs_threshold", 0, 0, &threshold);
	profile_get_boolean(ctx->profile, "scratch_files",
			    "dirinfo", 0, 1, &enable);

	if (!enable || !tdb_dir || access(tdb_dir, W_OK) ||
	    (threshold && num_dirs <= threshold))
		return;

	retval = ext2fs_get_mem(strlen(tdb_dir) + 64, &db->tdb_fn);
	if (retval)
		return;

	uuid_unparse(ctx->fs->super->s_uuid, uuid);
	sprintf(db->tdb_fn, "%s/%s-dirinfo-XXXXXX", tdb_dir, uuid);
	save_umask = umask(077);
	fd = mkstemp(db->tdb_fn);
	umask(save_umask);
	if (fd < 0) {
		db->tdb = NULL;
		return;
	}

	if (num_dirs < 99991)
		num_dirs = 99991; /* largest 5 digit prime */

	db->tdb = tdb_open(db->tdb_fn, num_dirs, TDB_NOLOCK | TDB_NOSYNC,
			   O_RDWR | O_CREAT | O_TRUNC, 0600);
	close(fd);
}
#endif


void setup_db_directly(e2fsck_t ctx, struct dir_info_db	**dir_info)
{
	struct dir_info_db	*db;
	ext2_ino_t		num_dirs;
	errcode_t		retval;

	db = (struct dir_info_db *)
		e2fsck_allocate_memory(ctx, sizeof(struct dir_info_db),
				       "directory map db");
	db->count = db->size = 0;
	db->array = 0;

#ifdef _PIPELINE_PARALLELISM
        pthread_rwlock_init(&db->rwlock, NULL);
#endif

	//ctx->dir_info = db;
	*dir_info = db;

	retval = ext2fs_get_num_dirs(ctx->fs, &num_dirs);
	if (retval)
		num_dirs = 1024;	/* Guess */

#ifdef CONFIG_TDB
	setup_tdb(ctx, num_dirs);

	if (db->tdb) {
#ifdef DIRINFO_DEBUG
		printf("Note: using tdb!\n");
#endif
		return;
	}
#endif

	db->size = num_dirs + 10;
	db->array  = (struct dir_info *)
		e2fsck_allocate_memory(ctx, db->size
				       * sizeof (struct dir_info),
				       "directory map");
}


static void setup_db(e2fsck_t ctx)
{
	struct dir_info_db	*db;
	ext2_ino_t		num_dirs;
	errcode_t		retval;

	db = (struct dir_info_db *)
		e2fsck_allocate_memory(ctx, sizeof(struct dir_info_db),
				       "directory map db");
	db->count = db->size = 0;
	db->array = 0;

#ifdef _PIPELINE_PARALLELISM
	pthread_rwlock_init(&db->rwlock, NULL);
#endif

	ctx->dir_info = db;

	retval = ext2fs_get_num_dirs(ctx->fs, &num_dirs);
	if (retval)
		num_dirs = 1024;	/* Guess */

#ifdef CONFIG_TDB
	setup_tdb(ctx, num_dirs);

	if (db->tdb) {
#ifdef DIRINFO_DEBUG
		printf("Note: using tdb!\n");
#endif
		return;
	}
#endif

	db->size = num_dirs + 10;
	db->array  = (struct dir_info *)
		e2fsck_allocate_memory(ctx, db->size
				       * sizeof (struct dir_info),
				       "directory map");
}


struct dir_info* e2fsck_create_dir_info_single(ext2_ino_t ino, ext2_ino_t parent){

	printf("Creating dir info single\n");
	struct dir_info *dir;

	ext2fs_get_mem(sizeof(struct dir_info), &dir);
	dir->ino = ino;
	dir->dotdot = parent;
	dir->parent = parent;

	return dir;
}

void e2fsck_free_dir_info_single(struct dir_info* dir){
	printf("Freeing dir info single\n");
	ext2fs_free_mem(&dir);
}

/*
 * This subroutine is called during pass1 to create a directory info
 * entry.  During pass1, the passed-in parent is 0; it will get filled
 * in during pass2.
 */
void e2fsck_add_dir_info_directly(e2fsck_t ctx, struct dir_info_db	*dir_info, ext2_ino_t ino, ext2_ino_t parent)
{
	struct dir_info		*dir, *old_array;
	int			i, j;
	errcode_t		retval;
	unsigned long		old_size;

#ifdef DIRINFO_DEBUG
	printf("add_dir_info for inode (%lu, %lu)...\n", ino, parent);
#endif
	//if (!ctx->dir_info)
	//	setup_db(ctx);

	if (dir_info->count >= dir_info->size) {
		old_size = dir_info->size * sizeof(struct dir_info);
		dir_info->size += 10000;
		old_array = dir_info->array;

#ifdef _PIPELINE_PARALLELISM
		//fsck_lock(&dir_info->dir_info_lock);
		pthread_rwlock_wrlock(&dir_info->rwlock);
#endif

		retval = ext2fs_resize_mem(old_size, dir_info->size *
					   sizeof(struct dir_info),
					   &dir_info->array);

#ifdef _PIPELINE_PARALLELISM
		//fsck_unlock(&dir_info->dir_info_lock);
		pthread_rwlock_unlock(&dir_info->rwlock);
#endif

		if (retval) {
			fprintf(stderr, "Couldn't reallocate dir_info "
				"structure to %d entries\n",
				dir_info->size);
			fatal_error(ctx, 0);
			dir_info->size -= 10;
			return;
		}
		if (old_array != dir_info->array)
			dir_info->last_lookup = NULL;
	}

#ifdef CONFIG_TDB
	if (dir_info->tdb) {
		struct dir_info ent;

		ent.ino = ino;
		ent.parent = parent;
		ent.dotdot = parent;
		e2fsck_put_dir_info(ctx, &ent);
		return;
	}
#endif

	/*
	 * Normally, add_dir_info is called with each inode in
	 * sequential order; but once in a while (like when pass 3
	 * needs to recreate the root directory or lost+found
	 * directory) it is called out of order.  In those cases, we
	 * need to move the dir_info entries down to make room, since
	 * the dir_info array needs to be sorted by inode number for
	 * get_dir_info()'s sake.
	 */
	if (dir_info->count &&
	    dir_info->array[dir_info->count-1].ino >= ino) {
		for (i = dir_info->count-1; i > 0; i--)
			if (dir_info->array[i-1].ino < ino)
				break;
		dir = &dir_info->array[i];
		if (dir->ino != ino)
			for (j = dir_info->count++; j > i; j--)
				dir_info->array[j] = dir_info->array[j-1];
	} else
		dir = &dir_info->array[dir_info->count++];

	dir->ino = ino;
	dir->dotdot = parent;
	dir->parent = parent;

}

/*
 * This subroutine is called during pass1 to create a directory info
 * entry.  During pass1, the passed-in parent is 0; it will get filled
 * in during pass2.
 */
void e2fsck_add_dir_info(e2fsck_t ctx, ext2_ino_t ino, ext2_ino_t parent)
{
	struct dir_info		*dir, *old_array;
	int			i, j;
	errcode_t		retval;
	unsigned long		old_size;

#ifdef DIRINFO_DEBUG
	printf("add_dir_info for inode (%lu, %lu)...\n", ino, parent);
#endif
	if (!ctx->dir_info)
		setup_db(ctx);

	if (ctx->dir_info->count >= ctx->dir_info->size) {
		old_size = ctx->dir_info->size * sizeof(struct dir_info);
		ctx->dir_info->size += 10000;
		old_array = ctx->dir_info->array;

#ifdef _PIPELINE_PARALLELISM
		//fsck_lock(&ctx->dir_info->dir_info_lock);
		pthread_rwlock_wrlock(&ctx->dir_info->rwlock);
#endif

		retval = ext2fs_resize_mem(old_size, ctx->dir_info->size *
					   sizeof(struct dir_info),
					   &ctx->dir_info->array);

#ifdef _PIPELINE_PARALLELISM
		pthread_rwlock_unlock(&ctx->dir_info->rwlock);
#endif

		if (retval) {
			fprintf(stderr, "Couldn't reallocate dir_info "
				"structure to %d entries\n",
				ctx->dir_info->size);
			fatal_error(ctx, 0);
			ctx->dir_info->size -= 10;
			return;
		}
		if (old_array != ctx->dir_info->array)
			ctx->dir_info->last_lookup = NULL;
	}

#ifdef CONFIG_TDB
	if (ctx->dir_info->tdb) {
		struct dir_info ent;

		ent.ino = ino;
		ent.parent = parent;
		ent.dotdot = parent;
		e2fsck_put_dir_info(ctx, &ent);
		return;
	}
#endif


	/*
	 * Normally, add_dir_info is called with each inode in
	 * sequential order; but once in a while (like when pass 3
	 * needs to recreate the root directory or lost+found
	 * directory) it is called out of order.  In those cases, we
	 * need to move the dir_info entries down to make room, since
	 * the dir_info array needs to be sorted by inode number for
	 * get_dir_info()'s sake.
	 */
	if (ctx->dir_info->count &&
	    ctx->dir_info->array[ctx->dir_info->count-1].ino >= ino) {
		for (i = ctx->dir_info->count-1; i > 0; i--)
			if (ctx->dir_info->array[i-1].ino < ino)
				break;
		dir = &ctx->dir_info->array[i];
		if (dir->ino != ino)
			for (j = ctx->dir_info->count++; j > i; j--)
				ctx->dir_info->array[j] = ctx->dir_info->array[j-1];
	} else
		dir = &ctx->dir_info->array[ctx->dir_info->count++];

	dir->ino = ino;
	dir->dotdot = parent;
	dir->parent = parent;

}

//
// void e2fsck_add_dir_info(e2fsck_t ctx, ext2_ino_t ino, ext2_ino_t parent){
// 	e2fsck_add_dir_info_directly(ctx, ext2_ino_t ino, parent, NULL);
// }


/* old e2fsck_get_dir_info
 * get_dir_info() --- given an inode number, try to find the directory
 * information entry for it.
 */
static struct dir_info *e2fsck_get_dir_info_directly(e2fsck_t ctx, ext2_ino_t ino, struct dir_info_db* dir_info)
{
	struct dir_info_db	*db;
	struct dir_info_db	*dir_info2;
	int			low, high, mid;

	if(dir_info){
		db = dir_info;
	} else{
		db = ctx->dir_info;
	}

	if (!db)
		return 0;


#ifdef DIRINFO_DEBUG
	printf("e2fsck_get_dir_info %d...", ino);
#endif

#ifdef CONFIG_TDB
	if (db->tdb) {
		static struct dir_info	ret_dir_info;
		TDB_DATA key, data;
		struct dir_info_ent	*buf;

		key.dptr = (unsigned char *) &ino;
		key.dsize = sizeof(ext2_ino_t);

		data = tdb_fetch(db->tdb, key);
		if (!data.dptr) {
			if (tdb_error(db->tdb) != TDB_ERR_NOEXIST)
				printf("fetch failed: %s\n",
				       tdb_errorstr(db->tdb));
			return 0;
		}

		buf = (struct dir_info_ent *) data.dptr;
		ret_dir_info.ino = ino;
		ret_dir_info.dotdot = buf->dotdot;
		ret_dir_info.parent = buf->parent;
#ifdef DIRINFO_DEBUG
		printf("(%d,%d,%d)\n", ino, buf->dotdot, buf->parent);
#endif
		free(data.dptr);
		return &ret_dir_info;
	}
#endif

	if (db->last_lookup && db->last_lookup->ino == ino)
		return db->last_lookup;

	if(dir_info){
		dir_info2 = dir_info;
	} else{
		dir_info2 = ctx->dir_info;
	}

	low = 0;
	high = dir_info2->count-1;
	if (ino == dir_info2->array[low].ino) {
#ifdef DIRINFO_DEBUG
		printf("(%d,%d,%d)\n", ino,
		       dir_info2->array[low].dotdot,
		       dir_info2->array[low].parent);
#endif
		return &dir_info2->array[low];
	}
	if (ino == dir_info2->array[high].ino) {
#ifdef DIRINFO_DEBUG
		printf("(%d,%d,%d)\n", ino,
		       dir_info2->array[high].dotdot,
		       dir_info2->array[high].parent);
#endif
		return &dir_info2->array[high];
	}

	while (low < high) {
		mid = (low+high)/2;
		if (mid == low || mid == high)
			break;
		if (ino == dir_info2->array[mid].ino) {
#ifdef DIRINFO_DEBUG
			printf("(%d,%d,%d)\n", ino,
			       dir_info2->array[mid].dotdot,
			       dir_info2->array[mid].parent);
#endif
			return &dir_info2->array[mid];
		}
		if (ino < dir_info2->array[mid].ino)
			high = mid;
		else
			low = mid;
	}
	return 0;
}

static struct dir_info *e2fsck_get_dir_info(e2fsck_t ctx, ext2_ino_t ino){
	return e2fsck_get_dir_info_directly(ctx, ino, NULL);
}

// old e2fsck_put_dir_info
static void e2fsck_put_dir_info_directly(e2fsck_t ctx EXT2FS_NO_TDB_UNUSED,
				struct dir_info *dir EXT2FS_NO_TDB_UNUSED, struct dir_info_db* dir_info)
{
#ifdef CONFIG_TDB
	struct dir_info_db	*db;
	struct dir_info_ent	buf;
	TDB_DATA		key, data;

	if(dir_info){
		db = dir_info;
	} else{
		db = ctx->dir_info;
	}

#endif

#ifdef DIRINFO_DEBUG
	printf("e2fsck_put_dir_info (%d, %d, %d)...", dir->ino, dir->dotdot,
	       dir->parent);
#endif

#ifdef CONFIG_TDB
	if (!db->tdb)
		return;

	printf("DIR_INFO TDB USED\n");

	buf.parent = dir->parent;
	buf.dotdot = dir->dotdot;

	key.dptr = (unsigned char *) &dir->ino;
	key.dsize = sizeof(ext2_ino_t);
	data.dptr = (unsigned char *) &buf;
	data.dsize = sizeof(buf);

	if (tdb_store(db->tdb, key, data, TDB_REPLACE) == -1) {
		printf("store failed: %s\n", tdb_errorstr(db->tdb));
	}
#endif
}

static void e2fsck_put_dir_info(e2fsck_t ctx EXT2FS_NO_TDB_UNUSED,
				struct dir_info *dir EXT2FS_NO_TDB_UNUSED){
		e2fsck_put_dir_info_directly(ctx, dir, NULL);
}

/*
 * Free the dir_info structure when it isn't needed any more.
 */
void e2fsck_free_dir_info_directly(struct dir_info_db	**db)
{
	struct dir_info_db *dir_info = *db;

	if (dir_info) {
#ifdef CONFIG_TDB
		if (dir_info->tdb)
			tdb_close(dir_info->tdb);
		if (dir_info->tdb_fn) {
			if (unlink(dir_info->tdb_fn) < 0)
				com_err("e2fsck_free_dir_info", errno,
					_("while freeing dir_info tdb file"));
			free(dir_info->tdb_fn);
		}
#endif
		if (dir_info->array)
			ext2fs_free_mem(&dir_info->array);
		dir_info->array = 0;
		dir_info->size = 0;
		dir_info->count = 0;
		ext2fs_free_mem(db);
		dir_info = 0;
	}
}

/*
 * Free the dir_info structure when it isn't needed any more.
 */
void e2fsck_free_dir_info(e2fsck_t ctx)
{
	if (ctx->dir_info) {
#ifdef CONFIG_TDB
		if (ctx->dir_info->tdb)
			tdb_close(ctx->dir_info->tdb);
		if (ctx->dir_info->tdb_fn) {
			if (unlink(ctx->dir_info->tdb_fn) < 0)
				com_err("e2fsck_free_dir_info", errno,
					_("while freeing dir_info tdb file"));
			free(ctx->dir_info->tdb_fn);
		}
#endif
		if (ctx->dir_info->array)
			ext2fs_free_mem(&ctx->dir_info->array);
		ctx->dir_info->array = 0;
		ctx->dir_info->size = 0;
		ctx->dir_info->count = 0;
		ext2fs_free_mem(&ctx->dir_info);
		ctx->dir_info = 0;
	}
}

/*
 * Return the count of number of directories in the dir_info structure
 */
int e2fsck_get_num_dirinfo(e2fsck_t ctx)
{
	return ctx->dir_info ? ctx->dir_info->count : 0;
}

struct dir_info_iter *e2fsck_dir_info_iter_begin(e2fsck_t ctx)
{
	struct dir_info_iter *iter;

	iter = e2fsck_allocate_memory(ctx, sizeof(struct dir_info_iter),
				      "dir_info iterator");

#ifdef CONFIG_TDB
	if (ctx->dir_info->tdb)
		iter->tdb_iter = tdb_firstkey(ctx->dir_info->tdb);
#endif

	return iter;
}

void e2fsck_dir_info_iter_end(e2fsck_t ctx EXT2FS_ATTR((unused)),
			      struct dir_info_iter *iter)
{
#ifdef CONFIG_TDB
	free(iter->tdb_iter.dptr);
#endif
	ext2fs_free_mem(&iter);
}

/*
 * A simple interator function
 */
struct dir_info *e2fsck_dir_info_iter(e2fsck_t ctx, struct dir_info_iter *iter)
{
	if (!ctx->dir_info || !iter)
		return 0;

#ifdef CONFIG_TDB
	if (ctx->dir_info->tdb) {
		static struct dir_info ret_dir_info;
		struct dir_info_ent *buf;
		TDB_DATA data, key;

		if (iter->tdb_iter.dptr == 0)
			return 0;
		key = iter->tdb_iter;
		data = tdb_fetch(ctx->dir_info->tdb, key);
		if (!data.dptr) {
			printf("iter fetch failed: %s\n",
			       tdb_errorstr(ctx->dir_info->tdb));
			return 0;
		}
		buf = (struct dir_info_ent *) data.dptr;
		ret_dir_info.ino = *((ext2_ino_t *) iter->tdb_iter.dptr);
		ret_dir_info.dotdot = buf->dotdot;
		ret_dir_info.parent = buf->parent;
		iter->tdb_iter = tdb_nextkey(ctx->dir_info->tdb, key);
		free(key.dptr);
		free(data.dptr);
		return &ret_dir_info;
	}
#endif

	if (iter->i >= ctx->dir_info->count)
		return 0;

#ifdef DIRINFO_DEBUG
	printf("iter(%d, %d, %d)...", ctx->dir_info->array[iter->i].ino,
	       ctx->dir_info->array[iter->i].dotdot,
	       ctx->dir_info->array[iter->i].parent);
#endif
	ctx->dir_info->last_lookup = ctx->dir_info->array + iter->i++;
	return(ctx->dir_info->last_lookup);
}

/* old e2fsck_set_dir_info_set_parent
 * This function only sets the parent pointer, and requires that
 * dirinfo structure has already been created.
 */
int e2fsck_dir_info_set_parent_directly(e2fsck_t ctx, ext2_ino_t ino,
			       ext2_ino_t parent, struct dir_info_db* dir_info)
{

#ifdef _PIPELINE_PARALLELISM
	if(dir_info){
		//fsck_lock(&dir_info->dir_info_lock);
		pthread_rwlock_rdlock(&dir_info->rwlock);
	}
#endif

	struct dir_info *p;

	if(dir_info){
		p = e2fsck_get_dir_info_directly(ctx, ino, dir_info);
	} else{
		p = e2fsck_get_dir_info(ctx, ino);
	}

	if (!p)
		return 1;
	p->parent = parent;

	if(dir_info){
		e2fsck_put_dir_info_directly(ctx, p, dir_info);
	} else{
		e2fsck_put_dir_info(ctx, p);
	}

#ifdef _PIPELINE_PARALLELISM
	if(dir_info){
		//fsck_unlock(&dir_info->dir_info_lock);
		pthread_rwlock_unlock(&dir_info->rwlock);
	}
#endif

	return 0;
}

int e2fsck_dir_info_set_parent(e2fsck_t ctx, ext2_ino_t ino,
			       ext2_ino_t parent){
		return e2fsck_dir_info_set_parent_directly(ctx, ino, parent, NULL);
}

/* old e2fsck_dir_info_set_dotdot
 * This function only sets the dot dot pointer, and requires that
 * dirinfo structure has already been created.
 */
int e2fsck_dir_info_set_dotdot_directly(e2fsck_t ctx, ext2_ino_t ino,
			       ext2_ino_t dotdot, struct dir_info_db* dir_info)
{
#ifdef _PIPELINE_PARALLELISM
	if(dir_info){
		//fsck_lock(&dir_info->dir_info_lock);
		pthread_rwlock_rdlock(&dir_info->rwlock);
	}
#endif

	struct dir_info *p;

	if(dir_info){
		p = e2fsck_get_dir_info_directly(ctx, ino, dir_info);
	} else{
		p = e2fsck_get_dir_info(ctx, ino);
	}

	if (!p)
		return 1;
	p->dotdot = dotdot;

	if(dir_info){
		e2fsck_put_dir_info_directly(ctx, p, dir_info);
	} else{
		e2fsck_put_dir_info(ctx, p);
	}

#ifdef _PIPELINE_PARALLELISM
	if(dir_info){
		//fsck_unlock(&dir_info->dir_info_lock);
		pthread_rwlock_unlock(&dir_info->rwlock);
	}
#endif

	return 0;
}

int e2fsck_dir_info_set_dotdot(e2fsck_t ctx, ext2_ino_t ino,
			       ext2_ino_t dotdot){
		return e2fsck_dir_info_set_dotdot_directly(ctx, ino, dotdot, NULL);
}

/*
 * This function only sets the parent pointer, and requires that
 * dirinfo structure has already been created.
 */
int e2fsck_dir_info_get_parent(e2fsck_t ctx, ext2_ino_t ino,
			       ext2_ino_t *parent)
{
	struct dir_info *p;

// --->>> need to lock

	p = e2fsck_get_dir_info(ctx, ino);
	if (!p){	
		return 1;
	}
	*parent = p->parent;


	return 0;
}

/*
 * This function only sets the dot dot pointer, and requires that
 * dirinfo structure has already been created.
 */
int e2fsck_dir_info_get_dotdot(e2fsck_t ctx, ext2_ino_t ino,
			       ext2_ino_t *dotdot)
{
	struct dir_info *p;

// -----> need to lock

	p = e2fsck_get_dir_info(ctx, ino);
	if (!p)
		return 1;
	*dotdot = p->dotdot;
	return 0;
}

struct dir_info* get_dir_at(struct dir_info_db* db, int index) {
	return db->array + index;
}

int get_dir_count(struct dir_info_db* db) {
	return db->count;
}

void e2fsck_merge_dir_info(e2fsck_t ctx, struct dir_info_db** dir_info1, struct dir_info_db** dir_info2){
	struct dir_info_db *main = *dir_info1;
	struct dir_info_db *merge = *dir_info2;
	struct dir_info	*ptr1, *ptr2, *old_array;
	errcode_t		retval;
	int total_count;
	unsigned long old_mem_size;
	int x, old_size;

	total_count = main->count + merge->count;

	if(total_count >= main->size){
		old_size = main->size;
		old_mem_size = main->size * sizeof(struct dir_info);
		main->size = total_count;
		old_array = main->array;
		retval = ext2fs_resize_mem(old_mem_size, main->size *
						 sizeof(struct dir_info),
						 &main->array);
		if (retval) {
			fprintf(stderr, "Couldn't reallocate dir_info "
				"structure to %d entries\n",
				main->size);
			fatal_error(ctx, 0);
			main->size = old_size;
			return;
		}
		if (old_array != main->array)
			main->last_lookup = NULL;
	}

	// copy over to main
	ptr1 = main->array;
	ptr2 = merge->array;

	if(merge->count > 0){
		memcpy((void*) &ptr1[main->count], (void*) ptr2, merge->count * sizeof(struct dir_info));
	}
	
	//for(x = 0; x < merge->count; x++){
	//	memcpy((void*) &ptr1[main->count + x], (void*) &ptr2[x], sizeof(struct dir_info));
	//}
	
	//update count
	main->count = total_count;

}

void print_dir_count( struct dir_info_db* ptr){
	printf("DIR INFO COUNT: %d\n",ptr->count);
}
