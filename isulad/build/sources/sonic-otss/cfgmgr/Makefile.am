INCLUDES = -I$(top_srcdir)/lib -I $(top_srcdir) -I $(top_srcdir)/orchagent -I $(top_srcdir)/warmrestart -I $(top_srcdir)/orchagent/flex_counter
CFLAGS_OTAI = -I /usr/include/otai

bin_PROGRAMS = linecardmgrd

cfgmgrdir = $(datadir)/swss

if DEBUG
DBGFLAGS = -ggdb -DDEBUG
else
DBGFLAGS = -g
endif

linecardmgrd_SOURCES = linecardmgrd.cpp linecardmgr.cpp $(top_srcdir)/orchagent/orch.cpp $(top_srcdir)/orchagent/request_parser.cpp shellcmd.h
linecardmgrd_CFLAGS = $(DBGFLAGS) $(AM_CFLAGS) $(CFLAGS_COMMON) $(CFLAGS_OTAI)
linecardmgrd_CPPFLAGS = $(DBGFLAGS) $(AM_CFLAGS) $(CFLAGS_COMMON) $(CFLAGS_OTAI)
linecardmgrd_LDADD = -lswsscommon

