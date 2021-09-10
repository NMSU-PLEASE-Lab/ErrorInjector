# ErrorInjector

Bit error injector: injects bit error into running application's memory.
This library will inject random bit errors into a
running application. 

Copyright (C) 2021 Jonathan Cook. All rights reserved.

## Usage

- set environment variable SDC_DELAY to the integer # of seconds
  of wall-time to wait before injecting the error (default: 3 seconds)
- set environment variable SDC_MPIONLY if you want to only inject an
  MPI process (this checks for env vars and allows you to avoid injecting 
  into mpirun/mpiexec)
- set environment variable SDC_MPIRANK to an integer if you want an error 
  only injected to that particular MPI process; otherwise, all processes get
  injected.
- set environment variable SDC_OUTFILE to the name of the desired output
  file (defaults to sdcout-PID.log, where PID is the process PID for this run)
  Use one '%d' in the output filename if you want the PID embedded in the name.
- set environment variable SDC_MEMTYPE to one of the following:
  - 'all' -- any memory in the application space (and its DSO libraries) may be 
              injected with an error
  - 'data' -- any data memory in the application space may be injected with an error
  - 'code' -- any program memory in the application space may be injected with an error
  - 'appdata' -- any data memory in the application space, but not data memory in the
                  DSO libraries, may be injected with an error (excl heap and stack, bad!)
  - 'heap' -- any memory in the application's heap may be injected with an error
  - 'stack' -- any memory in the application's stack may be injected with an error
  - default is 'data'
- load this library into app space using LD_PRELOAD
- run the application

## TODO

- use env var for bit range for errors (i.e., limit to exponent?)
- decide on 32 or 64 bit base (effects address alignment and bit range)
- need some sort of process selection capability (extern random # set into env var?)
- allow double bit errors (ecc will correct single bit, but single models other problems too)
- allow OR in of different memory types


