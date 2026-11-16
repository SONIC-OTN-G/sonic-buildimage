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

#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <otairedis.h>
#include "linecardorch.h"
#include "flexcounterorch.h"
#include "otai_serialize.h"
#include "notifier.h"
#include "notificationproducer.h"
#include "notifications.h"
#include "orchfsm.h"
#include "subscriberstatetable.h"
#include "otaihelper.h"
#include "timestamp.h"
#include "converter.h"

using namespace std;
using namespace swss;

extern FlexCounterOrch *gFlexCounterOrch;
extern std::unordered_set<std::string> linecard_counter_ids_status;
extern std::unordered_set<std::string> linecard_counter_ids_gauge;
extern std::unordered_set<std::string> linecard_counter_ids_counter;
extern int gSlotId;
extern FlexCounterOrch* gFlexCounterOrch;

vector<otai_attr_id_t> g_linecard_cfg_attrs =
{
    OTAI_LINECARD_ATTR_LINECARD_TYPE,
    OTAI_LINECARD_ATTR_BOARD_MODE,
    OTAI_LINECARD_ATTR_RESET,
    OTAI_LINECARD_ATTR_HOSTNAME,
    OTAI_LINECARD_ATTR_BAUD_RATE,
    OTAI_LINECARD_ATTR_COLLECT_LINECARD_LOG,
    OTAI_LINECARD_ATTR_HOST_IP,
    OTAI_LINECARD_ATTR_USER_NAME,
    OTAI_LINECARD_ATTR_USER_PASSWORD,
    OTAI_LINECARD_ATTR_UPGRADE_FILE_NAME,
    OTAI_LINECARD_ATTR_UPGRADE_FILE_PATH,
    OTAI_LINECARD_ATTR_UPGRADE_DOWNLOAD,
    OTAI_LINECARD_ATTR_UPGRADE_AUTO,
    OTAI_LINECARD_ATTR_UPGRADE_COMMIT,
    OTAI_LINECARD_ATTR_UPGRADE_COMMIT_PAUSE,
    OTAI_LINECARD_ATTR_UPGRADE_COMMIT_RESUME,
    OTAI_LINECARD_ATTR_UPGRADE_ROLLBACK,
    OTAI_LINECARD_ATTR_UPGRADE_REBOOT,
    OTAI_LINECARD_ATTR_LED_MODE,
    OTAI_LINECARD_ATTR_LED_FLASH_INTERVAL,
};

vector<otai_attr_id_t> g_linecard_state_attrs = 
{
    OTAI_LINECARD_ATTR_UPGRADE_STATE,
};

LinecardOrch::LinecardOrch(DBConnector *db, std::vector<TableConnector>& connectors)
    : OtaiObjectOrch(db, connectors, OTAI_OBJECT_TYPE_LINECARD, g_linecard_cfg_attrs)
{
    SWSS_LOG_ENTER();

    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OT_LINECARD_TABLE_NAME));
    m_countersTable = COUNTERS_OT_LINECARD_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OT_LINECARD_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OT_LINECARD_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OT_LINECARD_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OT_LINECARD_REPLY);

    std::string oper_status;
    std::string linecard_key = "LINECARD-1-" + std::to_string(gSlotId);
    if (m_stateTable->hget(linecard_key, "oper-status", oper_status))
    {   
        if (oper_status == "ACTIVE")
        {   
            SWSS_LOG_NOTICE("Orchagent go into ready state");
            OrchFSM::setState(ORCH_STATE_READY);
        }   
    }

    m_config_total_num = 0;
    m_config_total_num_inited = false;
    m_config_num = 0;

    m_setFunc = otai_linecard_api->set_linecard_attribute;
    m_getFunc = otai_linecard_api->get_linecard_attribute;

    for (auto i : g_linecard_state_attrs)
    {
        auto meta = otai_metadata_get_attr_metadata(OTAI_OBJECT_TYPE_LINECARD, i);
        if (meta == NULL)
        {   
            SWSS_LOG_ERROR("invalid attr, object=LINECARD, attr=%d", i);
            continue;
        }
        if (meta->isreadonly == true)
        {
            m_readonlyAttrs[meta->attridkebabname] = i;
        }
    }
}

void LinecardOrch::initConfigTotalNum(int num)
{
    SWSS_LOG_ENTER();

    if (num < 0)
    {   
        SWSS_LOG_ERROR("Invalid config total number, num=%d", num);
        return;
    }   
    m_config_total_num = num;
    m_config_total_num_inited = true;

    SWSS_LOG_NOTICE("Config total number=%d", num);

    if (m_config_num >= m_config_total_num)
    {   
        stopPreConfigProc();
        onLinecardActive();
    }
}

