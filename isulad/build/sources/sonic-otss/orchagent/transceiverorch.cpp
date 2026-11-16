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

#include <iostream>
#include <sstream>
#include <string>
#include "transceiverorch.h"
#include "flexcounterorch.h"
#include "otai_serialize.h"
#include "notifier.h"
#include "notificationproducer.h"
#include "notifications.h"

using namespace std;
using namespace swss;

extern FlexCounterOrch *gFlexCounterOrch;
extern std::unordered_set<std::string> transceiver_counter_ids_counter;
extern std::unordered_set<std::string> transceiver_counter_ids_gauge;
extern std::unordered_set<std::string> transceiver_counter_ids_status;

vector<otai_attr_id_t> g_transceiver_cfg_attrs =
{
    OTAI_TRANSCEIVER_ATTR_PORT_TYPE,
    OTAI_TRANSCEIVER_ATTR_PORT_ID,
    OTAI_TRANSCEIVER_ATTR_ENABLED,
    OTAI_TRANSCEIVER_ATTR_VENDOR_EXPECT,
    OTAI_TRANSCEIVER_ATTR_FEC_MODE,
    OTAI_TRANSCEIVER_ATTR_UPGRADE_DOWNLOAD,
    OTAI_TRANSCEIVER_ATTR_SWITCH_FLASH_PARTITION,
    OTAI_TRANSCEIVER_ATTR_BACKUP_FLASH_PARTITION,
    OTAI_TRANSCEIVER_ATTR_ETHERNET_PMD_PRECONF,
    OTAI_TRANSCEIVER_ATTR_POWER_MODE,
    OTAI_TRANSCEIVER_ATTR_RESET,
};

vector<string> g_transceiver_auxiliary_fields =
{
    "parent",
    "physical-channel",
    "logical-channel",
    "ethernet",
    "otn",
    "och",
    "interface",
};

TransceiverOrch::TransceiverOrch(DBConnector *db, std::vector<TableConnector>& connectors)
    : OtaiObjectOrch(db, connectors, OTAI_OBJECT_TYPE_TRANSCEIVER, g_transceiver_cfg_attrs, g_transceiver_auxiliary_fields)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OT_TRANSCEIVER_TABLE_NAME));
    m_countersTable = COUNTERS_OT_TRANSCEIVER_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OT_TRANSCEIVER_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OT_TRANSCEIVER_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OT_TRANSCEIVER_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OT_TRANSCEIVER_REPLY);

    m_db = db;
    m_upgrade_notification_consumer = new NotificationConsumer(db, "UPGRADE_TRANSCEIVER");
    auto upgrade_notifier = new Notifier(m_upgrade_notification_consumer, this, "UPGRADE_TRANSCEIVER");
    Orch::addExecutor(upgrade_notifier);
    m_pchTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OT_PHYSICALCHANNEL_TABLE_NAME));
    m_lchTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OT_LOGICALCHANNEL_TABLE_NAME));
    m_ethTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OT_ETHERNET_TABLE_NAME));
    m_otnTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OT_OTN_TABLE_NAME));
    m_ochTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OT_OCH_TABLE_NAME));
    m_intfTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OT_INTERFACE_TABLE_NAME));

    m_createFunc = otai_transceiver_api->create_transceiver;
    m_removeFunc = otai_transceiver_api->remove_transceiver;
    m_setFunc = otai_transceiver_api->set_transceiver_attribute;
    m_getFunc = otai_transceiver_api->get_transceiver_attribute;
}

void TransceiverOrch::doTask(NotificationConsumer& consumer)
{
    SWSS_LOG_ENTER();

    if (&consumer == m_upgrade_notification_consumer)
    {
        doUpgradeTask(consumer);
    }
}

void TransceiverOrch::getUpgradeState(otai_object_id_t oid)
{
    SWSS_LOG_ENTER();

    std::string op("state");
    std::string data("SUCCESS");
    std::vector<swss::FieldValueTuple> values;
    otai_attribute_t attr;
    memset(&attr, 0, sizeof(attr));
    otai_status_t status;
    NotificationProducer upgrade_reply(m_db, "UPGRADE_TRANSCEIVER_REPLY");

    attr.id = OTAI_TRANSCEIVER_ATTR_UPGRADE_STATE;
    status = otai_transceiver_api->get_transceiver_attribute(oid, 1, &attr);
    if (status != OTAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to get transceiver upgrade state, rv:%d", status);
        data = "FAILED";
        upgrade_reply.send(op, data, values);
        return;
    }
    auto meta = otai_metadata_get_attr_metadata(OTAI_OBJECT_TYPE_TRANSCEIVER, attr.id);
    if (meta == NULL)
    {
        SWSS_LOG_ERROR("Failed to get metadata for transceiver");
        data = "FAILED";
        upgrade_reply.send(op, data, values);
        return;
    }

    swss::FieldValueTuple fv("UPGRADE_STATE",
        otai_serialize_enum_v2(attr.value.s32, meta->enummetadata).c_str());
    values.push_back(fv);
    upgrade_reply.send(op, data, values);

    return;
}

