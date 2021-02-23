import subprocess
import os, datetime
import re
from subprocess import Popen, PIPE
from command import run

# Path of Makefile
e2fsck_path = os.environ['FSCKEXEPATH']
print e2fsck_path
parallel=os.environ['PARALLEL']

class MakeFSCK:

    def getpath():
        return e2fsck_path;


    def withflags(self, flag):
        self.flags = flag
        return self

    def debug(self, message):
        print "MakeFSCK [" + e2fsck_path + "] : " + message;

    def compile(self):
        # Generate command
        os.chdir(e2fsck_path)

        command = "make clean"
        run(command)

        command = "make" + " " + str(self.flags) + " " + "-j" + str(parallel)
        # Run e2fsck
        self.debug("Compiling e2fsck with flags " + command)
        result = run(command)

        # Check for errors running e2fsck
        if result.status != 0:
            self.debug("Error running e2fsck")
            print result.output
            return False
        else:
            self.debug("Successfully compiled e2fsck")

        return True
