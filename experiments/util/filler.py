from util.fsmark import *
from util.bytes import *
from util.command import *

# Filler class
# This python script contains definitions that fill a certain folder
# in a particular fashion

# All functions added at the very least, passed the root directory

# Fills a filesystem image up with all files
def fill_files(root):
    Fs_mark(root).subdirs(10000).filesperdir(100).numfiles(100).filesize(bytes(12288)).fill().run()

def fill_dirs(root):
    command = "df -i " + root
    result = run(command)
    print("INODES PER: " + result.output)
    print(result.output.split())
    inodes = result.output.split()[9]
    inode_range = len(inodes) * 10 * int(inodes[0])
    pass

def count_dirs(root, dir_count):
    command = "df -i " + root
    result = run(command)
    print("INODES Available in " + root + ": " + result.output)
    Fs_mark(root).subdirs(dir_count).filesperdir(1).numfiles(dir_count).filesize(bytes(24576)).keep().run()
    pass

def count_files(root, file_count):
    command = "df -i " + root
    result = run(command)
    print("INODES Available in " + root + ": " + result.output)
    num_dir = 10
    file_num = int(file_count)
    Fs_mark(root).subdirs(num_dir).filesperdir(int(file_num/num_dir)).numfiles(file_num).filesize(bytes(12288)).keep().run()
