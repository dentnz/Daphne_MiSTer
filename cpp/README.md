# Daphne_MiSTer CPP Files

Contains the following files and folders:

### Daphne

A copy of the daphne source code.

### Main_MiSTer

Set of files required for simulation. Some files will be for eventual inclusion in the 
main Main_MiSTer binary for Daphne to work. In particular, the sub ``support`` folder 
contains the code that is to be executed during Main_MiSTer's 'polling' loop. This
includes handling of video and audio file IO.

#### Mock files

Some Main_MiSTer files were hacked in via mocks to assist with development of daphne.
These include:

- file_io.cpp - system level file IO wrappers
- fpga_io.cpp - Core fpga messaging used by spi.cpp
- spi.cpp - SPI bridge messaging -
- user_io.cpp - wrappers around mounted files - removed/commented out unnecessary code