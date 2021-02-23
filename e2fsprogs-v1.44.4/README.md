# pFSCK 

pFSCK, based on the e2fsck file system checking tool, uses parallel programming approaches and 
user level scheduling to accelerate file system checking for EXT file systems
on modern storage. This project is based on the e2fsprogs-v1.44.4 release.



## Table Of Contents
- [Compiling](#compiling-pfsck)
- [Usage](#usage)
  - [Additional Flags](#additional-flags)
  - [Example Usage](#example-usage)
- [Compilation Options](#compilation-options)
  - [Multiple Locking Support](#multiple-locking-support)
  - [String Localization Compute Reduction](#string-localization-compute-reduction)
  - [Intel CRC32C Acceleration](#intel-crc32c-acceleration)
  - [DBList Iterate by Stride](#dblist-iterate-by-stride)
  - [Pipeline Parallelism](#pipeline-parallelism)
  - [Block Cache Fix](#block-cache-fix)
  - [Directory Block Read Ahead](#dir-block-read-ahead)
  - [Pipeline Inode Threshold](#pipeline-inode-threshold)
  - [Pipeline Dblist Threshold](#pipeline-dblist-threshold)
  - [Cache Fix](#cache-fix)
  - [Dynamic Thread Scheduling](#dynamic-scheduler)



## Compiling pFSCK
To compile follow the instructions in e2fsprogs-v1.44.4/INSTALL

## Usage

### Additional Flags
The following are extra flags added for pfsck

| flag | Arguments                | description                      |
|:----:|:------------------------:|----------------------------------|                  
| `-x` | none                     | Prints extra time.h statistics          |
| `-o` | none                     | Prints extra output from optimizations  |
| `-g` | pass numbers             | Specifies which passes to optimize      |
| `-q` | thread number            | Specifies number of threads to use for data parallelism |  
| `-w` | thread number            | Specifies number of threads to use for pipeline parallelism (if compiled with pipeline enabled)|
| `-Z` | thread number			  | Activates dynamic scheduler to scheduler N threads |
| `-X` | none			  | Activates resource-aware scheduler to scale threads |


### Example Usage
Running pfsck with extra timing, extra output, 8 threads with pass 1 and pass 2 optimizations
```
e2fsck -fFnttx -q 8 -g 12 /dev/loop1
```

## Compilation Options
### Multiple Locking Support
By default, we use pthreads for locking. But to use different optimized locks, you must compile
the code using the following commands. Each of the locks are added as a header file. Please
fsck_lock.h to see how the locks are used

- Default pthread mutex locking.
```
make clean
make
```

- SPIN XCHANGE
```
make clean
make USER_CFLAGS='-D_SPIN_XCHG'
```

- SPIN XCHANGE BACKOFF
```
make clean
make USER_CFLAGS='-D_SPIN_XCHG_BACKOFF'
```

- SPIN XCHANGE HLE
```
make USER_CFLAGS='-D_SPIN_XCHG_HLE'
```

### String Localization Compute Reduction
Operations strings for user output is localized on the fly. Sometimes this leads to redudundant localization computations. This optimizations reduces any redundant localizations.
```
make clean
make USER_CFLAGS='-D_REDUCE_OP_STRINGS'
```

### Intel CRC32C Acceleration
Newer generation Intel processsors that support the SSE4.2 instruction set can accelarate crc32c caluculations via optimized crc instructions. To enable this feature, compile with the `-D_INTEL_CRC32C` flag:
```
make clean
make USER_CFLAGS='-D_INTEL_CRC32C'
```


### DBlist Iterate by Stride
Originally within pass 2, when parallelized, directory blocks are split among threads in continuous chunks, it could also be split by striding. To enable this, compile with the '-D_PASS2_STRIDE' flag:
```
make clean
make USER_CFLAGS='-D_PASS2_STRIDE'
```

### Pipeline Parallelism
Pipeline paralellism adds the `-w` flag to set the number of threads dedicated to pipeline processing. 
```
make clean
make USER_CFLAGS='-D_PIPELINE_PARALLELISM'
```

### Block Cache Fix
Originally the block cache would try and store blocks that were read in in the cache, however reuse_cache functionality kept fetching the same cache slot for all the blocks resulting all blocks copied to the same cache slot in order, leaving only the last block stored within the cache. To fix this, reuse_cache is called after every cache miss, to ensure all blocks are stored within different cache slots. You can enable the fix with the '-D_FIX_CACHE' flag:
```
make clean
make USER_CFLAGS='-D_FIX_CACHE'
```

### Directory Block Read Ahead
Originally, directory blocks are read 1 at a time, however, it may be benifitial to reduce the number of pread() calls and read in more than one directory block in hopes that the next directory block to be examined is adjacent to the block currently being examine. You can set the max number of director blocks to read ahead at a time by setting the '-D_READ_DIR_BLOCKS' flag. For example: 
```
make clean
make USER_CFLAGS='-D_READ_DIR_BLOCKS=8'
```
NOTE: This should be used with -D_FIX_CACHE Flag or else blocks that are read ahead will not be cached properly

### Pipeline Inode Threshold
Pass 1 keeps track of directory blocks that need to be processed. In order for directory block checking threads to process directory blocks in parallel with pass 1, pass 1 has to schedule the work to done. The rate at which this work isscheduled after a certain number of inodes have been checked known as the inode threshold. This rate can be set by setting the '-D_INODE_THRESHOLD' flag. For example:
```
make clean
make USER_CFLAGS='-D_INODE_THRESHOLD=25000'
```

### Pipeline Dblist Threshold
An alternative to the inode threshold is the dblist threshold, it'll schedule for directory blocks to be checked after a certain number of directory blocks to check are accumulated by pass 1. You can set this rate by settting the '-D_DBLIST_THRESHOLD' flag. For example:
```
make clean
make USER_CFLAGS='-D_DBLIST_THRESHOLD=5000'
```
NOTE: This is exclusive and disables the INODE_THRESHOLD is defined.

### Cache Fix
The previous implementation of the block cache incorrectly cached blocks, simply caching only the last block of a read. This has been fiexed and can be activated by setting the '-D_FIX_CACHE' flag. For example:
```
make clean
make USER_CFLAGS='-D_FIX_CACHE'
```

### Dynamic Scheduler
In order to activate dynamic scheduling use with the `-Z` flag, compile the program using the '-D_SCHEDULER' flag. For example:
```
make clean
make USER_CFLAGS='-D_SCHEDULER'
```


  
