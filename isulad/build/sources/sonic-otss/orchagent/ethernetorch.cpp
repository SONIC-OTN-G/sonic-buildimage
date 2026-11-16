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

#include "ethernetorch.h"
#include "flexcounterorch.h"

using namespace std;
using namespace swss;

extern otai_object_id_t gLinecardId;
extern FlexCounterOrch *gFlexCounterOrch;
extern std::unordered_set<std::string> ethernet_counter_ids_counter;
extern std::unordered_set<std::string> ethernet_counter_ids_status;

vector<otai_attr_id_t> g_ethernet_cfg_attrs =
{
    OTAI_ETHERNET_ATTR_CHANNEL_ID,
    OTAI_ETHERNET_ATTR_CLIENT_ALS,
    OTAI_ETHERNET_ATTR_ALS_DELAY,
    OTAI_ETHERNET_ATTR_CLIENT_RX_ALS,
    OTAI_ETHERNET_ATTR_CLIENT_RX_ALS_DELAY,
    OTAI_ETHERNET_ATTR_MAINTENANCE,
    OTAI_ETHERNET_ATTR_CLEAR_RMON,
};

vector<string> g_ethernet_auxiliary_fields =
{
    "index",
};

EthernetOrch::EthernetOrch(DBConnector *db, std::vector<TableConnector>& connectors)
    : OtaiObjectOrch(db, connectors, OTAI_OBJECT_TYPE_ETHERNET, g_ethernet_cfg_attrs, g_ethernet_auxiliary_fields)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OT_ETHERNET_TABLE_NAME));
    m_countersTable = COUNTERS_OT_ETHERNET_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OT_ETHERNET_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OT_ETHERNET_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OT_ETHERNET_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OT_ETHERNET_REPLY);

    m_createFunc = otai_ethernet_api->create_ethernet;
    m_removeFunc = otai_ethernet_api->remove_ethernet;
    m_setFunc = otai_ethernet_api->set_ethernet_attribute;
    m_getFunc = otai_ethernet_api->get_ethernet_attribute;
}

void EthernetOrch::setFlexCounter(otai_object_id_t id, vector<otai_attribute_t> &attrs)
{            
    gFlexCounterOrch->getCounterGroup()->setCounterIdList(id, CounterType::ETHERNET_COUNTER, ethernet_counter_ids_counter);
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::ETHERNET_STATUS, ethernet_counter_ids_status);
}

void EthernetOrch::clearFlexCounter(otai_object_id_t id, string key)
{
    gFlexCounterOrch->getCounterGroup()->clearCounterIdList(id);
    gFlexCounterOrch->getStatusGroup()->clearCounterIdList(id);
}
