#!/bin/bash

# Project Paths 
export FSCKHOME=$PWD
export EXPSCRIPTS=$FSCKHOME/experiments
export FSCKSCRIPT=$FSCKHOME/scripts

# pFSCK paths
export FSCKSRC=$FSCKHOME/e2fsprogs-v1.44.4
export FSCKEXE=$FSCKSRC/build/e2fsck/e2fsck
export FSCKEXEPATH=$FSCKSRC/build

# path to fs_mark to populate file system images 
export FSMARKPATH=$EXPSCRIPTS/fs_mark-3.3

# Custom Enviroment Variables
export IMAGEPATH=$FSCKHOME/images
export RESULTPATH=$FSCKHOME/results
export IMAGESIZE=128
export THREAD_RANGE=16