void TransceiverOrch::doUpgradeTask(NotificationConsumer& consumer)
{
    SWSS_LOG_ENTER();

    otai_attribute_t attr;
    otai_status_t status;
    std::string key;
    otai_object_id_t oid = OTAI_NULL_OBJECT_ID;

    std::string op;
    std::string data;
    std::vector<swss::FieldValueTuple> values;
    consumer.pop(op, data, values);

    NotificationProducer upgrade_reply(m_db, "UPGRADE_TRANSCEIVER_REPLY");

    SWSS_LOG_NOTICE("UPGRADE_TRANSCEIVER notification for %s ", op.c_str());

    for (unsigned i = 0; i < values.size(); i++)
    {
        string& value = fvValue(values[i]);
        string& field = fvField(values[i]);
        if (field == "partition")
        {
            if (value == "A")
            {
                attr.value.s32 = OTAI_TRANSCEIVER_FLASH_PARTITION_A;
            }
            else if (value == "B")
            {
                attr.value.s32 = OTAI_TRANSCEIVER_FLASH_PARTITION_B;
            }
        }
        else if (field == "key")
        {
            key = value;
        }
    }

    if (m_key2oid.find(key) == m_key2oid.end())
    {
        SWSS_LOG_ERROR("Failed to find oid");
        data = "FAILED";
        upgrade_reply.send(op, data, values);
        return;
    }

    oid = m_key2oid[key];

    if (op == "download")
    {
        attr.id = OTAI_TRANSCEIVER_ATTR_UPGRADE_DOWNLOAD;
        attr.value.booldata = true;
    }
    else if (op == "switch")
    {
        attr.id = OTAI_TRANSCEIVER_ATTR_SWITCH_FLASH_PARTITION;
    }
    else if (op == "backup")
    {
        attr.id = OTAI_TRANSCEIVER_ATTR_BACKUP_FLASH_PARTITION;
    }
    else if (op == "state")
    {
        getUpgradeState(oid);
        return;
    }

    status = otai_transceiver_api->set_transceiver_attribute(oid, &attr);
    if (status != OTAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to upgrade transceiver, op=%s, attr_id=%d, status=%d",
            op.c_str(), attr.id, status);
        data = "FAILED";
        upgrade_reply.send(op, data, values);
        return;
    }
    data = "SUCCESS";
    upgrade_reply.send(op, data, values);

    return;
}

void tokenize(string const &str, const char delim, vector<string> &out)
{
    stringstream ss(str);
    string s;
    while (getline(ss, s, delim))
    {
        out.push_back(s);
    }
}

void TransceiverOrch::doSubobjectStateTask(const string &key, const string &present)
{
    vector<string> pch_keys;
    vector<string> lch_keys;
    vector<string> och_keys;
    vector<string> intf_keys;
    vector<string> eth_keys;
    vector<string> otn_keys;

    if (m_key2auxiliaryFvs.find(key) == m_key2auxiliaryFvs.end())
    {
        return;
    }

    for (auto fv: m_key2auxiliaryFvs[key])
    {
        if (fvField(fv) == "physical-channel")
        {
            tokenize(fvValue(fv), ',', pch_keys);
        }
        else if (fvField(fv) == "logical-channel")
        {
            tokenize(fvValue(fv), ',', lch_keys);
        }
        else if (fvField(fv) == "ethernet")
        {
            tokenize(fvValue(fv), ',', eth_keys);
        }
        else if (fvField(fv) == "otn")
        {
            tokenize(fvValue(fv), ',', otn_keys);
        }
        else if (fvField(fv) == "och")
        {
            tokenize(fvValue(fv), ',', och_keys);
        }
        else if (fvField(fv) == "interface")
        {
            tokenize(fvValue(fv), ',', intf_keys);
        }
    }

    for (auto &pch : pch_keys)
    {
        m_pchTable->hset(pch, "present", present);
    }

    for (auto &lch : lch_keys)
    {
        m_lchTable->hset(lch, "present", present);
    }

    for (auto &otn : otn_keys)
    {
        m_otnTable->hset(otn, "present", present);
    }

    for (auto &eth : eth_keys)
    {
        m_ethTable->hset(eth, "present", present);
    }

    for (auto &och : och_keys)
    {
        m_ochTable->hset(och, "present", present);
    }

    for (auto &intf : intf_keys)
    {
        m_intfTable->hset(intf, "present", present);
    }
}

void TransceiverOrch::setFlexCounter(otai_object_id_t id, vector<otai_attribute_t> &attrs)
{
    gFlexCounterOrch->getGaugeGroup()->setCounterIdList(id, CounterType::TRANSCEIVER_GAUGE, transceiver_counter_ids_gauge);
    gFlexCounterOrch->getCounterGroup()->setCounterIdList(id, CounterType::TRANSCEIVER_COUNTER, transceiver_counter_ids_counter);
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::TRANSCEIVER_STATUS, transceiver_counter_ids_status);
}

void TransceiverOrch::clearFlexCounter(otai_object_id_t id, string key)
{
    unordered_set<string> present_status = 
        { "OTAI_TRANSCEIVER_ATTR_PRESENT" };

    gFlexCounterOrch->getGaugeGroup()->clearCounterIdList(id);
    gFlexCounterOrch->getCounterGroup()->clearCounterIdList(id);
    gFlexCounterOrch->getStatusGroup()->clearCounterIdList(id);

    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::TRANSCEIVER_STATUS, present_status); 
}

