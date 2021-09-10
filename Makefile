#
# @file
# @author Jonathan Cook
# @brief Makefile for error injector library
#
# Copyright (C) 2021 Jonathan Cook
#

CFLAGS = -Wall -fPIC -g

libsdc.so: injector.o readsmaps.o
	$(CC) $(CFLAGS) -shared -o $@ $^ -lrt -ldl

testsdc: injector.c readsmaps.o
	$(CC) $(CFLAGS) -o $@ -DTESTING $^ -lrt -ldl

dox: 
	doxygen doxygen.cfg
	
tar: veryclean
	(cd ..; tar cvf sdctester.tar sdc/*.[ch] sdc/Make*)
	mv ../sdctester.tar .

clean:
	rm -rf *.o *~

veryclean: clean
	rm -rf html latex doc
