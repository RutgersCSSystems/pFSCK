from util.filler import *
from util.image import *
from math import log
import os

# Image Generator:
# The Image Generate generates images bases with certain attributes.
# Attributes are derived from the image name

# IMAGE NAMING CONVENTION:
# An image name consists of various attributes value pairs joined with
# underscores:
#
# Image attributes take the convention:
#    <attribute>-<value>
#
# Images must have at least two attributes:
#  - type-fulldir
#  - size-4gb
#  - if size is x, then looks at environment variable
# you can set


######################
# Custom Image Table #
######################

custom_images = {
    "fillfile": fill_files,   # fills to max
    "filldir" : fill_dirs,    # fills with directories
    "files-": count_files,     # fills with # of files
    "dirs-": count_dirs        # fills with # of dir
}


# Generates an image within a directory
def generate_image(directory, image_name):

    def debug(string):
        print("IMAGE_GENERATOR: " + string)

    # Simplify image name
    image_name = os.path.expandvars(image_name)

    name = image_name.strip(".img")

    fill_name = ""
    need_arg = False

    func = None


    for custom in custom_images:
        if name.startswith(custom):
            func = custom_images[custom]
            if custom.endswith("-"):
                need_arg = True
            fill_name = custom

    if func == None:
        debug("Image not found in table")
        return None

    attribute_pairs = name.split("_")

    attr_dic = {}

    for attribute_pair in attribute_pairs:
        attr = attribute_pair.split("-")
        if len(attr) > 1:
            attr_dic[attr[0]] = attr[1]

    print("Attribute dictionary: " + str(attr_dic))

    # check if size attribute is there:
    if "size" not in attr_dic:
        return None

    image_path = os.path.join(directory, image_name)
    size = attr_dic["size"].strip("gb")
    evaluated_size = str(int(eval(size)))
    image_path = image_path.replace(size, evaluated_size)
    size = evaluated_size

    debug("Setting image size to " + size + "gb")

    # Check if image already exists
    if os.path.isfile(image_path):
        debug("Image " + image_path + " already exists, returning that")
        return Image(image_path)

    image = create_image(image_path, size)

    # Check if image was created sucessfully
    if image == None:
        return None

    if not image.mount():
        return None
    if need_arg:
        func(image.get_mount_point(), attr_dic[fill_name.strip("-")])
    else:
        func(image.get_mount_point())

    if image.unmount():
        return image


    return image
