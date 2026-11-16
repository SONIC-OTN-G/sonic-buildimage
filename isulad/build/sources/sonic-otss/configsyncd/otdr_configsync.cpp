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
 
#include "otdr_configsync.h"
#include "swss/tokenize.h"
 
using namespace std;
using namespace swss;

std::mutex OtdrScanningMgr::m_mtx;

#define MUTEX std::unique_lock<std::mutex> _lock(OtdrScanningMgr::m_mtx);
#define MUTEX_UNLOCK _lock.unlock();
 
OtdrScanningMgr::OtdrScanningMgr(string otdrName, 
                                 bool enable,
                                 uint64_t startTime,
                                 uint32_t period):
        m_otdrName(otdrName),
        m_applDb("APPL_DB", 0),
        m_stateDb("STATE_DB", 0),
        m_stateTable(&m_stateDb, STATE_OT_OTDR_TABLE_NAME),
        m_queryChannel(&m_applDb, OT_OTDR_NOTIFICATION),
        m_replyChannel(&m_applDb, OT_OTDR_REPLY),
        m_startTime(startTime)
{
    SWSS_LOG_ENTER();

    if (period)
    {
        m_period = period;
    }
    else
    {
        m_period = 30 * 60 * 1000; /* 30 minutes */
    }

    if (enable)
    {
        startScanningThread();
    }
}
 
OtdrScanningMgr::~OtdrScanningMgr()
{
    SWSS_LOG_ENTER();
 
    stopScanningThread();
}
 
void OtdrScanningMgr::startScanningThread()
{
    SWSS_LOG_ENTER();
 
    m_runScanningThread = true;
 
    m_scanningThread = make_shared<thread>(&OtdrScanningMgr::scanningThreadRunFunction, this);
 
    SWSS_LOG_NOTICE("%s, OTDR scanning Thread started.", m_otdrName.c_str());
}
 
void OtdrScanningMgr::stopScanningThread()
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
 
        SWSS_LOG_NOTICE("%s, OTDR scanning Thread ended.", m_otdrName.c_str());
    }
}

bool OtdrScanningMgr::queryScanningStatus(OtdrScanningStatus &status)
{
    SWSS_LOG_ENTER();

    bool ret = false;

    string value;

    if (m_stateTable.hget(m_otdrName, "scanning-status", value))
    {
        if (value == "INACTIVE")
        {
            ret = true;
            status = OTDR_SCANNING_STATUS_INACTIVE;
        }
        else if (value == "ACTIVE")
        {
            ret = true;
            status = OTDR_SCANNING_STATUS_ACTIVE;
        }
    }

    return ret;
}

void OtdrScanningMgr::scan()
{
    SWSS_LOG_ENTER();

    OtdrScanningStatus scanningStatus = OTDR_SCANNING_STATUS_ACTIVE;

    if (!queryScanningStatus(scanningStatus))
    {
        SWSS_LOG_INFO("skip scan %s, get scanning status failed", m_otdrName.c_str());
        return;
    }

    if (scanningStatus == OTDR_SCANNING_STATUS_ACTIVE)
    {
        SWSS_LOG_INFO("skip scan %s, it is inactive", m_otdrName.c_str());
        return;
    }
 
    string op = "set";
 
    FieldValueTuple fv = std::make_pair("scan", "true");
 
    vector<FieldValueTuple> fvs = { fv };
 
    swss::Select s;
    s.addSelectable(&m_replyChannel);
    swss::Selectable *sel;

    int waitTime = 20000; // 20 seconds

    m_queryChannel.send(op, m_otdrName, fvs);

    int result = s.select(&sel, waitTime);
 
    if (result == swss::Select::TIMEOUT)
    {
        SWSS_LOG_ERROR("otdr scanning timeout, key=%s", m_otdrName.c_str());
        return;
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
            if (key == m_otdrName)
            {
                found = true;
                op_ret = kfvOp(entry);
            }
        }
 
        if (found && op_ret != "SUCCESS")
        {
            SWSS_LOG_INFO("otdr failed to scan, key=%s, ret=%s",
                          m_otdrName.c_str(), op_ret.c_str());
        }
    }
}
 
void OtdrScanningMgr::scanningThreadRunFunction()
{
    SWSS_LOG_ENTER();

    if (m_startTime)
    {
        const auto p = chrono::system_clock::now().time_since_epoch();

        auto currentTimeMs = chrono::duration_cast<chrono::milliseconds>(p).count();

        long startTimeMs = m_startTime / 1000000;

        long delay;

        if (startTimeMs >= currentTimeMs)
        {
            delay = startTimeMs - currentTimeMs;
        }
        else if (startTimeMs < currentTimeMs)
        {
            delay = m_period - ((currentTimeMs - startTimeMs) % m_period);
        }

        SWSS_LOG_INFO("%s, delay %ld", m_otdrName.c_str(), delay);
        std::unique_lock<std::mutex> lk(m_mtxSleep);
        m_cvSleep.wait_for(lk, std::chrono::milliseconds(delay));
    }
 
    while (m_runScanningThread)
    {
        auto start = std::chrono::steady_clock::now();

        SWSS_LOG_INFO("Begin to scan, otdr: %s", m_otdrName.c_str());

        MUTEX;

        scan();

        MUTEX_UNLOCK;

        auto finish = std::chrono::steady_clock::now();

        uint32_t delay = static_cast<uint32_t>(chrono::duration_cast<chrono::milliseconds>(finish - start).count());

        uint32_t correction = delay % m_period;

        correction = m_period - correction;
 
        if (m_runScanningThread == false)
        {
            break;
        }

        std::unique_lock<std::mutex> lk(m_mtxSleep);
 
        m_cvSleep.wait_for(lk, std::chrono::milliseconds(correction));
    }
}
 
