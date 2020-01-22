PROJECT_ROOT = $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

OBJS = rtcmfilter.o

#ifeq ($(BUILD_MODE),debug)
#	CFLAGS += -g
#else ifeq ($(BUILD_MODE),run)
#	CFLAGS += -O2
#else
#	$(error Build mode $(BUILD_MODE) not supported by this Makefile)
#endif

CFLAGS += -g

all:
	$(MAKE) -C $(PROJECT_ROOT)/src all

test:
	$(MAKE) -C $(PROJECT_ROOT)/src test

clean:
	$(MAKE) -C $(PROJECT_ROOT)/src clean
