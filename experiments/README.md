# Experiments
The main experimental setup consists of two components:
1. The Experiment Runner 
2. The Image Manager

## Experiment Runner (experiment.py) 
The experiment runner runs an test on an image and generates graphs. The
experiment runner requires you have your "RESULTPATH" environment variable
specified as it will place experimental results in there. By default, the
experiment runner will toss images unless the '-k' flag is passed in.

### Usage
The usage of the experiment runner is as follows:

```
Argument | Input      |  Description
------------------------------------
  -i     | <xml file> | input test configuration file
  -k     |    -       | keep image files that were created
```

### Test Formats
The experiment runner runs a test described in a test XML file.
It has the basic format of:

```
<data>
  <name>Pass 2 Test</name>       <-- The name of the Test
  <folder>Pass 2 Test</folder>   <-- The name of the folder to put result
  <run>
    ......                       <-- Run information
  </run>
  <graph>
   ....                          <-- Graph information
  </graph>
</data>
```

The test must consists of:
  1. runs
  2. graphs


### Output
In the end, your results folder should look like the following:

```
 /results/
   experiment-1/
       result-1/
           output-1.txt
           result-1.csv
       result-2/
           output-2.txt
           result-2.csv
       experiment-1.csv
       graph-1.png
```


## Image Manager
The image manager makes handling images easier by locating images on the fly
or dynamically generating them if need be. This requires the "IMAGEPATH"
environment variable to be specified to locate where images should be located
  
First the image manager will check the images folder to see if the image already
exists. If it doesn't exist it will try and parse the name to see if it can be
automatically generated.

An image name a set of attribute pairs in the form attr-value.
for example, if I want a 4gb image i would set the image to size-4gb.img
the first attribute specifies how the image should be populated.

For example fillfile_size-4gb.img will generate a 4gb image and fill the image
completely with files, using as much inodes as possible.

Another example would be dir-100_size-4gb.img. This would generate an 4gb image
and fill it with 100 directories.

Images are filled with a function that generates files and folders in some
order.

A dictionary of prefix to function mapping can be found in image_gen.py
Populator functions can be found in filler.py

In order to add a new filesystem configuration, you would have to:
1. Add a generic populator function to filler.py
2. Update prefix to function mapping in image_gen.py

## Tests
Within the tests folder are a couple of sample tests

### Basic Sensitivity Tests 
1. *_file-count-test.xml* - runs fsck for different file counts
2. *_dir-count-test.xml* - runs fsck for different directory counts
3. *_fs-size-test.xml* - runs fsck for different file system sizes

### pFSCK Tests 

#### Data parallelism tests 
The following tests the scalability of pFSCK's data parallelism for 
different thread counts 
1. *data-files.xml* - tests on file-intensive file system 
1. *data-dirs.xml* - tests on directory-intensive file system

#### Pipeline parallelism tests 
The following tests the scalability of pFSCK's pipeline parallelism for 
different pipeline thread assignments
1. *pipeline-files.xml* - tests on file-intensive file system 
1. *pipeline-dirs.xml* - tests on directory-intensive file system

#### Scheduler tests 
The following tests the scalability of pFSCK's dynamic thread scheduler for 
different core budget counts
1. *scheduler-files.xml* - tests on file-intensive file system 
1. *scheduler-dirs.xml* - tests on directory-intensive file system

