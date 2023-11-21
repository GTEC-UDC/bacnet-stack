# BACnet Stack

This is a fork of [bacnet-stack](https://github.com/bacnet-stack/bacnet-stack) with two new apps: `readpropmjson` and `readpropmbyname`. These apps are based on the `bacrpm` app and provide additional functionalities.

The `bacrpm` app included in [bacnet-stack](https://github.com/bacnet-stack/bacnet-stack) allows reading multiple BACnet properties. Its usage is as follows:

```bash
bacrpm device-instance 
    object-type object-instance property[index][,property[index]]
    [object-type ...] [--version][--help]
```

This application has two drawbacks:

* It returns the data in a proprietary format.
* It does not allow to connect to a BACnet/TCP controller by specifying its IP.

To overcome these problems, the `readpropmjson` application has been created. This application returns the data in JSON format and also allows to specify the IP address of the BACnet controller. Its usage is as follows:

```bash
readpropmjson [--dnet N] [--dadr A] [--mac A] device-instance
    object-type object-instance property[index][,property[index]]
    [object-type ...] [--version][--help]
```

This application requires the [cJSON](https://github.com/DaveGamble/cJSON) library, included in this repository as a submodule in `lib/cJSON`.

To retrieve BACnet data with these commands the following BACnet parameters must be specified:

* **Device instance**: The number that uniquely identifies the BACnet device throughout the interconnected BACnet network.
* **Object type**: The type of object. BACnet defines 18 different object types, e.g., Analog Input, Analog Output, Analog Value, etc.
* **Object instance**: The object instance number that uniquely identifies the object on the BACnet device.
* **Property[index]**: The property to be read from the object. In case the property is an array, the index of the element to read can be specified.

To simplify the reading of BACnet properties a new application `readpropmbyname` has been created. This application allows to retrieve the `present-value` property of BACnet objects by specifying only their names, i.e., it does not need the *object type* and *object instance* parameters. For this, it is necessary to pass to the application a `--csv` parameter with a CSV file with the information of the object names and types. The usage of this application is as follows:

```bash
readpropmbyname [--dnet N] [--dadr A] [--mac A] --id device-instance
    --csv object-data-file object-name [object-name ...]
    [--version][--help]
```

The CSV file for this application must contain at least the columns `name`, `type_value`, and `address`. An example of this file and a Python script for creating it from a BACnet/IP controller is provided in the [bacnet_get_objects_props](https://github.com/tombolano/bacnet_get_objects_props) repository.

This application requires the [cJSON](https://github.com/DaveGamble/cJSON), [glib-2.0](https://docs.gtk.org/glib/), and [libcsv](https://github.com/rgamble/libcsv) libraries. The cJSON and libcsv libraries are included as submodules in the `lib` directory.

To compile the code first run `git submodule update --init` and then run `make`. Optionally, the new applications can be also compiled with static linking, to do so call `make` with the parameter `STATIC=1`, in this case the package glibc-static is required.

For additional information about these applications see the included help using the `--help` option.

The original README of the [bacnet-stack](https://github.com/bacnet-stack/bacnet-stack) project is included below.

---

BACnet open source protocol stack for embedded systems, Linux, and Windows
http://bacnet.sourceforge.net/

Welcome to the wonderful world of BACnet and true device interoperability!

Continuous Integration
----------------------

This library uses automated continuous integration services
to assist in automated compilation, validation, linting, security scanning,
and unit testing to produce robust C code and BACnet functionality.

[![Actions Status](https://github.com/bacnet-stack/bacnet-stack/workflows/CMake/badge.svg)](https://github.com/bacnet-stack/bacnet-stack/actions/workflows/main.yml) GitHub Workflow: CMake build of library and demo apps on Ubuntu, Windows and MacOS

[![Actions Status](https://github.com/bacnet-stack/bacnet-stack/workflows/GCC/badge.svg)](https://github.com/bacnet-stack/bacnet-stack/actions/workflows/gcc.yml) GitHub Workflow: Ubuntu Makefile GCC build of library, BACnet/IP demo apps with and without BBMD, BACnet/IPv6, BACnet Ethernet, and BACnet MSTP demo apps, gateway, router, router-ipv6, router-mstp, ARM ports (STM, Atmel), AVR ports, and BACnet/IP demo apps compiled with MinGW32.

[![Actions Status](https://github.com/bacnet-stack/bacnet-stack/workflows/Quality/badge.svg)](https://github.com/bacnet-stack/bacnet-stack/actions/workflows/lint.yml) GitHub Workflow: scan-build (LLVM Clang Tools), cppcheck, codespell, unit tests and code coverage.

[![Actions Status](https://github.com/bacnet-stack/bacnet-stack/workflows/CodeQL/badge.svg)](https://github.com/bacnet-stack/bacnet-stack/actions/workflows/codeql-analysis.yml) GitHub Workflow CodeQL Analysis

About this Project
------------------

This BACnet library provides a BACnet application layer, network layer and
media access (MAC) layer communications services for an embedded system.

BACnet - A Data Communication Protocol for Building Automation and Control
Networks - see bacnet.org. BACnet is a standard data communication protocol for
Building Automation and Control Networks. BACnet is an open protocol, which
means anyone can contribute to the standard, and anyone may use it. The only
caveat is that the BACnet standard document itself is copyrighted by ASHRAE,
and they sell the document to help defray costs of developing and maintaining
the standard (just like IEEE or ANSI or ISO).

For software developers, the BACnet protocol is a standard way to send and
receive messages containing data that are understood by other BACnet
compliant devices. The BACnet standard defines a standard way to communicate
over various wires or radios, known as Data Link/Physical Layers: Ethernet, 
EIA-485, EIA-232, ARCNET, and LonTalk. The BACnet standard also defines a 
standard way to communicate using UDP, IP, HTTP (Web Services), and Websockets.

This BACnet protocol stack implementation is specifically designed for the
embedded BACnet appliance, using a GPL with exception license (like eCos),
which means that any changes to the core code that are distributed are shared, 
but the BACnet library can be linked to proprietary code without the proprietary 
code becoming GPL. Note that some of the source files are designed as 
skeleton or example or template files, and are not copyrighted as GPL.

The text of the GPL exception included in each source file is as follows: 

"As a special exception, if other files instantiate templates or use macros or
inline functions from this file, or you compile this file and link it with
other works to produce a work based on this file, this file does not by itself
cause the resulting work to be covered by the GNU General Public License.
However the source code for this file must still be made available in
accordance with section (3) of the GNU General Public License."

The code is written in C for portability, and includes unit tests (PC based
unit tests). Since the code is designed to be portable, it compiles with GCC as
well as other compilers, such as Clang or IAR.

The BACnet protocol is an ASHRAE/ANSI/ISO standard, so this library adheres to
that standard. BACnet has no royalties or licensing restrictions, and
registration for a BACnet vendor ID is free.

What the code does
------------------

For an overview of this library architecture and how to use it, see
https://sourceforge.net/p/bacnet/src/ci/master/tree/doc/README.developer

This stack includes unit tests that can be run using the Makefile in the
project root directory "make test".
The unit tests can also be run using individual make invocations. 
The unit tests run a PC and continue to do so with 
every commit within the Continuous Integration environment.

The BACnet stack was functionally tested using a variety of tools
as well as various controllers and workstations. It has been included
in many products that successfully completed BTL testing.

Using the Makefile in the project root directory, a dozen sample applications
are created that run under Windows or Linux. They use the BACnet/IPv4 datalink
layer for communication by default, but could be compiled to use BACnet IPv6, 
Ethernet, ARCNET, or MS/TP.

Linux/Unix/Cygwin

    $ make clean all

Windows MinGW Bash

    $ make win32

Windows Command Line

    c:\> build.bat

The BACnet stack can be compiled by a variety of compilers.  The most common
free compiler is GCC (or MinGW under Windows).  The makefiles use GCC by
default.

The library is also instrumented to use [CMake](https://cmake.org/) which can
generate a project or Makefiles for a variety of IDE or compiler. For example,
to generate a Code::Blocks project:

    $ mkdir build
    $ cd build/
    $ cmake .. -G"CodeBlocks - Unix Makefiles"
    
    c:\> mkdir build
    c:\> cd build/
    c:\> cmake .. -G"CodeBlocks - MinGW Makefiles"

The unit tests also use CMake and may be run with the command sequence:

    $ make test
    
The unit test framework uses a slightly modified ztest, and the tests are located
in the test/ folder.  The unit test builder uses CMake, and the test coverage 
uses LCOV.  The HTML results of the unit testing coverage are available starting 
in the test/build/lcoverage/index.html file.

The demo applications are all client applications that provide one main BACnet
service, except the one server application and one gateway application, 
a couple router applications, and a couple of MS/TP specific applications.
Each application will accept command line parameters, and prints the output to 
stdout or stderr.  The client applications are command line based and can 
be used in scripts or for troubleshooting.  
The demo applications make use of environment variables to 
setup the network options.  See each individual demo for the options.

There are also projects in the ports/ directory for ARM7, AVR, RTOS-32, PIC, 
and others.  Each of those projects has a demo application for specific hardware.
In the case of the ARM7 and AVR, their makefile works with GCC compilers and
there are project files for IAR Embedded Workbench and Rowley Crossworks for ARM.

Project Documentation
---------------------

The project documentation is in the doc/ directory.  Similar documents are
on the project website at <http://bacnet.sourceforge.net/>.

Project Mailing List and Help
-----------------------------

If you want to contribute to this project and have some C coding skills,
join us via https://github.com/bacnet-stack/bacnet-stack/
or via https://sourceforge.net/p/bacnet/src/ and create a
fork or branch, and eventually a pull request to have 
your code considered for inclusion.

If you find a bug in this project, please tell us about it at
https://sourceforge.net/p/bacnet/bugs/
or
https://github.com/bacnet-stack/bacnet-stack/issues

If you have a support request, you can post it at 
https://sourceforge.net/p/bacnet/support-requests/

If you have a feature request, you can post it at
https://sourceforge.net/p/bacnet/feature-requests/

If you have a problem getting this library to work for
your device, or have a BACnet question, join the developers mailing list at:
http://lists.sourceforge.net/mailman/listinfo/bacnet-developers
or post the question to the Open Discussion, Developers, or Help forums at
https://sourceforge.net/p/bacnet/discussion/

I hope that you get your BACnet Device working!

Steve Karg, Birmingham, Alabama USA
skarg@users.sourceforge.net

ASHRAE® and BACnet® are registered trademarks of the 
American Society of Heating, Refrigerating and Air-Conditioning Engineers, Inc.
180 Technology Parkway NW, Peachtree Corners, Georgia 30092 US.
