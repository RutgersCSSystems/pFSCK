# Experiments


## Installing neccesary library. 
To install the required libraries for experiments. Run the `install_libraries.sh`
script. 
```
sudo ./install_libraries.sh
```
This script installs the following libraries:
- *qemu* - required to create file system images 
- *python* - required to run python based experiment scripts 
- *pip* - required to install necessary python modules for experiments 

After installing these libraries, it then uses pip to install all necessary 
python modules. 

##  Setting the Environment Variables
From the root folder, run the setvars script to set the necessary environment 
variables needed to run experiments.
```
source scripts/setvars.sh
```

There are 4 custom environment variables that you should consider when
running experiments on your particular machine:

| Env Variable | Value Type            | Description                      |
|:----:|:------------------------:|----------------------------------|                  
| `$IMAGEPATH` | directory path                   |  specifies directory to store generated filesystem  images. It is created if directory doesn't already exists.       |
| `$RESULTPATH` | directory path                     | specifies directory to store experiment results.  It is created if directory doesn't already exists.  |
| `$IMAGESIZE`  | integer | size of filesystem images to use in experimentation (in GB)| 
| `$THREAD_RANGE`  | integer | Max number of threads to test for experiments |                 

*Note 1: For time trials on fast persistent storage, make sure the image path is
located on the fast persistent storage you are trying to run tests with*

*Note 2: You can setup a tempfs to place images on DRAM with the setup-tempfs.sh
script along with setting the $RAMDISKSIZE environment variable to the size of
the tmpfs. The setup-tempfs.sh script will mount the tmpfs to your where you set
you images directory ($IMAGEPATH)*
```
export RAMDISKSIZE=51200m && ./scripts/setup-tempfs.sh
```

##  Initial Compiling and Setup
In order to run everything from the get go run the set_env.sh script to run the initial setup. 
```
sudo scripts/setup-env.sh
```
This script does the following:
1. Creates inital pFSCK build folder, set it up, and does initiall compile
2. Compiles fs_mark within the experiments folder to be used when populating file system images
3. Creates result directory to store expeimental results

*Note: Experiments generate image files and folders. We run the scripts with sudo to ensure we have all the read/write permissions to write to the image and results folders specified.*
