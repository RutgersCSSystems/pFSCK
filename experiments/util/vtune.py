from command import *


vtune_path = "~/intel/vtune_amplifier/bin64/amplxe-cl"


class Analysis:

    # Conctructor
    def __init__(self, programpath):
        self.program = programpath

        # default analysis values
        self.collect_type = "hotspots"

        # other default values
        self.debugging = True


    def debug(self, message):
        if self.debugging:
            print "VTUNE: Analyzer - " + message

    # Sets program to be analyzed
    def set_program(self, program_name):
        self.program = program_name
        return self

    # Short hand for set_program
    def analyze(self, program_name):
        return self.set_program(program_name)

    # Sets collection type
    def set_collect(self, type):
        self.collect_type = type
        return self

    # Short-hand for set_collect
    def collect(self, type):
        return self.set_collect(type)

    # Sets directory to put analysis
    def set_result_dir(self, dir):
        self.result_dir = dir
        return self

    # Short-hand for set_result_dir
    def output(self, dir):
        return self.set_result_dir(dir)

    # Run Analysis
    def run(self):
        # Check if there is a program to analyze
        if self.program == "":
            self.debug("RUN Error - no program specified to run")
            return False

        # Check if an output directory is set
        if not hasattr(self, "result_dir"):
            self.debug("RUN Error - no output directory specified")
            return False


        # set up base command
        command = "sudo " + vtune_path

        # append collection type
        if hasattr(self, "collect_type"):
            command += " -collect " + self.collect_type

        # append result director
        if hasattr(self, "result_dir"):
            command += " -result-dir " + self.result_dir

        # finally append program
        command += " -- " + self.program

        # Execute analysis
        self.debug("Running analysis - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("Error : Analysis \n" + result.output)
            return False

        return True

    # Shorthand for easy running
    def to(self, dir):
        self.output(dir)
        return self.run()

class Report:

    def __init__(self, resultdir):
        self.result_dir = resultdir

        # default report values
        self.report_type = "top-down"
        self.output_type = "csv"
        self.columntype = "values"
        self.columns = ["\"CPU Time:Total\"", "\"CPU Time:Self\""]

        # other default values
        self.debugging = True

    def debug(self, message):
        if self.debugging:
            print "VTUNE: Reporter - " + message

    # Set column value type
    def set_columntype(self, type):
        self.columntype = type
        return self

    def set_columns(self, cols):
        self.columns = []
        for col in cols:
            columns.append("\"" + col + "\"")
        return self

    def set_report(self, type):
        self.report_type = type
        return self

    def set_result(self, resultdir):
        self.result_dir = resultdir
        return self

    def report(self, resultdir):
        return self.set_result(resultdir)

    def set_output(self, filename):
        self.output_name = filename
        return self

    def set_output_type(self, type):
        self.output_type = type
        return self

    def run(self):

        # Check if an result directory is set
        if not hasattr(self, "result_dir"):
            self.debug("RUN Error - no result to from specified")
            return False

        # Check if an output name is set
        if not hasattr(self, "output_name"):
            self.debug("RUN Error - no output filename specified")
            return False

        # set up base command
        command = "sudo " + vtune_path

        # append report type
        if hasattr(self, "report"):
            command += " -report " + self.report_type

        # append result directory to read from
        if hasattr(self, "result_dir"):
            command += " -r " + self.result_dir

        # add specified columns
        if hasattr(self, "columns"):
            command += " --column="
            for x in range(len(self.columns)):
                if x > 0:
                    command += ","
                command += self.columns[x]

        # specify column types
        if hasattr(self, "columntype"):
            command += " -show-as=" + self.columntype


        # set output file
        if hasattr(self, "output_type"):
            command += " -report-output " + self.output_name + " -format " + self.output_type
            if self.output_type == "csv":
                command += " -csv-delimiter tab"


        # Execute analysis
        self.debug("Exporting Report - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("Error : Reporting \n" + result.output)
            return False

        self.debug("Sucessfully saved report to " + self.output_name + "." + self.output_type)
        return True

    def to(self, filename):
        return self.set_output(filename).run()



class Vtune:

    @staticmethod
    def analyze(program):
        return Analysis(program)

    @staticmethod
    def report(result):
        return Report(result)


'''
NOTES ON VTUNE


// Set Variables
$ source <install dir>/amplxe-vars.sh


// Run hotspot analysis
sudo ~/intel/vtune_amplifier/bin64/amplxe-cl -collect hotspots -- ../e2fsprogs-v1.44.4/build/e2fsck/e2fsck -fFnttv /dev/loop24

// report output

// report csv with time
sudo ~/intel/vtune_amplifier/bin64/amplxe-cl -report  top-down -r r004hs --column="CPU Time:Total","CPU Time:Self" -show-as=values -report-output vtune4.csv -format csv -csv-delimiter tab

// report csv with percent
sudo ~/intel/vtune_amplifier/bin64/amplxe-cl -report  top-down -r r004hs --column="CPU Time:Total","CPU Time:Self" -show-as=values -report-output vtune4.csv -format csv -csv-delimiter tab

// show as time
-show-as=values

// Columns
--column="CPU Time: Total","CPU Time: Self"

//-discard-raw-data -(to discard after use)

// Extract data
amplxe-cl -report  top-down -r r000hs -format csv -csv-delimiter tab -report-output vtune.csv


// References
1. https://scc.ustc.edu.cn/zlsc/tc4600/intel/2017.0.098/vtune_amplifier_xe/help/GUID-05FE31FB-1D3A-4A83-AF67-586AF8F9341C.html

2. https://software.intel.com/en-us/vtune-amplifier-help-report-output

3. https://software.intel.com/en-us/vtune-amplifier-help-filtering-and-grouping-reports

'''
