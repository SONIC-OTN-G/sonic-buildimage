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

#include "physicalchannelorch.h"
#include "flexcounterorch.h"
#include "otai_serialize.h"

using namespace std;
using namespace swss;

extern FlexCounterOrch *gFlexCounterOrch;
extern std::unordered_set<std::string> physicalch_counter_ids_counter;
extern std::unordered_set<std::string> physicalch_counter_ids_gauge;
extern std::unordered_set<std::string> physicalch_counter_ids_status;

vector<otai_attr_id_t> g_physicalchannel_cfg_attrs =
{
    OTAI_PHYSICALCHANNEL_ATTR_PORT_TYPE,
    OTAI_PHYSICALCHANNEL_ATTR_PORT_ID,
    OTAI_PHYSICALCHANNEL_ATTR_LANE_ID,
};

PhysicalChannelOrch::PhysicalChannelOrch(DBConnector *db, std::vector<TableConnector>& connectors)
    : OtaiObjectOrch(db, connectors, OTAI_OBJECT_TYPE_PHYSICALCHANNEL, g_physicalchannel_cfg_attrs)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OT_TRANSCEIVER_TABLE_NAME));
    m_countersTable = COUNTERS_OT_TRANSCEIVER_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OT_TRANSCEIVER_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OT_TRANSCEIVER_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OT_TRANSCEIVER_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OT_TRANSCEIVER_REPLY);

    m_createFunc = otai_physicalchannel_api->create_physicalchannel;
    m_removeFunc = otai_physicalchannel_api->remove_physicalchannel;
    m_setFunc = otai_physicalchannel_api->set_physicalchannel_attribute;
    m_getFunc = otai_physicalchannel_api->get_physicalchannel_attribute;
}

void PhysicalChannelOrch::setFlexCounter(otai_object_id_t id, vector<otai_attribute_t> &attrs)
{
    gFlexCounterOrch->getCounterGroup()->setCounterIdList(id, CounterType::PHYSICALCH_COUNTER, physicalch_counter_ids_counter);
    gFlexCounterOrch->getGaugeGroup()->setCounterIdList(id, CounterType::PHYSICALCH_GAUGE, physicalch_counter_ids_gauge);
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::PHYSICALCH_STATUS, physicalch_counter_ids_status);
}

void PhysicalChannelOrch::clearFlexCounter(otai_object_id_t id, string key)
{
    gFlexCounterOrch->getCounterGroup()->clearCounterIdList(id);
    gFlexCounterOrch->getGaugeGroup()->clearCounterIdList(id);
    gFlexCounterOrch->getStatusGroup()->clearCounterIdList(id);
}
