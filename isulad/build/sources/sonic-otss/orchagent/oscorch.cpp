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

#include "oscorch.h"
#include "flexcounterorch.h"

using namespace std;
using namespace swss;

extern FlexCounterOrch *gFlexCounterOrch;
extern std::unordered_set<std::string> osc_counter_ids_status;
extern std::unordered_set<std::string> osc_counter_ids_gauge;

vector<otai_attr_id_t> g_osc_cfg_attrs =
{
    OTAI_OSC_ATTR_ID,
    OTAI_OSC_ATTR_ENABLED,
    OTAI_OSC_ATTR_RX_LOW_THRESHOLD,
    OTAI_OSC_ATTR_RX_HIGH_THRESHOLD,
    OTAI_OSC_ATTR_TX_LOW_THRESHOLD,
};

vector<string> g_osc_auxiliary_fields =
{
    "name",
    "location",
    "interface",
    "parent",
};

OscOrch::OscOrch(DBConnector *db, const vector<string> &table_names)
    : OtaiObjectOrch(db, table_names, OTAI_OBJECT_TYPE_OSC, g_osc_cfg_attrs, g_osc_auxiliary_fields)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OT_OSC_TABLE_NAME));
    m_countersTable = COUNTERS_OT_OSC_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OT_OSC_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OT_OSC_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OT_OSC_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OT_OSC_REPLY);

    m_createFunc = otai_osc_api->create_osc;
    m_removeFunc = otai_osc_api->remove_osc;
    m_setFunc = otai_osc_api->set_osc_attribute;
    m_getFunc = otai_osc_api->get_osc_attribute;
}

void OscOrch::setFlexCounter(otai_object_id_t id, vector<otai_attribute_t> &attrs)
{
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::OSC_STATUS, osc_counter_ids_status);
    gFlexCounterOrch->getGaugeGroup()->setCounterIdList(id, CounterType::OSC_GAUGE, osc_counter_ids_gauge);
}

