extern "C" {
#include "otai.h"
#include "otaistatus.h"
}

#include <fstream>
#include <iostream>
#include <unordered_map>
#include <map>
#include <memory>
#include <thread>
#include <chrono>
#include <getopt.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "timestamp.h"

#include <otairedis.h>
#include <logger.h>

#include "orchdaemon.h"
#include "otai_serialize.h"
#include "otaihelper.h"
#include <signal.h>

using namespace std;
using namespace swss;

extern otai_linecard_api_t *otai_linecard_api;

/* Global variables */
otai_object_id_t gLinecardId = OTAI_NULL_OBJECT_ID;

#define DEFAULT_BATCH_SIZE  128
int gBatchSize = DEFAULT_BATCH_SIZE;
int gSlotId = 0;
string gFlexcounterJsonFile;

void usage()
{
    cout << "usage: orchagent [-h] [-b batch_size] [-m MAC] [-i INST_ID]" << endl;
    cout << "    -h: display this message" << endl;
    cout << "    -b batch_size: set consumer table pop operation batch size (default 128)" << endl;
    cout << "    -i INST_ID: set the ASIC instance_id in multi-asic platform" << endl;
    cout << "    -c flexcounter_json_filename: flexcounter json filename" << endl;
}


int main(int argc, char **argv)
{
    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_DEBUG);

    SWSS_LOG_ENTER();

    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_NOTICE);

    swss::Logger::linkToDbNative("orchagent");

    int opt;

    while ((opt = getopt(argc, argv, "b:m:f:d:i:h:c:")) != -1)
    {
        switch (opt)
        {
        case 'b':
            gBatchSize = atoi(optarg);
            break;
        case 'i':
            gSlotId = atoi(optarg) + 1;
            break;
        case 'h':
            usage();
            exit(EXIT_SUCCESS);
        case 'c':
            if (optarg)
            {
                gFlexcounterJsonFile = optarg;
            }
            break;
        default: /* '?' */
            exit(EXIT_FAILURE);
        }
    }

    SWSS_LOG_NOTICE("--- Starting Orchestration Agent ---");

    initOtaiApi();


    /* Initialize orchestration components */
    DBConnector appl_db("APPL_DB", 0);
    DBConnector config_db("CONFIG_DB", 0);
    DBConnector state_db("STATE_DB", 0);

    auto orchDaemon = make_shared<OrchDaemon>(&appl_db, &config_db, &state_db);

    if (!orchDaemon->init())
    {
        SWSS_LOG_ERROR("Failed to initialize orchestration daemon");
        exit(EXIT_FAILURE);
    }

    orchDaemon->start();

    return 0;
}