void LinecardOrch::incConfigNum()
{
    SWSS_LOG_ENTER();

    m_config_num++;

    if (!m_config_total_num_inited)
    {   
        return;
    }   

    if (m_config_num >= m_config_total_num)
    {   
        stopPreConfigProc();
        onLinecardActive();
    }
}

void LinecardOrch::stopPreConfigProc()
{
    SWSS_LOG_ENTER();

    otai_status_t status;
    otai_attribute_t attr;

    SWSS_LOG_NOTICE("Pre-Config Finished.");

    attr.id = OTAI_LINECARD_ATTR_STOP_PRE_CONFIGURATION;
    attr.value.booldata = true;
    status = otai_linecard_api->set_linecard_attribute(gLinecardId, &attr);
    if (status != OTAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to notify Otai pre-config finish %d", status);
    }
}

void LinecardOrch::doAppLinecardTableTask(Consumer& consumer)
{
    SWSS_LOG_ENTER();

    if (OrchFSM::getState() == ORCH_STATE_NOT_READY)
    {
        return;
    }

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        auto t = it->second;
        auto op = kfvOp(t);
        string key = kfvKey(t);

        if (op == SET_COMMAND)
        {
            string operation_id;

            map<string, string> create_attrs;
            map<string, string> set_attrs;

            for (auto i : kfvFieldsValues(t))
            {
                if (fvField(i) == "operation-id")
                {
                    operation_id = fvValue(i);
                }
                else if (fvField(i) == "object-count")
                {
                    initConfigTotalNum(stoi(fvValue(i)));
                }
                else if (fvField(i) == "board-mode")
                {
                    create_attrs[fvField(i)] = fvValue(i);
                }
                else if (m_createandsetAttrs.find(fvField(i)) != m_createandsetAttrs.end())
                {
                    set_attrs[fvField(i)] = fvValue(i);
                }
                if (m_mandatoryAttrs.find(fvField(i)) != m_mandatoryAttrs.end())
                {
                    create_attrs[fvField(i)] = fvValue(i);
                }
            }
            if (OrchFSM::getState() == ORCH_STATE_READY)
            {
                createLinecard(key, create_attrs);
            }
            if (OrchFSM::getState() == ORCH_STATE_WORK)
            {
                setOtaiObjectAttrs(key, set_attrs, operation_id);
            }
            it = consumer.m_toSync.erase(it);
        }
        else
        {
            SWSS_LOG_WARN("Unsupported operation %s ", op.c_str());
            it = consumer.m_toSync.erase(it);
        }
    }
}

void LinecardOrch::doLinecardStateTask(Consumer& consumer)
{
    SWSS_LOG_ENTER();

    if (OrchFSM::getState() != ORCH_STATE_NOT_READY)
    {   
        return;
    }

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        auto t = it->second;
        auto op = kfvOp(t);
        for (auto i : kfvFieldsValues(t))
        {
            if (fvField(i) == "oper-status")
            {
                if (strcmp(fvValue(i).c_str(), "ACTIVE") == 0)
                {
                    SWSS_LOG_NOTICE("Linecard is active.");
                    OrchFSM::setState(ORCH_STATE_READY);
                }
                else
                {
                    SWSS_LOG_NOTICE("Linecard is inactive.");
                }
            }
        }
        it = consumer.m_toSync.erase(it);
    }
}

void LinecardOrch::doTask(Consumer& consumer)
{
    SWSS_LOG_ENTER();

    const string& table_name = consumer.getTableName();

    if (table_name == APP_OT_LINECARD_TABLE_NAME)
    {
        doAppLinecardTableTask(consumer);
    }
    else if (table_name == STATE_OT_LINECARD_TABLE_NAME)
    {
        doLinecardStateTask(consumer);
    }
}

