INCLUDES = -I $(top_srcdir)/lib -I $(top_srcdir) -I $(top_srcdir)/cfgmgr

bin_PROGRAMS = configsyncd

if DEBUG
DBGFLAGS = -ggdb -DDEBUG
else
DBGFLAGS = -g
endif

configsyncd_SOURCES = configsyncd.cpp \
                      configsync.cpp \
                      ocm_configsync.cpp \
                      otdr_configsync.cpp

configsyncd_CFLAGS = $(DBGFLAGS) $(AM_CFLAGS) $(CFLAGS_COMMON)
configsyncd_CPPFLAGS = $(DBGFLAGS) $(AM_CFLAGS) $(CFLAGS_COMMON)
configsyncd_LDADD = -lswsscommon -lpthread

