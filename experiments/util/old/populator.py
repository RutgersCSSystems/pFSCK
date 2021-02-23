from command import run

'''
Create a Directory with full pathname
'''
def create_directory(dirname):

    # Create directory
    command = "sudo mkdir " + dirname
    result = run(command)

    # Check return status
    if result.status != 0:
        print "error creating directory: " + dirname
        return False

    return True


'''
Create a File with full filename
'''
def create_file(filename, bytes):

    # Create file (dd bs=1024 count=1 if=/dev/zero of=bigfile)
    command = "sudo dd bs=" + str(bytes) + " count=1 if=/dev/zero of=" + filename
    result = run(command)

    # Check return status
    if result.status != 0:
        print "error creating file " + filename
        return False

    return True

'''
File Class
'''
class File:
    def __init__(self, file_name, file_size):
        self.name = file_name
        self.size = file_size

'''
Directory Class
'''
class Directory:

    def __init__(self, dir_name):
        self.name = dir_name
        self.directories = []
        self.files = []
        self.dir_counter = 0
        self.file_counter = 0

    def create_directory_full(self, dir_name):
        # Dir path
        full_dir_name = self.name + "/" + dir_name

        # Try and create directory
        if create_directory(full_dir_name):
            pass
        else:
            print "Error trying to create directory " + full_dir_name
            return None

        # Update directory structure
        new_dir = Directory(full_dir_name)
        self.directories.append(new_dir)
        return new_dir

    def create_directory(self):

        # Create directory name with counter
        dir_name = "dir-" + str(self.dir_counter)

        # Increment counter
        self.dir_counter += 1

        # Call full directory creator function
        return self.create_directory_full(dir_name)


    def create_directories(self, num):
        for i in range(num):
            self.create_directory()


    def create_file_full(self, name, size):

        full_file_name = self.name + "/" + name

        # Try and create file
        if create_file(full_file_name, size):
            pass
        else:
            print "Error trying to create file " + full_file_name
            return None

        # Update file structure
        new_file = File(full_file_name, size)
        self.files.append(new_file)

        # Return file structure
        return new_file

    def create_file(self, size):

        # Generate file name with counter
        file_name = "file-" + str(self.file_counter)

        # Increment file name counter
        self.file_counter += 1

        # Call full directory creator function
        return self.create_file_full(file_name, size)

    def create_files(self, num, size):
        for i in range(num):
            self.create_file(size)
