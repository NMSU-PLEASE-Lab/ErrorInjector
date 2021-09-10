#!/bin/bash
#
# @file
# @author Jonathan Cook
# @brief Example script for running an application with the error injector
#
# Copyright (C) 2021 Jonathan Cook
#

# Usage: ./runscript.sh numberofprocesses compiledMPIfile (e.x ./runscript.sh 10 hello)

SEED=$(od -vAn -N4 -tu4 < /dev/random)
RANDOM=$SEED
#print variable on a screen
NumberofProcesses=$1
CompiledFileName=$2
RandomProcessorId=$((RANDOM%NumberofProcesses))
export SDC_DELAY=1 
export SDC_MPIONLY=1
export SDC_MPIRANK=$RandomProcessorId
export SDC_OUTFILE=result%d.log
export SDC_MEMTYPE=all
export LD_PRELOAD=libsdc.so
export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH
export PATH=/home/jcd3/srg/students/amir/free-cfd-0.1.2/MPI/installedversion/bin:$PATH
export LD_LIBRARY_PATH=/home/jcd3/srg/students/amir/free-cfd-0.1.2/MPI/installedversion/lib:$LD_LIBRARY_PATH
echo SDC_DELAY: $SDC_DELAY
echo SDC_MPIONLY: $SDC_MPIONLY
echo SDC_MPIRANK: $SDC_MPIRANK
echo SDC_OUTFILE: $SDC_OUTFILE
echo SDC_MEMTYPE: $SDC_MEMTYPE
echo LD_PRELOAD: $LD_PRELOAD
echo LD_LIBRARY_PATH:$LD_LIBRARY_PATH
mpirun -np $NumberofProcesses $CompiledFileName

