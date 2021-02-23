import os
import util.filler
from util.image import *
from util.image_gen import *


class ImageManager(object):

    @staticmethod
    def get_instance(images_path):

        # Check if valid path
        if os.path.isdir(images_path):
            print("ImageManager: Generating ImageManager for " + images_path)
            return ImageManager(images_path)
        else:
            print("ImageManager: Couldn't get ImageManager for " + images_path)
            return None

    def __init__(self, images_path):
        self.path = images_path

    def debug(self, string):
        print("ImageManager [" + self.path + "] : " + string)

    def check_image(self, image_name):
        self.debug("check_image : Checking for " + image_name)
        if os.path.isfile(os.path.join(self.path,image_name)):
            self.debug("check_image" + image_name + "found")
            return True
        self.debug("check_image" + image_name + " not found")
        return False

    def get_image(self, image_name):
        image_path = os.path.join(self.path, image_name)
        if not os.path.isfile(image_path):
            self.debug("Image file " + image_path  + " not found")
            self.debug("Going to generate image")
            return generate_image(self.path, image_name)
        self.debug("Getting image " + image_name)
        return Image(image_path)

    def delete_image(self, image_name):

        # derive image path
        image_path = os.path.join(self.path, image_name)

        # Delete image
        command = "sudo rm " + image_path
        self.debug("Deleting image - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("(ERROR) Failed to delete image - \n" \
                       + result.output)
            return False

        # Return true upon success
        self.debug("Sucesssfully deleted image " + image_name)
        return True
