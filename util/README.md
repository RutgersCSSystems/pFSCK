# Utilities

This folder contains various utility scripts 

#### clear_cache.sh
This script can be run to clear all caches. This is useful to ensure all cached filesystem blocks are cleared to ensure consistent performance measurements. 

#### create_image.sh
This script can be used to create arbitrary file system images to test against. 

Usage:
```
./create_image.sh <files/directories> <ext4/xfs> <img name> <size in GB> <optional: total number of inodes> 
```
You can specify the following arguments:
1. **<files/directories>** - choose whether to make it a file intensive or directory intensive file system
2. **<ext4/xfs>** - choose whether or not to format file system as EXT4 or XFS. *Note: You will need to make sure you have the XFS kernel module installed to format as XFS*
3. **<img_name>** - name of the file system image
4. **<size_in_GB>** - size of file system in GB

You can additionally add an extra argument to specify the total number of inodes to ensure consistent inode usage across file system sizes. 

#### create_corrupt.sh 

This script takes a file system image and corrupts a certain amount of bytes using e2fsprogs' e2fuzz tool.
It will place the corrupted image whereever you specified `$IMAGEPATH`

Usage: 
```
/create_corrupt.sh <base image> <# bytes to corrupt>
```

#### loopfs.sh
This script can be used to mount a file system image to a loop device so it can be ran with e2fsck/pFSCK.

Usage:
```
./loopfs.sh <image path>
```
