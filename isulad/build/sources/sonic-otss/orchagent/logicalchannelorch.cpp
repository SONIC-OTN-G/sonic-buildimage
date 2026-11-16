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

#include "logicalchannelorch.h"
#include "flexcounterorch.h"

using namespace std;

extern FlexCounterOrch *gFlexCounterOrch;
extern std::unordered_set<std::string> logicalch_counter_ids_status;
extern std::unordered_set<std::string> logicalch_counter_ids_counter;

vector<otai_attr_id_t> g_logicalchannel_cfg_attrs =
{
    OTAI_LOGICALCHANNEL_ATTR_CHANNEL_ID,
    OTAI_LOGICALCHANNEL_ATTR_ADMIN_STATE,
    OTAI_LOGICALCHANNEL_ATTR_LOOPBACK_MODE,
    OTAI_LOGICALCHANNEL_ATTR_TEST_SIGNAL_PATTERN,
    OTAI_LOGICALCHANNEL_ATTR_TX_TEST_SIGNAL_PATTERN,
    OTAI_LOGICALCHANNEL_ATTR_RX_TEST_SIGNAL_PATTERN,
    OTAI_LOGICALCHANNEL_ATTR_LINK_DOWN_DELAY_MODE,
    OTAI_LOGICALCHANNEL_ATTR_LINK_DOWN_DELAY_HOLD_OFF,
    OTAI_LOGICALCHANNEL_ATTR_LINK_UP_DELAY_MODE,
    OTAI_LOGICALCHANNEL_ATTR_LINK_UP_DELAY_HOLD_OFF,
    OTAI_LOGICALCHANNEL_ATTR_LINK_UP_DELAY_ACTIVE_THRESHOLD,
};

vector<string> g_lch_auxiliary_fields =
{
    "description",
    "transceiver",
    "index",
};

LogicalChannelOrch::LogicalChannelOrch(DBConnector *db, std::vector<TableConnector>& connectors)
    : OtaiObjectOrch(db, connectors, OTAI_OBJECT_TYPE_LOGICALCHANNEL, g_logicalchannel_cfg_attrs, g_lch_auxiliary_fields)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OT_LOGICALCHANNEL_TABLE_NAME));
    m_countersTable = COUNTERS_OT_LOGICALCHANNEL_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OT_LOGICALCHANNEL_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OT_LOGICALCHANNEL_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OT_LOGICALCHANNEL_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OT_LOGICALCHANNEL_REPLY);

    m_createFunc = otai_logicalchannel_api->create_logicalchannel;
    m_removeFunc = otai_logicalchannel_api->remove_logicalchannel;
    m_setFunc = otai_logicalchannel_api->set_logicalchannel_attribute;
    m_getFunc = otai_logicalchannel_api->get_logicalchannel_attribute;
}

void LogicalChannelOrch::setFlexCounter(otai_object_id_t id, vector<otai_attribute_t> &attrs)
{    
    gFlexCounterOrch->getCounterGroup()->setCounterIdList(id, CounterType::LOGICALCH_COUNTER, logicalch_counter_ids_counter);        
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::LOGICALCH_STATUS, logicalch_counter_ids_status);
}

void LogicalChannelOrch::clearFlexCounter(otai_object_id_t id, string key)
{
    gFlexCounterOrch->getCounterGroup()->clearCounterIdList(id);
    gFlexCounterOrch->getStatusGroup()->clearCounterIdList(id);
}