void OtdrScanningMgr::updatePeriodicScanningParameter(
        bool enable,
        uint64_t startTime,
        uint32_t period)
{
    SWSS_LOG_ENTER();

    if (enable == m_runScanningThread &&
        startTime == m_startTime && 
        period == m_period)
    {
        return;
    }

    stopScanningThread();

    m_startTime = startTime;

    if (period)
    {
        m_period = period;
    }
    else
    {
        m_period = 30 * 60 * 1000; /* 30 minutes */
    }

    if (enable)
    {
        startScanningThread();
    }
}
 
OtdrConfigSync::OtdrConfigSync():
        ConfigSync("otdrsync", CFG_OT_OTDR_TABLE_NAME, APP_OT_OTDR_TABLE_NAME)
{
    SWSS_LOG_ENTER();
}
 
OtdrConfigSync::~OtdrConfigSync()
{
    SWSS_LOG_ENTER();
}
 
bool OtdrConfigSync::handleConfigFromConfigDB()
{
    SWSS_LOG_ENTER();
 
    if (ConfigSync::handleConfigFromConfigDB() == false)
    {
        return false;
    }
 
    for (auto &k: m_entries)
    {
        vector<FieldValueTuple> fvs;

        m_cfg_table.get(k, fvs);

        bool enable = false;

        uint64_t startTime = 0;

        uint32_t period = 30 * 60 * 1000; /* 30 minutes */

        for (auto &fv : fvs)
        {
            string &field = fvField(fv);
            string &value = fvValue(fv);

            if (field == "enable")
            {
                enable = (value == "true") ? true : false;
            }
            else if (field == "start-time")
            {
                char *endptr = NULL;
                startTime = (uint64_t)strtoull(value.c_str(), &endptr, 10); 
            }
            else if (field == "period")
            {
                char *endptr = NULL;
                period = (uint32_t)strtoull(value.c_str(), &endptr, 10) * 1000;
            }
        }

        m_otdrScanningMgrs[k] = make_shared<OtdrScanningMgr>(k,
                                                             enable,
                                                             startTime,
                                                             period);
    }
 
    return true;
}
 
vector<Selectable *> OtdrConfigSync::getSelectables()
{
    SWSS_LOG_ENTER();
 
    vector<Selectable *> selectables;
 
    selectables.push_back(&m_sst);
 
    return selectables;
}
 
void OtdrConfigSync::doTask(Selectable *select)
{
    SWSS_LOG_ENTER();
 
    if (select == (Selectable *)&m_sst)
    {
        std::deque<KeyOpFieldsValuesTuple> entries;
        m_sst.pops(entries);
    
        for (auto entry: entries)
        {
            string key = kfvKey(entry);

            if (m_entries.find(key) == m_entries.end())
            {
                SWSS_LOG_ERROR("%s|%s is invalid", m_service_name.c_str(), key.c_str());
                continue;
            }

            SWSS_LOG_NOTICE("Getting %s configuration changed, key is %s",
                            m_service_name.c_str(), key.c_str());

            if (m_cfg_map.find(key) != m_cfg_map.end())
            {
                SWSS_LOG_NOTICE("droping previous pending config");
                m_cfg_map.erase(key);
            }
            m_cfg_map[key] = entry;

        }

        handleConfig(m_cfg_map);

        for (auto entry: entries)
        {
            setPeriodicScanningParameter(entry);
        }
    }
}

void OtdrConfigSync::setPeriodicScanningParameter(KeyOpFieldsValuesTuple &entry)
{
    SWSS_LOG_ENTER();

    string key = kfvKey(entry);

    if (m_otdrScanningMgrs.find(key) == m_otdrScanningMgrs.end())
    {
        return;
    }

    std::shared_ptr<OtdrScanningMgr> mgr = m_otdrScanningMgrs[key];

    bool enable = false;

    uint64_t startTime = 0;

    uint32_t period = 30 * 60 * 1000; /* 30 minutes */

    for (auto &fv : kfvFieldsValues(entry))
    {
        string &field = fvField(fv);
        string &value = fvValue(fv);

        if (field == "enable")
        {
            enable = (value == "true") ? true : false;
        }
        else if (field == "start-time")
        {
            char *endptr = NULL;
            startTime = (uint64_t)strtoull(value.c_str(), &endptr, 10); 
        }
        else if (field == "period")
        {
            char *endptr = NULL;
            period = (uint32_t)strtoull(value.c_str(), &endptr, 10) * 1000;
        }
    }

    mgr->updatePeriodicScanningParameter(enable, startTime, period);
}

