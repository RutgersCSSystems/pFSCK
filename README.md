# pFSCK 

pFSCK, based on the e2fsck file system checking tool, uses parallel programming approaches and 
user level scheduling to accelerate file system checking for EXT file systems
on modern storage. This project is based on the e2fsprogs-v1.44.4 release.


## Project structure

### e2fsprogs-v1.44.4
This folder is a modified version of e2fsprogs which pFSCK is built upon.

See `e2fsprogs-v1.44.4/README.md` for more details on pFSCK, features, and how to 
compile

### scripts
This folder holds scripts pertaining to setting up the environment 

See `scripts/README.md` for more details.

### experiments 
This folder has various experiments and library to automatically generate 
file system images and run e2fsck/pFSCK against them. 

See `experiments/README.md` for more details.

### util 
The util folder has several scripts to help do misc tasks, such as generating your own file system image as well as other things

See `util/README.md` for more details. 

## Questions

If there are any questions or issues, let me know at djd240@cs.rutgers.edu
