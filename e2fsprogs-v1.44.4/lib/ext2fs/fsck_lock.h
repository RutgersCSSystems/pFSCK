#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sched.h>

#ifdef _SPIN_XCHG_BACKOFF
#include "spinlock-xchg-backoff.h"
#elif _SPIN_XCHG
#include "spinlock-xchg.h"
#elif _SPIN_XCHG_HLE
#include "spinlock-xchg-hle.h"
#else
#define _ENABLE_PTHREAD
#endif

//#define _ENABLE_PTHREAD
#define _DEBUG

struct fsck_lock {
	int locked;
	long tid_holder;
};

#ifdef _ENABLE_PTHREAD
typedef pthread_mutex_t  fsck_lock_t;
#else
typedef spinlock fsck_lock_t;
#endif

#ifndef _ENABLE_PTHREAD
//int fsck_spinlock_init(fsck_lock_t* spinlock);
//int fsck_spin_lock(fsck_lock_t* spinlock);
//int fsck_spin_unlock(fsck_lock_t *spinlock);
#endif


int fsck_init(fsck_lock_t *restrict mutex, pthread_mutexattr_t *restrict attr);
int fsck_lock(fsck_lock_t *mutex);
int fsck_unlock(fsck_lock_t *mutex);


//TODO: Needs implementation later
#if 0
#define MUTEX_FREE -100
#define fsck_t long
struct fsck_mutex {
	int mutex;
	long tid_holder;
};
extern int fsck_create(fsck_t* thid, int fn (void*), void*arg);
extern void fsck_init(struct timeval *timer) ;
extern void fsck_exit(void* exit_code);
extern void fsck_yeild();
extern long fsck_self();
extern int fsck_equal(long t1, long t2);
extern int fsck_join(long gttid, void** status);
extern void fsck_join_all() ;
#endif


