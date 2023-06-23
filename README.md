# SAM-Audio-Starter

# Overview
This project is tailored for quick audio startup with the ADZS-SC584-EZLITE. Some components in this audio starter project are derived from Open Source (OSS) projects and have BSD, MIT, or similar commercial friendly licenses.

# To build

### Install the tools
- Install CCES as-per Analog Devices instructions
- Install Git bash shell from here: https://git-scm.com/downloads
- Instead of Git bash, one can also install MinGW/MSYS2 from
  here: http://www.msys2.org/

## Standalone Compilation (By Command Line)

### Source the environment
- Check your path first to see if it already includes the desired version of
  CCES.  In general Git bash inherits the path and MinGW/MSYS2 does not
  unless it's launched with '-use-full-path' option.

```
echo $PATH  # Check inherited path
```

- If necessary, modify the 'env.sh' shell to match your CCES installation
  directory and include the environment in your shell.

```
. ./env.sh # Note the space between the first two periods!
```

### Build the code
- Go into the build directory and type `make`

```
cd build
make -j4
```
### Binaries
- Binaries are located in the root of the _build_ folder.

## Debugging the code
- Open CCES, create a new debug configuration
- Load `build/ezkitSC584_preload_core0_v10` into core0
- Additionally, load the `SAM-Audio-Starter-ARM.exe` executable into core0
- Load the `SAM-Audio-Starter-SHARC0.dxe` executable into core1
- Load the `SAM-Audio-Starter-SHARC1.dxe` executable into core2
- Under the "Automatic Breakpoint" tab, be sure to **uncheck** the "Enable
  semihosting" checkbox located at the bottom of the tab window.
- Save and start debugging.

## Initial flashing of the code using the ADI's cldp utility

- Edit setenv.bat as necessary to point to your CCES install and run it.
  If cldp is already in your path you can omit this step.
- Set the blue rotary switch on the ez-kit to position zero and reset the
  board
- Go into the 'build' directory.
- Run *flash.bat* (or *./flash.bat* depending on your terminal console) and wait for CLPD to complete. *Note that this may take a few minutes to complete.*
- Set the blue rotary switch back to one and reset the board
