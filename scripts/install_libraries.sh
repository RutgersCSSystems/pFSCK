#!/bin/bash

if [ -v EXPSCRIPTS ]
then

  # Make sure to update
  sudo apt-get update

  # Install gemu
  sudo apt-get --yes install qemu

  # Install python
  sudo apt-get --yes install python

  # Install Pip
  sudo apt-get --yes install python-pip

  # Install necessary python packages
  pip install -r $EXPSCRIPTS/requirements.txt

else

  echo EXPSCRIPTS environment variable not set. Did you run setvars?

fi