void LinecardOrch::createLinecard(
    const string& key,
    const map<string, string>& create_attrs)
{
    SWSS_LOG_ENTER();

    otai_attribute_t attr;
    vector<otai_attribute_t> attrs;
    otai_status_t status;
    bool is_board_mode_existed = false;
    string board_mode;
 
    for (auto fv: create_attrs)
    {
        if (translateOtaiObjectAttr(fv.first, fv.second, attr) == false)
        {
            SWSS_LOG_ERROR("Failed to translate linecard attr, %s", fv.first.c_str());
            continue;
        }
        if (attr.id == OTAI_LINECARD_ATTR_BOARD_MODE)
        {
            is_board_mode_existed = true;
            board_mode = (attr.value.chardata);
            continue;
        }
        attrs.push_back(attr);
    }

    gFlexCounterOrch->initCounterTable();

    attr.id = OTAI_LINECARD_ATTR_LINECARD_ALARM_NOTIFY;
    attr.value.ptr = (void*)onLinecardAlarmNotify;
    attrs.push_back(attr);

    attr.id = OTAI_LINECARD_ATTR_LINECARD_STATE_CHANGE_NOTIFY;
    attr.value.ptr = (void*)onLinecardStateChange;
    attrs.push_back(attr);

    attr.id = OTAI_LINECARD_ATTR_COLLECT_LINECARD_ALARM;
    attr.value.booldata = true;
    attrs.push_back(attr);

    attr.id = OTAI_LINECARD_ATTR_LINECARD_OCM_SPECTRUM_POWER_NOTIFY;
    attr.value.ptr = (void*)onOcmSpectrumPowerNotify;
    attrs.push_back(attr);

    attr.id = OTAI_LINECARD_ATTR_LINECARD_OTDR_RESULT_NOTIFY;
    attr.value.ptr = (void*)onOtdrResultNotify;
    attrs.push_back(attr);

    otai_linecard_api->set_linecard_attribute(gLinecardId, &attr);

    status = otai_linecard_api->create_linecard(&gLinecardId, (uint32_t)attrs.size(), attrs.data());
    if (status != OTAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to create a linecard, rv:%d", status);
        exit(EXIT_FAILURE);
    }
    SWSS_LOG_NOTICE("Create a linecard, id:%" PRIu64, gLinecardId);

    m_key2oid[key] = gLinecardId;

    attr.id = OTAI_LINECARD_ATTR_START_PRE_CONFIGURATION;
    attr.value.booldata = true;
    status = otai_linecard_api->set_linecard_attribute(gLinecardId, &attr);
    if (status != OTAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to notify Otai start pre-config %d", status);
    }

    if (is_board_mode_existed)
    {
        setBoardMode(board_mode);
    }

    FieldValueTuple tuple(otai_serialize_object_id(gLinecardId), key);
    vector<FieldValueTuple> fields;
    fields.push_back(tuple);
    m_nameMapTable->set("", fields);

    m_vid2NameTable->set("", fields);

    setFlexCounter(gLinecardId);

    OrchFSM::setState(ORCH_STATE_WORK);
}

void LinecardOrch::setBoardMode(std::string mode)
{
    SWSS_LOG_ENTER();

    int wait_count = 0;
    otai_attribute_t attr;
    otai_status_t status;

    attr.id = OTAI_LINECARD_ATTR_BOARD_MODE;
    memset(attr.value.chardata, 0, sizeof(attr.value.chardata));
    status = otai_linecard_api->get_linecard_attribute(gLinecardId, 1, &attr);
    if (status == OTAI_STATUS_SUCCESS && mode == attr.value.chardata)
    {
        SWSS_LOG_DEBUG("Linecard and maincard have a same board-mode, %s", mode.c_str());
        return;
    }

    SWSS_LOG_NOTICE("Begin to set board-mode %s", mode.c_str());

    memset(attr.value.chardata, 0, sizeof(attr.value.chardata));
    strncpy(attr.value.chardata, mode.c_str(), sizeof(attr.value.chardata) - 1);
    status = otai_linecard_api->set_linecard_attribute(gLinecardId, &attr);
    if (status != OTAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to set board-mode status=%d, mode=%s",
                       status, mode.c_str());
        return;
    }

    do
    {
        wait_count++;
        this_thread::sleep_for(chrono::milliseconds(1000));
        status = otai_linecard_api->get_linecard_attribute(gLinecardId, 1, &attr);
        if (status != OTAI_STATUS_SUCCESS)
        {
            continue;
        }
        SWSS_LOG_DEBUG("board-mode = %s", attr.value.chardata);
        if (mode == attr.value.chardata)
        {
            break;
        }
    } while (wait_count < 10 * 60); /* 10 minutes is enough for P230C to change its boardmode */

    SWSS_LOG_NOTICE("The end of setting board-mode");
}

void LinecardOrch::setFlexCounter(otai_object_id_t id)
{
    gFlexCounterOrch->getGaugeGroup()->setCounterIdList(id, CounterType::LINECARD_GAUGE, linecard_counter_ids_gauge);
    gFlexCounterOrch->getCounterGroup()->setCounterIdList(id, CounterType::LINECARD_COUNTER, linecard_counter_ids_counter);
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::LINECARD_STATUS, linecard_counter_ids_status);
}

