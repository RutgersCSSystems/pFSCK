#!/bin/bash

SCRIPT_NAME=$(basename -- "$0")
MOUNT_DIR="testmnt"

# Some nessesary tool paths
FSMARK_PATH=$EXPSCRIPTS/fs_mark-3.3/fs_mark 

# counters 
COUNTER=0

debug () {
	echo "[$SCRIPT_NAME]$TEST $1"
}

print_usage () {
	echo $SCRIPT_NAME
	echo "usage: $SCRIPT_NAME <files/directories> <ext4/xfs> <img name> <size in GB> <optional: total number of inodes> "
	exit 1
}

flush_cache () {
	debug "[flush] Flushing cache"
	sudo bash -c 'sync; echo 3 > /proc/sys/vm/drop_caches'
}

cleanup () {
	debug "[cleanup] unmounting" 
	sudo umount $MOUNT_DIR &> /dev/null #unmount if already mounted 
	debug "[cleanup] unlooping" 
	sudo losetup -D $LOOPDEV &> /dev/null #unloop if already looped
	debug "[cleanup] deleting mount directory"
	sudo rm -rf $MOUNT_DIR &> /dev/null #delete mount dir
}

check_success () {
	if [ $1 -ne 0 ]; then 
		debug "Operation Failed. Going to exit."
		cleanup
		exit 1
	fi
}

loop_image () {
	LOOPDEV=`sudo losetup -f`
	sudo losetup -o 0 $LOOPDEV $IMAGE_PATH
	check_success $?
	debug "[loop] Looped image $IMAGE_PATH to $LOOPDEV"
}

unloop_image () {
	debug "[unloop] Unlooping $LOOPDEV"
	sudo losetup -D $LOOPDEV
	check_success $?
}

mount_image () {
	debug "[mount] Mounting $IMAGE_PATH to $MOUNT_DIR"
	sudo mount -o loop $IMAGE_PATH $MOUNT_DIR
	check_success $?
}

unmount_image () {
	debug "[unmount] Umounting $IMAGE_PATH from $MOUNT_DIR"
	sudo umount $MOUNT_DIR
	check_success $?
}

examine_inodes () {
	debug "[examine] Examining inode usage"
	mount_image
	INODES_TOTAL=`df -i $MOUNT_DIR | awk '{print $2}' | sed -n 2p`
	INODES_USED=`df -i $MOUNT_DIR | awk '{print $3}' | sed -n 2p`
	INODES_FREE=`df -i $MOUNT_DIR | awk '{print $4}' | sed -n 2p`
	debug "[examine] Inode Usage: $INODES_TOTAL total, $INODES_USED used, $INODES_FREE free"
	unmount_image
}

fill () {
	debug "[filler] Going to fill file system"
	if [ $TYPE == "files" ]; then
		COUNT=`expr $COUNT + $INTERVAL_FILES`	
		PER_DIR=`expr $INTERVAL_FILES / 2`
		debug "[filler] Filling with $INTERVAL_FILES files to total $COUNT files"
		sudo mkdir $MOUNT_DIR/$ITER
		sudo $FSMARK_PATH -d $MOUNT_DIR/$ITER -D 2 -N $PER_DIR -n $INTERVAL_FILES -s 12288 -k -S 0 &> /dev/null 
	elif [ $TYPE == "directories" ]; then 
		COUNT=`expr $COUNT + $INTERVAL_DIRECTORIES`
		debug "[filler] Filling with $INTERVAL_DIRECTORIES directories to total $COUNT directories"
		sudo mkdir $MOUNT_DIR/$ITER
		sudo $FSMARK_PATH -d $MOUNT_DIR/$ITER -D $INTERVAL_DIRECTORIES -N 1 -n $INTERVAL_DIRECTORIES -s 24576 -k -S 0 &> /dev/null
	fi
}

# check files or dirs
if [ -z $1 ]; then 
	debug "Need to provide a configuration"
	print_usage
elif [ $1 == "files" ]; then
	debug "Going to fill with files"
	TYPE="files"
elif [ $1 == "directories" ]; then
	debug "Going to fill with directories"
	TYPE="directories"
fi

# check for fs type
if [ -z $2 ]; then
	debug "Need to specify filesystem"
	print_usage
elif [ $2 == 'ext4' ]; then
	debug "Going to use EXT4"
	MAKEFS="mkfs.ext4 -F"
elif [ $2 == 'xfs' ]; then 
	debug "Going to use XFS"
	MAKEFS="mkfs.xfs -d agcount=64"
fi

if [ -z $3 ]; then
	debug "Need to specify image name"
	print_usage
else
	IMAGE_PATH=$IMAGEPATH/$3
fi

if [ -z $4 ]; then 
	debug "Need to specify size in GB"
	print_usage
else
	IMAGE_SIZE=$4g
fi

#RESULT_DIR=$RESULT_DIR/count-$TYPE-test
#debug "Saving Results to $RESULT_DIR"

# check for existing image 
#sudo rm $IMAGE_PATH &> /dev/null

# check for mount directory 
rm -rf $MOUNT_DIR &> /dev/null
mkdir $MOUNT_DIR &> /dev/null

# check for results folder
rm -rf $RESULT_DIR &> /dev/null
mkdir $RESULT_DIR &> /dev/null

# generate image 
#debug "Creating File system image at $IMAGE_PATH of size $IMAGE_SIZE"
#sudo qemu-img create $IMAGE_PATH $IMAGE_SIZE &> /dev/null
#sudo $MAKEFS $IMAGE_PATH #&> /dev/null

if [ -z $5 ]; then
	debug "Automatically going to set total inodes"
	examine_inodes
else
	debug "null"
	INODES_TOTAL=$5
fi
	

# CACULATE interval
debug "Calculating Interval"

LEFT_OVER=${INODES_TOTAL:1}
BASE_TOTAL=`expr $INODES_TOTAL - $LEFT_OVER`

#if [ -z $5 ]; then
#	debug "Automatically going to set total inodes"
#else
#	BASE_TOTAL=$5
#fi

INTERVAL_FILES=`expr $BASE_TOTAL / 5`
INTERVAL_DIRECTORIES=`expr $INTERVAL_FILES / 2`


if [ $TYPE = files ]; then 
	debug "Filling file system $INTERVAL_FILES files at time."
	debug "Benchmark will run 5 times, totalling $BASE_TOTAL files"
elif [ $TYPE == "directories" ]; then
	debug "Filling file system $INTERVAL_DIRECTORIES directories at a time"
	debug "Benchmark will run 5 times, totalling $BASE_TOTAL files"
fi


for iter in {1..5}; do

	ITER=$iter
	mount_image
	fill
	flush_cache
	unmount_image
	examine_inodes

done

cleanup
