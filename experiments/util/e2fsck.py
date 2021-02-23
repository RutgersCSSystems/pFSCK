import subprocess
import os, datetime
import re
from subprocess import Popen, PIPE
from command import *

e2fsck_path = os.environ['FSCKEXE']

class E2fsck:

    @staticmethod
    def getpath():
        return e2fsck_path;

    @staticmethod
    def check(device):
        return E2fsck(device)

    def __init__(self, device):
        self.device = device
        self.flags = "-fFnttv"
        self.debugging = True

    def debug(self, message):
        if self.debugging:
            print "E2FSCK: " + message

    def withflags(self, flag):
        self.flags = flag
        return self

    def run(self):

        # Generate command
        command = "sudo /usr/bin/time -v " + e2fsck_path + " " + self.flags + " " + self.device

        # Run e2fsck
        self.debug("Running running e2fsck on " + self.device + " : " + command)
        result = run(command)

        # Check for errors running e2fsck
        if result.status != 0:
            self.debug("Error running e2fsck on " + self.device)
            #print result.output
            return False
        else:
            self.debug("Successfully ran e2fsck on " + self.device)

        # Write output if output file specified
        if hasattr(self, "output"):
            with open(self.output, 'w+') as file:
                file.write(result.output)
            self.debug("Wrote output to " + self.output)

        return True

    def to(self, filename):
        self.output = filename
        return self.run()
