 /**
 * Copyright (c) 2023 Alibaba Group Holding Limited
 * Copyright (c) 2023 Accelink Technologies Co., Ltd.
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

#include <unordered_map>
#include "flexcounterorch.h"
#include "portorch.h"
#include "select.h"
#include "notifier.h"
#include "otai_serialize.h"
#include "linecardorch.h"
#include "orchfsm.h"
#include <fstream>
#include <iostream>
#include "json.h"

using namespace std;

extern PortOrch* gPortOrch;
extern LinecardOrch* gLinecardOrch;
extern string gFlexcounterJsonFile;

std::unordered_map<std::string, std::string> flexCounterGroupMap =
{
    {"1S_STAT_GAUGE", STAT_GAUGE_FLEX_COUNTER_GROUP},
    {"1S_STAT_COUNTER", STAT_COUNTER_COUNTER_FLEX_COUNTER_GROUP},
    {"1S_STAT_STATUS", STAT_STATUS_COUNTER_FLEX_COUNTER_GROUP},
};

std::unordered_set<std::string> linecard_counter_ids_status;
std::unordered_set<std::string> linecard_counter_ids_gauge;
std::unordered_set<std::string> linecard_counter_ids_counter;

std::unordered_set<std::string> port_counter_ids_status;
std::unordered_set<std::string> port_counter_ids_gauge;
std::unordered_set<std::string> inport_counter_ids_gauge;
std::unordered_set<std::string> outport_counter_ids_gauge;

std::unordered_set<std::string> transceiver_counter_ids_status;
std::unordered_set<std::string> transceiver_counter_ids_gauge;
std::unordered_set<std::string> transceiver_counter_ids_counter;

std::unordered_set<std::string> logicalch_counter_ids_status;
std::unordered_set<std::string> logicalch_counter_ids_counter;

std::unordered_set<std::string> otn_counter_ids_status;
std::unordered_set<std::string> otn_counter_ids_gauge;
std::unordered_set<std::string> otn_counter_ids_counter;

std::unordered_set<std::string> ethernet_counter_ids_status;
std::unordered_set<std::string> ethernet_counter_ids_counter;

std::unordered_set<std::string> physicalch_counter_ids_status;
std::unordered_set<std::string> physicalch_counter_ids_gauge;
std::unordered_set<std::string> physicalch_counter_ids_counter;

std::unordered_set<std::string> opticalch_counter_ids_status;
std::unordered_set<std::string> opticalch_counter_ids_gauge;
std::unordered_set<std::string> opticalch_counter_ids_counter;

std::unordered_set<std::string> clientport_counter_ids_status;

std::unordered_set<std::string> lldp_counter_ids_status;

std::unordered_set<std::string> assignment_counter_ids_status;

std::unordered_set<std::string> interface_counter_ids_status;
std::unordered_set<std::string> interface_counter_ids_counter;

std::unordered_set<std::string> oa_counter_ids_status;
std::unordered_set<std::string> oa_counter_ids_gauge;

std::unordered_set<std::string> osc_counter_ids_status;
std::unordered_set<std::string> osc_counter_ids_gauge;

std::unordered_set<std::string> aps_counter_ids_status;

std::unordered_set<std::string> apsport_counter_ids_status;
std::unordered_set<std::string> apsport_counter_ids_gauge;

std::unordered_set<std::string> attenuator_counter_ids_status;
std::unordered_set<std::string> attenuator_counter_ids_gauge;

std::unordered_set<std::string> ocm_counter_ids_status;

std::unordered_set<std::string> otdr_counter_ids_status;

std::unordered_map<std::string, std::unordered_set<std::string>&> g_flexCounterIds =
{
    {"LINECARD_COUNTER_ID_LIST_STATUS",     linecard_counter_ids_status},
    {"LINECARD_COUNTER_ID_LIST_GAUGE",      linecard_counter_ids_gauge},
    {"LINECARD_COUNTER_ID_LIST_COUNTER",    linecard_counter_ids_counter},

    {"PORT_COUNTER_ID_LIST_STATUS",         port_counter_ids_status},
    {"PORT_COUNTER_ID_LIST_GAUGE",          port_counter_ids_gauge},
    {"INPORT_COUNTER_ID_LIST_GAUGE",        inport_counter_ids_gauge},
    {"OUTPORT_COUNTER_ID_LIST_GAUGE",       outport_counter_ids_gauge},

    {"TRANSCEIVER_COUNTER_ID_LIST_STATUS",  transceiver_counter_ids_status},
    {"TRANSCEIVER_COUNTER_ID_LIST_GAUGE",   transceiver_counter_ids_gauge},
    {"TRANSCEIVER_COUNTER_ID_LIST_COUNTER", transceiver_counter_ids_counter},

    {"LOGICALCH_COUNTER_ID_LIST_STATUS",    logicalch_counter_ids_status},
    {"LOGICALCH_COUNTER_ID_LIST_COUNTER",   logicalch_counter_ids_counter},

    {"OTN_COUNTER_ID_LIST_STATUS",          otn_counter_ids_status},
    {"OTN_COUNTER_ID_LIST_GAUGE",           otn_counter_ids_gauge},
    {"OTN_COUNTER_ID_LIST_COUNTER",         otn_counter_ids_counter},

    {"ETHERNET_COUNTER_ID_LIST_STATUS",     ethernet_counter_ids_status},
    {"ETHERNET_COUNTER_ID_LIST_COUNTER",    ethernet_counter_ids_counter},

    {"PHYSICALCH_COUNTER_ID_LIST_STATUS",   physicalch_counter_ids_status},
    {"PHYSICALCH_COUNTER_ID_LIST_GAUGE",    physicalch_counter_ids_gauge},
    {"PHYSICALCH_COUNTER_ID_LIST_COUNTER",  physicalch_counter_ids_counter},

    {"OPTICALCH_COUNTER_ID_LIST_STATUS",    opticalch_counter_ids_status},
    {"OPTICALCH_COUNTER_ID_LIST_GAUGE",     opticalch_counter_ids_gauge},
    {"OPTICALCH_COUNTER_ID_LIST_COUNTER",   opticalch_counter_ids_counter},

    {"LLDP_COUNTER_ID_LIST_STATUS",         lldp_counter_ids_status},

    {"ASSIGNMENT_COUNTER_ID_LIST_STATUS",   assignment_counter_ids_status},

    {"INTERFACE_COUNTER_ID_LIST_STATUS",    interface_counter_ids_status},
    {"INTERFACE_COUNTER_ID_LIST_COUNTER",   interface_counter_ids_counter},

    {"OA_COUNTER_ID_LIST_STATUS",           oa_counter_ids_status},
    {"OA_COUNTER_ID_LIST_GAUGE",            oa_counter_ids_gauge},

    {"OSC_COUNTER_ID_LIST_STATUS",          osc_counter_ids_status},
    {"OSC_COUNTER_ID_LIST_GAUGE",           osc_counter_ids_gauge},

    {"APS_COUNTER_ID_LIST_STATUS",          aps_counter_ids_status},

    {"APSPORT_COUNTER_ID_LIST_STATUS",      apsport_counter_ids_status},
    {"APSPORT_COUNTER_ID_LIST_GAUGE",       apsport_counter_ids_gauge},

    {"ATTENUATOR_COUNTER_ID_LIST_STATUS",   attenuator_counter_ids_status},
    {"ATTENUATOR_COUNTER_ID_LIST_GAUGE",    attenuator_counter_ids_gauge},

    {"OCM_COUNTER_ID_LIST_STATUS",          ocm_counter_ids_status},

    {"OTDR_COUNTER_ID_LIST_STATUS",         otdr_counter_ids_status},

};

shared_ptr<vector<KeyOpFieldsValuesTuple>> load_json(string file)
{
    try
    {
        ifstream json(file);
        auto db_items_ptr = make_shared<vector<KeyOpFieldsValuesTuple>>();

        if (!JSon::loadJsonFromFile(json, *db_items_ptr))
        {
            db_items_ptr.reset();
            return nullptr;
        }

        return db_items_ptr;
    }
    catch (...)
    {
        SWSS_LOG_WARN("Loading file %s failed", file.c_str());
        return nullptr;
    }
}

FlexCounterOrch::FlexCounterOrch(DBConnector* db, vector<string>& tableNames) :
    Orch(db, tableNames),
    m_flexCounterDb(new DBConnector("FLEX_COUNTER_DB", 0)),
    m_flexCounterGroupTable(new ProducerTable(m_flexCounterDb.get(), FLEX_COUNTER_GROUP_TABLE)),
    m_gaugeManager(STAT_GAUGE_FLEX_COUNTER_GROUP, StatsMode::STATS_MODE_GAUGE, DEFAULT_POLL_TIME, true),
    m_counterManager(STAT_COUNTER_COUNTER_FLEX_COUNTER_GROUP, StatsMode::STATS_MODE_COUNTER, DEFAULT_POLL_TIME, true),
    m_statusManager(STAT_STATUS_COUNTER_FLEX_COUNTER_GROUP, StatsMode::STATS_MODE_STATUS, DEFAULT_POLL_TIME, true)
{
    SWSS_LOG_ENTER();
    m_flexCounterInit = false;
    m_flexCounterGroupInit = false;
}

FlexCounterOrch::~FlexCounterOrch(void)
{
    SWSS_LOG_ENTER();
}

void FlexCounterOrch::doCounterGroupTask(Consumer& consumer)
{
    SWSS_LOG_ENTER();

    if (OrchFSM::getState() <= ORCH_STATE_READY)
    {
        return;
    }

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {

        KeyOpFieldsValuesTuple t = it->second;

        string key = kfvKey(t);
        string op = kfvOp(t);
        FlexCounterManager* groupPtr = NULL;
        auto data = kfvFieldsValues(t);
        SWSS_LOG_NOTICE("FlexCounterOrch key is %s op is %s", key.c_str(), op.c_str());

        if (!flexCounterGroupMap.count(key))
        {
            SWSS_LOG_NOTICE("Invalid flex counter group input, %s", key.c_str());
            consumer.m_toSync.erase(it++);
            continue;
        }
        if (strcmp(key.c_str(), "1S_STAT_GAUGE") == 0)
        {
            groupPtr = &m_gaugeManager;
        }
        else if (strcmp(key.c_str(), "1S_STAT_COUNTER") == 0)
        {
            groupPtr = &m_counterManager;
        }
        else if (strcmp(key.c_str(), "1S_STAT_STATUS") == 0)
        {
            groupPtr = &m_statusManager;
        }

        if (op == SET_COMMAND)
        {
            for (auto valuePair : data)
            {
                const auto& field = fvField(valuePair);
                const auto& value = fvValue(valuePair);

                if (field == POLL_INTERVAL_FIELD)
                {
                    groupPtr->updateGroupPollingInterval(atoi(value.c_str()));
                }
                else if (field == FLEX_COUNTER_STATUS_FIELD)
                {
                    if (strcmp(value.c_str(), "enable") == 0)
                    {
                        groupPtr->enableFlexCounterGroup();
                    }
                    else
                    {
                        groupPtr->disableFlexCounterGroup();
                    }
                }
                else
                {
                    SWSS_LOG_NOTICE("Unsupported field %s", field.c_str());
                }
            }
        }

        consumer.m_toSync.erase(it++);
    }
    m_flexCounterGroupInit = true;
}

void FlexCounterOrch::initCounterTable()
{
    SWSS_LOG_ENTER();

    string json_file;
    shared_ptr<vector<KeyOpFieldsValuesTuple>> db_items_ptr;

    if (gFlexcounterJsonFile == "")
    {
        return;
    }

    json_file = gFlexcounterJsonFile;

    db_items_ptr = load_json(json_file.c_str());
    if (db_items_ptr != nullptr)
    {
        auto &db_items = *db_items_ptr;
        for (auto &db_item : db_items)
        {
            string key = kfvKey(db_item);
            string op = kfvOp(db_item);
   
            unordered_map<string, unordered_set<string>&>::iterator itr;
            if ((itr = g_flexCounterIds.find(key)) == g_flexCounterIds.end())
            {
                SWSS_LOG_NOTICE("Invalid flex counter input, %s", key.c_str());
                continue;
            }

            SWSS_LOG_NOTICE("key is %s op is %s", key.c_str(), op.c_str());
    
            if (op == SET_COMMAND)
            {
                for (auto i : kfvFieldsValues(db_item))
                {
                    const auto& value = fvValue(i);
                    itr->second.insert(value);
                }
            }
        }
    }

    m_flexCounterInit = true;
}

void FlexCounterOrch::doTask(Consumer& consumer)
{
    SWSS_LOG_ENTER();

    string table_name = consumer.getTableName();

    if (table_name == CFG_FLEX_COUNTER_GROUP_TABLE_NAME)
    {
        doCounterGroupTask(consumer);
    }
    else
    {
        SWSS_LOG_ERROR("Invalid table %s", table_name.c_str());
    }
}

