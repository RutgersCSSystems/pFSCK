#!/bin/bash
set -x

COMPILE_FSMARK() {
	cd $FSMARKPATH
	make clean
	make
}

COMPILE_FSCK() {
	cd $FSCKSRC
	mkdir $FSCKSRC/build
	cd $FSCKSRC/build

	if [ ! -f "Makefile" ]
	then
		../configure
	fi
	make clean
	make -j4
	COMPILE_FSMARK
}

CREATE_RESULT() {
	mkdir $RESULTPATH
}

# Compile FSCK and related code
COMPILE_FSCK

# Compile FS_mark tool 
COMPILE_FSMARK

# Create a result folder
CREATE_RESULT

# Add other steps before running the code here
SETUP

