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

#include <map>
#include <set>
#include <string>
#include <thread>
#include <chrono>

#include "ocm_configsync.h"
#include "swss/tokenize.h"

using namespace std;
using namespace swss;

#define MUTEX std::unique_lock<std::mutex> _lock(m_mtx);
#define MUTEX_UNLOCK _lock.unlock();

OcmGroupMgr::OcmGroupMgr(string groupName):
        m_groupName(groupName),
        m_runScanningThread(false),
        m_cfgDb("CONFIG_DB", 0),
        m_applDb("APPL_DB", 0),
        m_stateDb("STATE_DB", 0),
        m_cfgOcmGroupTable(&m_cfgDb, CFG_OT_OCM_GROUP_TABLE_NAME),
        m_stateTable(&m_stateDb, STATE_OT_OCM_TABLE_NAME),
        m_appTable(&m_applDb, APP_OT_OCM_TABLE_NAME),
        m_queryChannel(&m_applDb, OT_OCM_NOTIFICATION),
        m_replyChannel(&m_applDb, OT_OCM_REPLY)
{
    SWSS_LOG_ENTER();

    vector<FieldValueTuple> fvs;

    bool updateFreqGranularity = false;

    m_cfgOcmGroupTable.get(groupName, fvs);

    for (auto &fv : fvs)
    {
        if (fv.first == "ocm-list")
        {
            m_ocmList = swss::tokenize(fv.second, ',');
        }
        else if (fv.first == "frequency-granularity")
        {
            m_freqGranularity = fv;
            updateFreqGranularity = true;
        }
    }

    if (m_ocmList.empty())
    {
        return;
    }

    if (updateFreqGranularity)
    {
        vector<FieldValueTuple> fvs = { m_freqGranularity };

        for (auto &ocm : m_ocmList)
        {
            m_appTable.set(ocm, fvs);
        }
    }

    startScanningThread();
}

OcmGroupMgr::~OcmGroupMgr()
{
    SWSS_LOG_ENTER();

    stopScanningThread();
}

std::string OcmGroupMgr::getGroupName()
{
    return m_groupName;
}

void OcmGroupMgr::startScanningThread()
{
    SWSS_LOG_ENTER();

    m_runScanningThread = true;

    m_scanningThread = make_shared<thread>(&OcmGroupMgr::scanningThreadRunFunction, this);

    SWSS_LOG_NOTICE("%s, OCM scanning Thread started.", m_groupName.c_str());
}

void OcmGroupMgr::stopScanningThread()
{
    SWSS_LOG_ENTER();

    if (m_runScanningThread)
    {
        m_runScanningThread = false;

        m_cvSleep.notify_all();

        if (m_scanningThread != nullptr)
        {
            auto fcThread = std::move(m_scanningThread);

            fcThread->join();
        }

        SWSS_LOG_NOTICE("%s, OCM scanning Thread ended.", m_groupName.c_str());
    }
}

OcmScanStatus OcmGroupMgr::scan(std::string ocm)
{
    SWSS_LOG_ENTER();

    string op = "set";

    FieldValueTuple fv = std::make_pair("scan", "true");

    vector<FieldValueTuple> fvs = { fv };

    swss::Select s;
    s.addSelectable(&m_replyChannel);
    swss::Selectable *sel;

    int waitTime = 20000;
    
    while (true)
    {
        m_queryChannel.send(op, ocm, fvs);

        int result = s.select(&sel, waitTime);

        if (result == swss::Select::TIMEOUT)
        {
            SWSS_LOG_ERROR("ocm scanning timeout, key=%s", ocm.c_str());

            return OCM_SCAN_TIMEOUT;
        }
        else if (result == swss::Select::OBJECT)
        {
            std::deque<KeyOpFieldsValuesTuple> entries;
            m_replyChannel.pops(entries);

            bool found = false;
            string op_ret;

            for (auto entry : entries)
            {
                string key = kfvKey(entry);
                if (key == ocm)
                {
                    found = true;
                    op_ret = kfvOp(entry);
                }
            }

            if (!found)
            {
                continue;
            }

            if (op_ret == "SUCCESS")
            {
                return OCM_SCAN_SUCCESS;
            }
            else if (op_ret == "UNAVAILABLE")
            {
                return OCM_SCAN_UNAVAILABLE;
            }
            else
            {
                return OCM_SCAN_FAILURE;
            }
        }
    }

    return OCM_SCAN_FAILURE;
}

void OcmGroupMgr::scanningThreadRunFunction()
{
    SWSS_LOG_ENTER();

    while (m_runScanningThread)
    {
        int waitTime = 100;

        MUTEX;

        for (auto &ocm : m_ocmList)
        {
            SWSS_LOG_INFO("Begin to scan, ocm_group: %s, ocm: %s",
                           m_groupName.c_str(), ocm.c_str());

            auto status = scan(ocm);

            if (status == OCM_SCAN_UNAVAILABLE)
            {
                waitTime = 5000; 
                break;
            }

            if (m_runScanningThread == false)
            {
                break;
            }
        }

        MUTEX_UNLOCK;

        if (m_runScanningThread == false)
        {
            break;
        }

        std::unique_lock<std::mutex> lk(m_mtxSleep);

        m_cvSleep.wait_for(lk, std::chrono::milliseconds(waitTime));
    }
}

