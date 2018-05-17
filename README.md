# Pulp debug bridge

This is a tool which can be used to interact with a pulp target, like doing read and write or loading a binary.
It also provide an RSP server so that it can be used to interface GDB with pulp targets.

###  Getting the sources

This repository uses submodules. You need the --recursive option to fetch the submodules automatically

    $ git clone --recursive to https://github.com/pulp-platform/pulp-debug-bridge.git

### Prerequisites

In case the FTDI cables are needed, the libftdi must be installed. Here is the command on Ubuntu to install it:

    $ sudo apt-get install libftdi1-dev
    
Once the FTDI lib is installed, USB access rights must be updated.
Most of the time a rule like the following must be added under /etc/udev/rules.d/, for example in /etc/udev/rules.d/10-ftdi.rules: ::

        ATTR{idVendor}=="15ba", ATTR{idProduct}=="002b", MODE="0666", GROUP="dialout"

The user should also need to be in the *dialout* group.


### Installation

To build this tool, execute this command from the root directory:

    $ make all
    
All what is needed to use the tool is then inside the directory `install`. You can define the following paths in order to use it:

    $ export PATH=<root dir>/install/bin:$PATH
    $ export PYTHONPATH=<root dir>/install/python:$PYTHONPATH
    $ export LD_LIBRARY_PATH=<root dir>/install/lib:$LD_LIBRARY_PATH


### Usage

You can execute the following to display the help:

    $ plpbridge --help
    
You need at least to configure the cable and the target with these options:

    # plpbridge --chip=pulpissimo --cable=ftdi
    
You can for example read from the target with this command:

    # plpbridge --chip=pulpissimo --cable=ftdi read --addr=0x1c000000 --size=32
    
Or write:

    # plpbridge --chip=pulpissimo --cable=ftdi write --addr=0x1c000000 --size=32 --value=0x12345678
    
A binary can also be loaded with this command:

    # plpbridge --chip=pulpissimo --cable=ftdi load --binary=<binary path>

The RSP server for the GDB connection can be started with this command:

    # plpbridge --chip=pulpissimo --cable=ftdi gdb wait --rsp-port=1234
    
### Supported cables

2 FTDI cables are supported: --cable=ftdi@olimex and --cable=ftdi@digilent.
However the bridge may need few modifications depending on the ftdi chip which is used.

It is also possible to connect the bridge to a remote server, like an RTL platform (using a DPI model): --cable=jtag-proxy.
More information for this cable will be provided soon.

### Supported targets

Only pulp and pulpissimo are supported for now.
