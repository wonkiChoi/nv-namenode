CURDIR=           $(shell pwd)
SRCDIR=           $(CURDIR)/src
INC=              -I. -I$(CURDIR)/include

NVRAMAPILIB=      pmcnvram


CC=               cc
AR=               ar
CFLAGS=           -Wall -Werror -g -DPLATFORM_HOST

UNAME_S = $(shell uname -s)

ifeq ($(UNAME_S),FreeBSD)
	CFLAGS += -D OS_FREE_BSD
else
	CFLAGS += -D OS_LINUX
endif

SRC=          $(addprefix $(SRCDIR)/,pmc_log.c pmc_utils.c pmc_nvram_events.c pmc_ossrv_linux.c pmc_nvram_api.c)
OBJS=         pmc_log.o pmc_utils.o pmc_nvram_events.o pmc_ossrv_linux.o pmc_nvram_api.o



%.o : $(SRCDIR)/%.* $(CURDIR)/include/*.h
		$(CC) $(CFLAGS) $(INC) -c $<
		
all :   $(SRC) $(OBJS)
		$(AR) rcs lib$(NVRAMAPILIB).a $(OBJS)
		
		
clean:
		rm -f $(OBJS)
		rm -f lib$(NVRAMAPILIB).a
