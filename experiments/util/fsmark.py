import subprocess
import os, datetime
import re
from subprocess import Popen, PIPE
from command import *

fs_mark_directory = os.environ['FSMARKPATH']

class Fs_mark:

    def __init__(self, directory_name):
        self.directory = directory_name

    def root(self, directory_name):
        self.directory = directory_name
        return self

    def subdirs(self, num_dir):
        self.numsubdirs = num_dir
        return self

    def numfiles(self, x):
        self.numfiles = x
        return self

    def filesize(self, x):
        self.filesize = x
        return self

    def filesperdir(self, x):
        self.numfilesperdir = x
        return self

    def keep(self):
        self.dokeep = True
        return self

    def fill(self):
        self.dofill = True
        return self

    def run(self):

        if os.path.isfile(os.path.join(fs_mark_directory, "fs_mark")):
            print "FS MARK found and compiled"
        else:
            print "FS MARK EXECUTABLE NOT FOUND"
            return False

        command = "sudo " + fs_mark_directory + "/fs_mark -S 0"

        if hasattr(self, "directory"):
            command += " -d " + self.directory
        else:
            print "FS-MARK: ERROR - no directory set"
            return False

        if hasattr(self, "numfiles"):
            command += " -n " + str(self.numfiles)
        else:
            print "FS-MARK: ERROR - no file number set"
            return False

        if hasattr(self, "filesize"):
            command += " -s " + str(self.filesize)
        else:
            print "FS-MARK: ERROR - no file size set"
            return False

        if hasattr(self, "numsubdirs"):
            command += " -D " + str(self.numsubdirs)

        if hasattr(self, "numfilesperdir"):
            command += " -N " + str(self.numfilesperdir)

        if hasattr(self, "dofill"):
            command += " -F"
        elif hasattr(self, "dokeep"):
            command += " -k"

        #command += " -t 8"

        print "FS-MARK: Running - " + command
        result = run(command)

        print "FS-MARK: Cleaning up logs"
        run("sudo rm fs_log*")

        if result.status != 0:
            if hasattr(self, "dofill"):
                print "FS-MARK: filling stopped"
                print result.output
                return True

            print "FS-MARK: Error"
            print result.output
            return False

        print "FS-MARK: Success"
        return True
