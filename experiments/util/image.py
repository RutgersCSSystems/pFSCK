import os
from command import run


# This holds the following:
#
# Image Class:
#   the image class
#
# create_image():
#   create_image creates an image given a image path and a size
#
# delete_image():
#   delete_image removes an image given an image path
#

class Image(object):

    def __init__(self, image_path):
        self.path = image_path
        self.name = os.path.basename(image_path).strip(".img")
        self.mount_point = ""
        self.loop_point = ""

    def debug(self, string):
        print("IMAGE [" + self.name + "] : " + string)

    # Loops the image to the first open loop device
    def loop(self):

        # Check if mounted
        if self.mount_point != "":
            self.debug("(ERROR) Cannot loop, image already mounted")
            return False

        # Check if alredy looped
        if self.loop_point != "":
            self.debug("(WARNING) Image already looped")
            return False

        # Find an open loop device (losetup -f`)
        command = "losetup -f"
        self.debug("finding open loop device - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("(ERROR) Failed finding open loop device - \n" \
                       + result.output)
            return False

        # Get name of open loop device
        openLoopDevice = result.output

        # Set up loop device (sudo losetup -o 0 $LOOP_DEV $IMG_NAME)
        command = "sudo losetup -o 0 " + openLoopDevice + " " + self.path
        self.debug("setting up loop device - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("(ERROR) Failed setting up loop device - \n" \
                       + result.output)
            return False

        # Set loop device flag to true
        self.loop_point = openLoopDevice
        self.debug("sucesssfully looped filesystem image - " + self.loop_point)
        return True


    def unloop(self):

        # check if mounted
        if self.mount_point != "":
            self.debug("(ERROR) Cannot unloop, image is mounted")
            return False

        # Check if already unlooped
        if self.loop_point == "":
            self.debug("(WARNING) Image is already unlooped")
            return True

        # Remove loop device
        command = "sudo losetup --detach " + self.loop_point
        self.debug("deleting loop device - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("ERROR unlooping - \n" + result.output)
            return False

        # Set loop device flag to false and return
        self.loop_point = ""
        self.debug("sucesssfully unlooped image")
        return True

    def get_loop_point(self):
        return self.loop_point

    # mounts to a directory with the same name
    def mount(self):

        # Check if it is looped
        if self.loop_point != "":
            self.debug("(ERROR) Image is looped, can not mount")
            return False

        # Check if it is mounted already:
        if self.mount_point != "":
            self.debug("(WARNING) IMage is already mounted")
            return True

        # if directory exists, remove it:
        if os.path.isdir(self.name):
            # Run unmount command (sudo umount $MNT_NAME)
            command = "sudo umount " + self.name
            self.debug("unmounting filesystem - " + command)
            result = run(command)

            self.debug("DIRECTORY EXISTS, deleting it")
            command = "sudo rm -rf " + self.name
            self.debug("deleting existing mount directory - " + command)
            result = run(command)

            # Check for any errors
            if result.status != 0:
                self.debug("(ERROR) failed to delete mount directory - \n"\
                           + result.output)
                return False


        # Try and create a directory to mount to
        command = "mkdir " + self.name
        self.debug("creating directory to mount to - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("(ERROR) failed creating mount directory - \n"\
                       + result.output)
            return False

        # Run mount command (sudo mount -o loop $IMG_NAME $MNT_NAME)
        command = "sudo mount -o loop " + self.path + " " + self.name
        self.debug("mounting filesystem - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("(ERROR) Failed mounting image - \n" + result.output)
            return False

        # Set mounted flag to true and return
        self.mount_point = self.name
        self.debug("sucesssfully mounted filesystem image to " + self.name)
        return True

    # unmounts the directory
    def unmount(self):

        # Check if it is looped
        if self.loop_point != "":
            self.debug("(ERROR) Image is looped, can not unmount")
            return False

        # Check if it is unmounted already:
        if self.mount_point == "":
            self.debug("(WARNING) Image is already unmounted")
            return True

        # Call clear_cache.sh
        command = "sudo $FSCKSCRIPT/clear_cache.sh"
        self.debug("Running clear_cache.sh script - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("(ERROR) Failed to run clear_cache.sh - \n" + result.output)
            #return False

        # Run unmount command (sudo umount $MNT_NAME)
        command = "sudo umount " + self.mount_point
        self.debug("unmounting filesystem - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("(ERROR) Failed to unmount - \n" + result.output)
            return False

        # Try and remove mount point
        command = "sudo rm -rf " + self.mount_point
        self.debug("deleting mount directory - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("(ERROR) Failed to delete mount directory - \n" \
                       + result.output)
            return False

        # Set mount flag and return
        self.mount_point = ""
        self.debug("sucesssfully unmounted filesystem image")
        return True

    def get_mount_point(self):
        return self.mount_point


def create_image(image_path, size):

    # Create an image with qemu
    command = "qemu-img create " + image_path + " " + str(size) + "g"
    print("creating image - " + command)
    result = run(command)

    # Check for any errors
    if result.status != 0:
        print("(ERROR) Failed to create image - \n" \
                   + result.output)
        return None

    # Format the newly created image
    command = "mkfs.ext4 -F " + image_path
    print("formatting image - " + command)
    result = run(command)

    # Check for any errors
    if result.status != 0:
        print("(ERROR) Failed to format image - \n" \
                   + result.output)
        return None

    # Return true upon success
    return Image(image_path)

def destroy_image(image_path):

    # Delete image
    command = "sudo rm " + image_path
    print("deleting image - " + command)
    result = run(command)

    # Check for any errors
    if result.status != 0:
        print("(ERROR) Failed to delete image - \n" \
                   + result.output)
        return False

    # Return true upon success
    return True