void OcmGroupMgr::handleConfig(KeyOpFieldsValuesTuple &entry)
{
    SWSS_LOG_ENTER();

    string key = kfvKey(entry);
    string op = kfvOp(entry);
    auto fvs = kfvFieldsValues(entry);

    if (op == SET_COMMAND)
    {
        stopScanningThread();

        MUTEX;

        bool updateFreqGranularity = false;
        bool updateOcmList = false;
        string ocmList = "";
        string operationId = "";

        for (auto &fv : fvs)
        {
            if (fv.first == "ocm-list")
            {
                m_ocmList = swss::tokenize(fv.second, ',');
                updateOcmList = true;
                ocmList = fv.second;
            }
            else if (fv.first == "frequency-granularity")
            {
                m_freqGranularity = fv;
                updateFreqGranularity = true;
            }
            else if (fv.first == "operation-id")
            {
                operationId = fv.second;
            }
        }

        if (updateFreqGranularity)
        {
            vector<FieldValueTuple> fvs;

            fvs.push_back(m_freqGranularity);

            if (operationId != "")
            {
               fvs.push_back({"operation-id", operationId});
            }
    
            for (auto &ocm : m_ocmList)
            {
                m_appTable.set(ocm, fvs);
            }

        }

        MUTEX_UNLOCK;

        startScanningThread();

        if (updateOcmList && operationId != "")
        {
            string channel = "ocm-list";
            channel += "-" + operationId;
            string error_msg = "Set " + key + " ocm-list to " + ocmList;
            swss::NotificationProducer notifications(&m_stateDb, channel);
            std::vector<swss::FieldValueTuple> entry;
            notifications.send("0", error_msg, entry);
        }

        if (updateFreqGranularity && operationId != "" && m_ocmList.empty())
        {
            string channel = "frequency-granularity";
            channel += "-" + operationId;
            string error_msg = "Set " + key + " frequency-granularity to " + m_freqGranularity.second;
            swss::NotificationProducer notifications(&m_stateDb, channel);
            std::vector<swss::FieldValueTuple> entry;
            notifications.send("0", error_msg, entry);
        }
    }
}

OcmConfigSync::OcmConfigSync():
        ConfigSync("ocmsync", CFG_OT_OCM_TABLE_NAME, APP_OT_OCM_TABLE_NAME),
        m_cfgOcmGroupTable(&m_cfg_db, CFG_OT_OCM_GROUP_TABLE_NAME),
        m_cfgOcmGroupSubStateTable(&m_cfg_db, CFG_OT_OCM_GROUP_TABLE_NAME)
{
    SWSS_LOG_ENTER();
}

OcmConfigSync::~OcmConfigSync()
{
    SWSS_LOG_ENTER();

    m_ocmGroupMgr = nullptr;
}

void OcmConfigSync::handleOcmGroupConfig()
{
    SWSS_LOG_ENTER();

    vector<string> ocmGroupKeys;

    m_cfgOcmGroupTable.getKeys(ocmGroupKeys);

    SWSS_LOG_NOTICE("ocmGroupKeys size = %ld", ocmGroupKeys.size());

    if (ocmGroupKeys.empty())
    {
        SWSS_LOG_NOTICE("No ocm group configuration in ConfigDB");

        return;
    }

    if (ocmGroupKeys.size() > 1)
    {
        SWSS_LOG_ERROR("Multiple ocm groups configuration in ConfigDB");

        return;
    }

    m_ocmGroupMgr = make_shared<OcmGroupMgr>(ocmGroupKeys.front());
}

bool OcmConfigSync::handleConfigFromConfigDB()
{
    SWSS_LOG_ENTER();

    if (ConfigSync::handleConfigFromConfigDB() == false)
    {
        return false;
    }

    handleOcmGroupConfig();

    return true;
}

vector<Selectable *> OcmConfigSync::getSelectables()
{
    SWSS_LOG_ENTER();

    vector<Selectable *> selectables;

    selectables.push_back(&m_sst);

    selectables.push_back(&m_cfgOcmGroupSubStateTable);

    return selectables;
}

void OcmConfigSync::doTask(Selectable *select)
{
    SWSS_LOG_ENTER();

    ConfigSync::doTask(select);

    if (select == (Selectable *)&m_cfgOcmGroupSubStateTable)
    {
        std::deque<KeyOpFieldsValuesTuple> entries;
        m_cfgOcmGroupSubStateTable.pops(entries);

        for (auto entry : entries)
        {
            string key = kfvKey(entry);

            if (m_ocmGroupMgr == nullptr)
            {
               m_ocmGroupMgr = make_shared<OcmGroupMgr>(key); 
            }
            else if (m_ocmGroupMgr->getGroupName() != key)
            {
                SWSS_LOG_ERROR("%s is invalid", key.c_str());
                continue;
            }

            m_ocmGroupMgr->handleConfig(entry);
        }
    }

    return;
}

