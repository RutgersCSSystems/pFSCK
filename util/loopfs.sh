#!/bin/bash

#Get image as argument
IMG_NAME=$1

# Find open loop device
LOOP_DEV=`sudo losetup -f`

# Create loop device
sudo losetup -o 0 $LOOP_DEV $IMG_NAME

# Print Loop Device name
echo $LOOP_DEV


# delete using `sudo losetup -d $LOOP_DEV`
