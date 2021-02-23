

import os, sys, getopt
import util.graphs
import shutil
from util.parser import *
from util.image_manager import *
from util.makefsck import *
from util.e2fsck import *
from util.extract_results import *
from util.graphs import *
import util.command as command


# Checks input
if len(sys.argv) < 2:
    print("Incorrect Usage.")
    exit()

# Default Test Configurations
keep_images = True
test_input  = ""

# Parse arguments
try:
    opts, args = getopt.getopt(sys.argv[1:], "i:kr", ["help"])
except getopt.GetoptError as err:
    print(str(err))
    sys.exit(2)
for opt, arg in opts:
    if opt in ("-h", "--help"):
        sys.exit()
    elif opt == "-i":
        print("Using test file " + arg)
        test_input = arg
    elif opt == "-k":
        print("Keeping images after generation")
        keep_images = True
    elif opt == "-r":
        print("Removing images after generation")
        keep_images = False
    else:
        print("Argument {} not found".format(o))
        exit()

# Check if test input is valid
if test_input == "":
    print("No Test input")
    exit()
elif not os.path.isfile(test_input):
    print("Test file not found")
    exit()
elif not test_input.endswith(".xml"):
    print("Passed in file is not XML")
    exit()


# Checks for Required Environment Variables
variables_set = True
env_vars = ["RESULTPATH", "IMAGEPATH"]
print("Checking for necessary environment variables:")
for env_var in env_vars:
    if env_var in os.environ:
        print("  Found: " + env_var + "=" + os.environ[env_var])
    else:
        variables_set = False
        print("  Error: " + env_var + " environment variable not defined")
if not variables_set:
    print("  Did you run the setvars.sh script?")
    _exit()

# Check if results folder is there
if not os.path.isdir(os.environ["RESULTPATH"]):
    print("Results folder " + os.environ["RESULTPATH"] + " not found")
    print("Creating " +  os.environ["RESULTPATH"])
    os.mkdir(os.environ["RESULTPATH"])
    print(os.environ["RESULTPATH"] + " folder was created")

# Check if images directory is present:
if not os.path.isdir(os.environ["IMAGEPATH"]):
    print("Images Folder " + os.environ["IMAGEPATH"] + " not found")
    print("Creating " + os.environ["IMAGEPATH"])
    os.mkdir(os.environ["IMAGEPATH"])
    print(os.environ["IMAGEPATH"] + " folder was created")


# parse xml
print("Parsing " + test_input)
test = parse_XML(test_input)

# Create image manager
image_manager = ImageManager.get_instance(os.environ["IMAGEPATH"])




print("Running Test: " + test["name"])


def debug(string):
    print("EXPERIMENT [" + test["name"] + "] : " + string)

test_folder = os.path.join(os.environ["RESULTPATH"], test["name"])

# Remove result folder if it already exists
if os.path.isdir(test_folder):
    shutil.rmtree(test_folder)

# Create new result folder
os.mkdir(test_folder)

previous_build = None
previous_image = None

