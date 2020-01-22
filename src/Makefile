PROJECT_ROOT = $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

ifdef windir
CC   = gcc
OPTS = -Wall -W -DWINDOWSVERSION -g -o
LIBS = -lwsock32
else
CC   = gcc
OPTS = -Wall -W -g -I/usr/local/include -c
endif

all: rtcmfilter

rtcmfilter:	rtcmfilter.o messagehandler.o rtcm.o rtcm2.o rtcm3.o rtcm3e.o rtkcmn.o
	gcc  -o rtcmfilter rtcmfilter.o messagehandler.o rtcm.o rtcm2.o rtcm3.o rtcm3e.o rtkcmn.o -lm

rcmfilter.o: rtcmfilter.c
	$(CC) $(OPTS) rtcmfilter.c -o rtcmfilter.o
	
messagehandler.o: messagehandler.c
	$(CC) $(OPTS) messagehandler.c -o messagehandler.o

rtcm.o: rtcm.c
	$(CC) $(OPTS) rtcm.c -o rtcm.o

rtcm2.o: rtcm2.c
	$(CC) $(OPTS) rtcm2.c -o rtcm2.o

rtcm3.o: rtcm3.c
	$(CC) $(OPTS) rtcm3.c -o rtcm3.o

rtcm3e.o: rtcm3e.c
	$(CC) $(OPTS) rtcm3e.c -o rtcm3e.o

rtkcmn.o: rtkcmn.c
	$(CC) $(OPTS) rtkcmn.c -o rtkcmn.o

test: send_test_data.o
	$(CC) -g send_test_data.o -o send_test_data

nodebug: rtcmfilter.c
	$(CC) -g -c $? -O3 -DNDEBUG -o $@ $(LIBS)
	
clean:
	$(RM) -f rtcmfilter *.o core
