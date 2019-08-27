PROJECT_ROOT = $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

ifdef windir
CC   = gcc
OPTS = -Wall -W -DWINDOWSVERSION -g
LIBS = -lwsock32
else
CC   = gcc
OPTS = -Wall -W -g
endif

all: rtcmfilter

rtcmfilter:	rtcmfilter.o messagehandler.o
	$(CC) -o $@ $^
	
test: send_test_data.o
	$(CC) -g send_test_data.o -o send_test_data

nodebug: rtcmfilter.c
	$(CC) -g -c $? -O3 -DNDEBUG -o $@ $(LIBS)

%.o:	$(PROJECT_ROOT)/src/%.c
	$(CC) -g -c -o $@ $<
	
clean:
	$(RM) -f rtcmfilter rtcmfilter.o messagehandler.o send_test_data send_test_data.o core
