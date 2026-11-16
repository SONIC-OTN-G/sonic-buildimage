INCLUDES = -I $(top_srcdir)/lib -I $(top_srcdir) -I $(top_srcdir)/warmrestart -I flex_counter

CFLAGS_OTAI = -I /usr/include/otai

swssdir = $(datadir)/swss

bin_PROGRAMS = orchagent

if DEBUG
DBGFLAGS = -ggdb -DDEBUG
else
DBGFLAGS = -g -DNDEBUG
endif

orchagent_SOURCES = \
            main.cpp \
            orchdaemon.cpp \
            orch.cpp \
            portorch.cpp \
            logicalchannelorch.cpp \
            physicalchannelorch.cpp \
            transceiverorch.cpp \
            ochorch.cpp \
            otnorch.cpp \
            ethernetorch.cpp \
            lldporch.cpp \
            assignmentorch.cpp \
            interfaceorch.cpp \
            oaorch.cpp \
            oscorch.cpp \
            apsorch.cpp \
            apsportorch.cpp \
            attenuatororch.cpp \
            ocmorch.cpp \
            otdrorch.cpp \
            otaiobjectorch.cpp \
            otaihelper.cpp \
            request_parser.cpp \
            flexcounterorch.cpp \
            linecardorch.cpp \
            notifications.cpp \
            orchfsm.cpp \
            diagorch.cpp

orchagent_SOURCES += flex_counter/flex_counter_manager.cpp flex_counter/flex_counter_stat_manager.cpp
orchagent_CFLAGS = $(DBGFLAGS) $(AM_CFLAGS) $(CFLAGS_COMMON) $(CFLAGS_OTAI)
orchagent_CPPFLAGS = $(DBGFLAGS) $(AM_CFLAGS) $(CFLAGS_COMMON) $(CFLAGS_OTAI)
orchagent_LDADD = -lnl-3 -lnl-route-3 -lpthread -lotairedis -lswsscommon -lotaimeta -lotaimetadata