# Run the tests
for run in test["runs"]:

    #if previous_image != None and run["image"] != previous_image and not keep_images:
     #   debug("Removing image " + previous_image)
      #  image_manager.delete_image(previous_image)

    debug("Running " + run["name"] + " with image " + run["image"])

    run_folder = os.path.join(test_folder, run["name"])
    os.mkdir(run_folder)

    # Get Image
    image = image_manager.get_image(run["image"])
    if image == None:
        debug("(ERROR) Couldn't get image " + run["image"] + ", skipping run")
        continue
    previous_image = run["image"]

    # Compile E2FSCK
    if "buildflags" in run:
        build_flags = run["buildflags"]
    else:
        build_flags = ""

    if previous_build != build_flags:
        debug("Building e2fsck with the following flags " + run["buildflags"])
        if not MakeFSCK().withflags(run["buildflags"]).compile():
            debug("(ERROR) Couldn't Compile, skipping test")
            continue
        previous_build = build_flags
    else:
        debug("Already compiled with proper build flags from previous run")

    # check if there are threads
    if "threads" in run:
        thread_count = run["threads"]
        debug("Going to run with " + thread_count + " threads")
        run["flags"] += " -q " + run["threads"]

    # Run e2fsck on image
    if not image.loop():
        debug("Failed to loop device, skipping test..")
        continue

    result_path = os.path.join(run_folder, run["name"] + ".txt")
    e2fsck = E2fsck.check(image.get_loop_point())

    # Two warm up runs
    #for x in range(2):
    #    e2fsck.withflags(run["flags"]).run()

    e2fsck_good = False;
    # Record third final run
    while e2fsck_good == False:
        cmd = "sudo sync"
        print("Running " + cmd)
        command.run(cmd)
        cmd = "sudo echo 3 > /proc/sys/vm/drop_caches"
        print("Running " + cmd)
        command.run(cmd)
        e2fsck_good = e2fsck.withflags(run["flags"]).to(result_path)

    # capture bandwidth
    e2fsck = E2fsck.check(image.get_loop_point())
    e2fsck_good = False;
    while e2fsck_good == False:
        cmd = "sudo sync"
        print("Running " + cmd)
        command.run(cmd)
        cmd = "sudo echo 3 > /proc/sys/vm/drop_caches"
        print("Running " + cmd)
        command.run(cmd)
 
        bandwidth_output = os.path.join(run_folder, run["name"] + "-bandwidth.txt")
       
        bashCommand = "sudo iotop -oqqqkt -d 1" #&>" #+ bandwidth_output 
        #bash_split = bashCommand.split()
        #bash_split.append(bandwidth_output)
        debug("Running command: " + bashCommand)
        #print(bash_split)
        with open(bandwidth_output, "w") as bfile:
            process = subprocess.Popen(bashCommand.split(), stdout=bfile, stderr=bfile)
            e2fsck_good = e2fsck.withflags(run["flags"]).run()
            bfile.flush()
            process.kill()
            bashCommand = "sudo pkill iotop"
            print("Running " + bashCommand)
            command.run(bashCommand)

    if not image.unloop():
        debug("(WARNING) Failed to unloop device")

    result = extract_result(result_path)

    csv_path = os.path.join(run_folder,run["name"] + ".csv")

    with open(csv_path, 'w') as csv_file:
        columns = ["Name", "Key", "Pass 1", "Pass 1e", "Pass 2", "Pass 3", \
                   "Pass 4", "Pass 5", "Total"]

        csv_file.write( ",".join(columns) + "\n")

        values = [run["name"], run["key"], result["pass_1"], result["pass_1e"],\
                  result["pass_2"], result["pass_3"], result["pass_4"],  \
                  result["pass_3"], result["total"] ]

        csv_file.write(",".join(values))

#if not keep_images and previous_image != None:
 #   image_manager.delete_image(previous_image)


#########################
# Aggregate Run Results #
#########################

# Get data from all runs
rows = []
for run in test["runs"]:
    run_folder = os.path.join(test_folder, run["name"])
    csv_path = os.path.join(run_folder, run["name"] + ".csv")
    if not os.path.isfile(csv_path):
        debug("(ERROR) Could not find result " + csv_path + ". Skipping.")
        continue
    with open(csv_path, "r") as csv_file:
        lines = csv_file.readlines()
        for line in lines[1:]:
            rows.append(line)
            debug("extracted row \"" + line + "\"")

# Aggregate Results
aggregate_csv_path = os.path.join(test_folder, test["name"] + ".csv")
aggregate_csv_file = open(aggregate_csv_path, "w")

# Write Header columns
columns = ["Name", "Key", "Pass 1", "Pass 1e", "Pass 2", "Pass 3", \
           "Pass 4", "Pass 5", "Total"]
aggregate_csv_file.write( ",".join(columns) + "\n")

# Write all collected rows
aggregate_csv_file.write("\n".join(rows))
aggregate_csv_file.close()


# Create Graph
for graph in test["graphs"]:
    graph_path = os.path.join(test_folder, graph["title"] + ".pdf")
    debug("Exporting Graph to " + graph_path)
    VerticleStackedBarChart(aggregate_csv_path, graph["title"], graph["xlabel"],\
                            graph["ylabel"], \
                            graph["fields"], \
                            graph_path)
