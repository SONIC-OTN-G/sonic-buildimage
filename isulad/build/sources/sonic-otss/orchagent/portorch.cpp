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

#include <string>

#include "portorch.h"
#include "flexcounterorch.h"
#include "otai_serialize.h"
#include "notifier.h"
#include "notificationproducer.h"
#include "notifications.h"

using namespace std;

extern FlexCounterOrch* gFlexCounterOrch;
extern std::unordered_set<std::string> port_counter_ids_status;
extern std::unordered_set<std::string> port_counter_ids_gauge;
extern std::unordered_set<std::string> inport_counter_ids_gauge;
extern std::unordered_set<std::string> outport_counter_ids_gauge;

vector<otai_attr_id_t> g_port_cfg_attrs =
{
    OTAI_PORT_ATTR_PORT_TYPE,
    OTAI_PORT_ATTR_PORT_ID,
    OTAI_PORT_ATTR_ADMIN_STATE,
    OTAI_PORT_ATTR_RX_CD_RANGE,
    OTAI_PORT_ATTR_ROLL_OFF,
    /* For optical-lay card */
    OTAI_PORT_ATTR_LOS_THRESHOLD,
    OTAI_PORT_ATTR_LOW_THRESHOLD,
    OTAI_PORT_ATTR_HIGH_THRESHOLD,
    OTAI_PORT_ATTR_LED_MODE,
    OTAI_PORT_ATTR_LED_FLASH_INTERVAL,
};

vector<string> g_port_auxiliary_fields =
{
    "subcomponents",
    "location",
    "parent",
};

PortOrch::PortOrch(DBConnector* db, const vector<string>& table_names)
    : OtaiObjectOrch(db, table_names, OTAI_OBJECT_TYPE_PORT, g_port_cfg_attrs, g_port_auxiliary_fields)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OT_PORT_TABLE_NAME));
    m_countersTable = COUNTERS_OT_PORT_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OT_PORT_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OT_PORT_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OT_PORT_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OT_PORT_REPLY);

    m_createFunc = otai_port_api->create_port;
    m_removeFunc = otai_port_api->remove_port;
    m_setFunc = otai_port_api->set_port_attribute;
    m_getFunc = otai_port_api->get_port_attribute;
}

void PortOrch::setFlexCounter(otai_object_id_t id, vector<otai_attribute_t> &attrs)
{
    otai_port_type_t port_type = OTAI_PORT_TYPE_INVALID;

    for (auto attr : attrs)
    {
         if (attr.id == OTAI_PORT_ATTR_PORT_TYPE)
         {
             port_type = (otai_port_type_t)attr.value.s32;
         }
    }
    switch (port_type)
    {
    case OTAI_PORT_TYPE_LINE_IN:
    case OTAI_PORT_TYPE_CLIENT_IN:
    case OTAI_PORT_TYPE_EDFA_IN:
    case OTAI_PORT_TYPE_MD_IN:
    case OTAI_PORT_TYPE_MD_EXP_IN:
    case OTAI_PORT_TYPE_OLP_PRI_IN:
    case OTAI_PORT_TYPE_OLP_SEC_IN:
    case OTAI_PORT_TYPE_OLP_COM_IN:
        gFlexCounterOrch->getGaugeGroup()->setCounterIdList(id, CounterType::INPORT_GAUGE, inport_counter_ids_gauge);
        break;
    case OTAI_PORT_TYPE_LINE_OUT:
    case OTAI_PORT_TYPE_CLIENT_OUT:
    case OTAI_PORT_TYPE_EDFA_OUT:
    case OTAI_PORT_TYPE_MD_OUT:
    case OTAI_PORT_TYPE_MD_EXP_OUT:
    case OTAI_PORT_TYPE_OLP_PRI_OUT:
    case OTAI_PORT_TYPE_OLP_SEC_OUT:
    case OTAI_PORT_TYPE_OLP_COM_OUT:
        gFlexCounterOrch->getGaugeGroup()->setCounterIdList(id, CounterType::OUTPORT_GAUGE, outport_counter_ids_gauge);
        break;
    default:
        gFlexCounterOrch->getGaugeGroup()->setCounterIdList(id, CounterType::PORT_GAUGE, port_counter_ids_gauge);
        break;
    }

    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::PORT_STATUS, port_counter_ids_status);
}

