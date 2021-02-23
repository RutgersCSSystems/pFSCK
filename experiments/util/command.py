# Wrapper for running commands #

import commands

class command_result:
    def __init__(self, status, output):
        self.status = status
        self.output = output

def run(command):
    result = commands.getstatusoutput(command)
    return command_result(result[0],result[1])
