import os, sys
from util.image_manager import *


def debug(string):
    print("CREATEIMAGE : " + string)

# Checks input
if len(sys.argv) != 2:
    print("Incorrect Usage.")
    exit()

# Check if images directory is present:
if not os.path.isdir(os.environ["IMAGEPATH"]):
    print("Images Folder " + os.environ["IMAGEPATH"] + " not found")
    print("Creating " + os.environ["IMAGEPATH"])
    os.mkdir(os.environ["IMAGEPATH"])
    print(os.environ["IMAGEPATH"] + " folder was created")


# Create image manager
image_manager = ImageManager.get_instance(os.environ["IMAGEPATH"])

image = image_manager.get_image(sys.argv[1])
if image == None:
    debug("(ERROR) Couldn't get image " + run["image"] + ", skipping run")
else:
    debug("Successfully got image")
