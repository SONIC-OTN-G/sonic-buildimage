INCLUDES = -I $(top_srcdir)/lib -I $(top_srcdir)

CFLAGS_OTAI = -I /usr/include/otai

swssdir = $(datadir)/swss

bin_PROGRAMS = upgrade_transceiver cmd dump_state_counter_db

if DEBUG
DBGFLAGS = -ggdb -DDEBUG
else
DBGFLAGS = -g -DNDEBUG
endif

upgrade_transceiver_SOURCES = upgrade_transceiver.cpp
upgrade_transceiver_CPPFLAGS = $(DBGFLAGS) $(AM_CPPFLAGS) $(CFLAGS_COMMON)
upgrade_transceiver_LDADD = -lhiredis -lswsscommon -lpthread

cmd_SOURCES = cmd.cpp
cmd_CPPFLAGS = $(DBGFLAGS) $(AM_CPPFLAGS) $(CFLAGS_COMMON)
cmd_LDADD = -lhiredis -lswsscommon -lpthread

dump_state_counter_db_SOURCES = dump_state_counter_db.cpp
dump_state_counter_db_CPPFLAGS = $(DBGFLAGS) $(AM_CPPFLAGS) $(CFLAGS_COMMON)
dump_state_counter_db_LDADD = -lhiredis -lswsscommon -lpthread

