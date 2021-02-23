#==================#
# Filesystem Class #
#==================#

# Creating a new filesystem named test of size 1 GB:
#    fs = Filesystem("test", 1)
#    fs.create() # generates filesystem image
#
# Opening a current filesystem image called test to use
#   fs = Filesystem("test", 0)

from command import run # wrapper for command running

class Filesystem:


    '''
    Constructor
    '''
    def __init__(self, fs_name):
        # Set file system names
        self.name = fs_name
        self.mountname = ""
        self.img_name = ""
        self.isMounted = False
        self.isLooped = False


    '''
    Debugging function to print messages
    '''
    def debug(self, message):
        filesystem_name = self.name
        if hasattr(self, "size"):
            filesystem_name += " (" + str(self.size) + "gb)"

        print "FILESYSTEM: " + self.name + " : " + message

    '''
    Set filesystem size for output purposes or for image creation
    '''
    def setsize(self, fs_size):
        self.size = fs_size
        return self

    '''
    Set filesystem image if manually configuring
    '''
    def setimg(self, image_name):
        self.img_name = image_name
        return self

    '''
    Create Actual filesystem Image
    '''
    def create(self):
        # Check if filesystem has size to even create an image
        if not hasattr(self, "size"):
            self.debug("ERROR cannot create filesystem without a size")
            return None

        # Set filesystem image name
        self.img_name = self.name + ".img"

        # Create filesystem (qemu-img create $IMG_NAME $IMG_SIZE)
        command = "qemu-img create " + self.img_name + " " + str(self.size) + "g"
        self.debug("creating filesystem image - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.img_name = ""
            self.debug("ERROR creating filesystem image - \n" + result.output)
            return None

        # Format fileystem image to Ext (mkfs.ext4 $IMG_NAME)
        command = "mkfs.ext4 " + self.img_name
        self.debug("formatting filesystem image - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("ERROR formatting filesystem image - \n" + result.output)
            return None

        # Return true open successful
        self.debug("sucesssfully created filesystem image - " + self.img_name)
        return self


    '''
    Mount filesystem
    '''
    def mount(self):
        # Check if there is even an image to mount
        if self.img_name == "":
            self.debug("ERROR: There is no image to mount")
            return False

        # Check if it is looped
        if self.isLooped:
            self.debug("ERROR: filesystem is looped, can not mount")
            return False

        # Check if it is mounted already:
        if self.isMounted:
            self.debug("(WARNING) filesytem is already mounted")
            return True

        # Try and create a directory to mount to
        command = "mkdir " + self.name
        self.debug("creating directory to mount to - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("ERROR creating mount directory - \n" + result.output)
            return False

        # Run mount command (sudo mount -o loop $IMG_NAME $MNT_NAME)
        command = "sudo mount -o loop " + self.img_name + " " + self.name
        self.debug("mounting filesystem - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("ERROR mounting filesystem- \n" + result.output)
            return False

        # Set mounted flag to true and return
        self.mountname = self.name
        self.isMounted = True
        self.debug("sucesssfully mounted filesystem image to /" + self.name)
        return True

    '''
    Unmount filesystem
    '''
    def unmount(self):
        # Check if it is looped
        if self.isLooped:
            self.debug("ERROR: filesystem is looped, can not unmount")
            return False

        # Check if it is unmounted already:
        if not self.isMounted:
            self.debug("(WARNING) filesytem is already unmounted")
            return True

        # Run unmount command (sudo umount $MNT_NAME)
        command = "sudo umount " + self.mountname
        self.debug("unmounting filesystem - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("ERROR unmounting - \n" + result.output)
            return False

        # Try and create a directory to mount to
        command = "sudo rm -rf " + self.mountname
        self.debug("deleting mount directory - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("ERROR deleting mount directory - \n" + result.output)
            return False

        # Set mount flag and return
        self.mountname = ""
        self.isMounted = False
        self.debug("sucesssfully unmounted filesystem image")
        return True

    '''
    Mount filesystem to loop device
    '''
    def loop(self):
        # Check if there is even an image to loop
        if self.img_name == "":
            self.debug("ERROR: There is no image to loop")
            return False

        # Check if mounted
        if self.isMounted:
            self.debug("ERROR: filesystem is mounted, can not loop")
            return False

        # Check if already looped
        if self.isLooped:
            self.debug("(WARNING) filesytem is already looped")
            return True

        # find an open loop device (losetup -f`)
        command = "losetup -f"
        self.debug("finding open loop device - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("ERROR finding open loop device - \n" + result.output)
            return False

        # Get name of open loop device
        openLoopDevice = result.output

        # Set up loop device (sudo losetup -o 0 $LOOP_DEV $IMG_NAME)
        command = "sudo losetup -o 0 " + openLoopDevice + " " + self.img_name
        self.debug("setting up loop device - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("ERROR setting up loop device - \n" + result.output)
            return False

        # Set loop device flag to true
        self.mountname = openLoopDevice
        self.isLooped = True
        self.debug("sucesssfully looped filesystem image - " + self.mountname)
        return True


    '''
    Unmount filesystem from loop device
    '''
    def unloop(self):
        # check if mounted
        if self.isMounted:
            self.debug("ERROR: filesystem is mounted, cannot unloop")
            return False

        # Check if already unlooped
        if not self.isLooped:
            self.debug("(WARNING) filesytem is already unlooped")
            return True

        # Remove loop device
        command = "sudo losetup -D " + self.mountname
        self.debug("deleting loop device - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("ERROR unlooping - \n" + result.output)
            return False

        # Set loop device flag to false and return
        self.mountname = ""
        self.isLooped = False
        self.debug("sucesssfully unlooped filesystem image")
        return True


    '''
    Destroy fileystem image
    '''
    def destroy(self):
        # Check if looped
        if self.isLooped:
            self.debug("ERROR: filesystem is looped, cannot destroy")
            return False

        # Check if mounted
        if self.isMounted:
            self.debug("ERROR: filesystem is mounted, cannot destroy")
            return False

        # Remove filesystem image
        command = "sudo rm " + self.img_name
        self.debug("deleting file system image - " + command)
        result = run(command)

        # Check for any errors
        if result.status != 0:
            self.debug("ERROR deleting file system image - \n" + result.output)
            return False

        self.debug("sucesssfully destroyed filesystem")
        return True



'''
Wrapper to create a filesystem
'''
def create_filesystem(fs_name, fs_size):
    print "FS name " + fs_name
    fs = Filesystem(fs_name, fs_size)
    if not fs.create():
        return None
    return fs
