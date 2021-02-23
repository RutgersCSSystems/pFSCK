#!/bin/bash

# Create a mount point

if [ -v IMAGEPATH ]
then
  
  if [ -v RAMDISKSIZE ]
  then 
    echo IMAGEPATH=$IMAGEPATH
    echo Creating imagepath folder is if doesnt exist
    mkdir $IMAGEPATH
    echo Creating tmpfs to mount to $IMAGEPATH
    echo mount -t tmpfs -o size=$RAMDISKSIZE tmpfs $IMAGEPATH
    mount -t tmpfs -o size=$RAMDISKSIZE tmpfs $IMAGEPATH
  else 
    echo RAMDISKSIZE environment variable not set. Exitting..
    exit 1
  fi

else
  echo IMAGEPATH environment variable not set. Exitting..
  exit 1
fi

# create it out of the images folder
#mkdir /mnt/ramdisk


# mount -t [TYPE] -i size=[SIZE] [FSTYPE] [MOUNTPOINT]
# 50gb * 1024mb/gb = 51200mb
#mount -t tmpfs -o size=512m tmpfs /mnt/ramdisk
