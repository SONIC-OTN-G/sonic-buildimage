/**
 * Copyright (c) 2023 Alibaba Group Holding Limited
 *
 *    Licensed under the Apache License, Version 2.0 (the "License"); you may
 *    not use this file except in compliance with the License. You may obtain
 *    a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 *    THIS CODE IS PROVIDED ON AN *AS IS* BASIS, WITHOUT WARRANTIES OR
 *    CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 *    LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
 *    FOR A PARTICULAR PURPOSE, MERCHANTABILITY OR NON-INFRINGEMENT.
 *
 *    See the Apache Version 2.0 License for specific language governing
 *    permissions and limitations under the License.
 *
 */

#include <getopt.h>
#include <iostream>
#include <string>
#include "configsync.h"
#include "ocm_configsync.h"
#include "otdr_configsync.h"
#include <thread>
#include <chrono>

using namespace std;
using namespace swss;

ConfigSync *g_apsportSync;
ConfigSync *g_apsSync;
ConfigSync *g_assignmentSync;
ConfigSync *g_attenuatorSync;
ConfigSync *g_ethernetSync;
ConfigSync *g_interfaceSync;
ConfigSync *g_lldpSync;
ConfigSync *g_logicalchannelSync;
ConfigSync *g_oaSync;
ConfigSync *g_ochSync;
ConfigSync *g_oscSync;
ConfigSync *g_otnSync;
ConfigSync *g_physicalchannelSync;
ConfigSync *g_portSync;
ConfigSync *g_transceiverSync;
ConfigSync *g_ocmSync;
ConfigSync *g_otdrSync;

void usage()
{
    cout << "Usage: configsyncd" << endl;
    cout << "       this program will exit if configDB does not contain that info" << endl;
}

int main(int argc, char **argv)
{
    Logger::linkToDbNative("configsyncd");
    SWSS_LOG_ENTER();

    int opt;

    while ((opt = getopt(argc, argv, "v:h")) != -1)
    {
        switch (opt)
        {
        case 'h':
            usage();
            return 1;
        default: /* '?' */
            usage();
            return EXIT_FAILURE;
        }
    }

    g_apsportSync = new ConfigSync("apsportsync", CFG_OT_APSPORT_TABLE_NAME, APP_OT_APSPORT_TABLE_NAME);
    g_apsSync = new ConfigSync("apssync", CFG_OT_APS_TABLE_NAME, APP_OT_APS_TABLE_NAME);
    g_assignmentSync = new ConfigSync("assignmentsync", CFG_OT_ASSIGNMENT_TABLE_NAME, APP_OT_ASSIGNMENT_TABLE_NAME);
    g_attenuatorSync = new ConfigSync("attenuatorsync", CFG_OT_ATTENUATOR_TABLE_NAME, APP_OT_ATTENUATOR_TABLE_NAME);
    g_ethernetSync = new ConfigSync("ethernetsync", CFG_OT_ETHERNET_TABLE_NAME, APP_OT_ETHERNET_TABLE_NAME);
    g_interfaceSync = new ConfigSync("interfacesync", CFG_OT_INTERFACE_TABLE_NAME, APP_OT_INTERFACE_TABLE_NAME);
    g_lldpSync = new ConfigSync("lldpsync", CFG_OT_LLDP_TABLE_NAME, APP_OT_LLDP_TABLE_NAME);
    g_logicalchannelSync = new ConfigSync("logicalchannelsync", CFG_OT_LOGICALCHANNEL_TABLE_NAME, APP_OT_LOGICALCHANNEL_TABLE_NAME);
    g_oaSync = new ConfigSync("oasync", CFG_OT_OA_TABLE_NAME, APP_OT_OA_TABLE_NAME);
    g_ochSync = new ConfigSync("ochsync", CFG_OT_OCH_TABLE_NAME, APP_OT_OCH_TABLE_NAME);
    g_oscSync = new ConfigSync("oscsync", CFG_OT_OSC_TABLE_NAME, APP_OT_OSC_TABLE_NAME);
    g_otnSync = new ConfigSync("otnsync", CFG_OT_OTN_TABLE_NAME, APP_OT_OTN_TABLE_NAME);
    g_physicalchannelSync = new ConfigSync("physicalchannelsync", CFG_OT_PHYSICALCHANNEL_TABLE_NAME, APP_OT_PHYSICALCHANNEL_TABLE_NAME);
    g_portSync = new ConfigSync("portsync", CFG_OT_PORT_TABLE_NAME, APP_OT_PORT_TABLE_NAME);
    g_transceiverSync = new ConfigSync("transceiversync", CFG_OT_TRANSCEIVER_TABLE_NAME, APP_OT_TRANSCEIVER_TABLE_NAME);
    g_ocmSync = new OcmConfigSync();
    g_otdrSync = new OtdrConfigSync();

    vector<ConfigSync *> syncList
    {
        g_apsportSync,
        g_apsSync,
        g_assignmentSync,
        g_attenuatorSync,
        g_ethernetSync,
        g_interfaceSync,
        g_lldpSync,
        g_logicalchannelSync,
        g_oaSync,
        g_ochSync,
        g_oscSync,
        g_otnSync,
        g_physicalchannelSync,
        g_portSync,
        g_transceiverSync,
        g_ocmSync,
        g_otdrSync
    };

    try
    {

        Select s;

        vector<ConfigSync *> objects;

        for (ConfigSync *sync : syncList)
        {
            if (sync->handleConfigFromConfigDB())
            {
                objects.push_back(sync);
                s.addSelectables(sync->getSelectables());
            }
        }

        if (objects.size() == 0)
        {
            goto exit;
        }

        DBConnector appl_db("APPL_DB", 0);
        Table linecard_table(&appl_db, APP_OT_LINECARD_TABLE_NAME);
        ProducerStateTable linecard_pst(&appl_db, APP_OT_LINECARD_TABLE_NAME);
        vector<FieldValueTuple> attrs;
        attrs.push_back(FieldValueTuple("object-count", to_string(objects.size())));

        vector<string> linecard_key;
        linecard_table.getKeys(linecard_key);
        while (linecard_key.size() == 0)
        {   
            linecard_table.getKeys(linecard_key);
            SWSS_LOG_NOTICE("Waiting for Linecard...");
            this_thread::sleep_for(chrono::milliseconds(1000));
        }

        for (const auto& value : linecard_key)
        {
            SWSS_LOG_NOTICE("Set object-count, key=%s, count=%d", value.c_str(), static_cast<int>(objects.size()));
            linecard_pst.set(value, attrs);
        }
 
        while (true)
        {
            Selectable *temps;

            int ret = s.select(&temps, DEFAULT_SELECT_TIMEOUT);

            if (ret == Select::ERROR)
            {
                cerr << "Error had been returned in select" << endl;
                continue;
            }
            else if (ret == Select::TIMEOUT)
            {
                continue;
            }
            else if (ret != Select::OBJECT)
            {
                SWSS_LOG_ERROR("Unknown return value from Select %d", ret);
                continue;
            }

            for (ConfigSync *o : objects)
            {
                o->doTask(temps);
            }
        }
    }
    catch (const std::exception& e)
    {
        cerr << "Exception \"" << e.what() << "\" was thrown in daemon" << endl;
    }
    catch (...)
    {   
        cerr << "Exception was thrown in daemon" << endl;
    }

exit:

    for (ConfigSync *sync : syncList)
    {
        delete sync;
    }

    return 0;
}

