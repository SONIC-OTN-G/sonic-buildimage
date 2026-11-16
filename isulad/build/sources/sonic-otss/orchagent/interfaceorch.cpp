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

#include "interfaceorch.h"
#include "flexcounterorch.h"

using namespace std;
using namespace swss;

extern FlexCounterOrch* gFlexCounterOrch;
extern std::unordered_set<std::string> interface_counter_ids_status;
extern std::unordered_set<std::string> interface_counter_ids_counter;

vector<otai_attr_id_t> g_interface_cfg_attrs =
{
    OTAI_INTERFACE_ATTR_INTERFACE_TYPE,
    OTAI_INTERFACE_ATTR_INTERFACE_ID,
};

vector<string> g_interface_auxiliary_fields =
{
    "type",
    "name",
    "transceiver",
    "hardware-port",
};

InterfaceOrch::InterfaceOrch(DBConnector *db, std::vector<TableConnector>& connectors)
    : OtaiObjectOrch(db, connectors, OTAI_OBJECT_TYPE_INTERFACE, g_interface_cfg_attrs, g_interface_auxiliary_fields)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OT_INTERFACE_TABLE_NAME));
    m_countersTable = COUNTERS_OT_INTERFACE_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OT_INTERFACE_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OT_INTERFACE_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OT_INTERFACE_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OT_INTERFACE_REPLY);

    m_createFunc = otai_interface_api->create_interface;
    m_removeFunc = otai_interface_api->remove_interface;
    m_setFunc = otai_interface_api->set_interface_attribute;
    m_getFunc = otai_interface_api->get_interface_attribute;
}

void InterfaceOrch::setFlexCounter(otai_object_id_t id, vector<otai_attribute_t> &attrs)
{
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::INTERFACE_STATUS, interface_counter_ids_status);
    gFlexCounterOrch->getCounterGroup()->setCounterIdList(id, CounterType::INTERFACE_COUNTER, interface_counter_ids_counter);
}

void InterfaceOrch::clearFlexCounter(otai_object_id_t id, string key)
{
    gFlexCounterOrch->getCounterGroup()->clearCounterIdList(id);
    gFlexCounterOrch->getStatusGroup()->clearCounterIdList(id);
}
