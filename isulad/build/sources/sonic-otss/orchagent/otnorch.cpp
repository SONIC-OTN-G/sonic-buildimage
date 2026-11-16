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

#include "otnorch.h"
#include "flexcounterorch.h"

using namespace std;

extern FlexCounterOrch *gFlexCounterOrch;
extern std::unordered_set<std::string> otn_counter_ids_counter;
extern std::unordered_set<std::string> otn_counter_ids_gauge;
extern std::unordered_set<std::string> otn_counter_ids_status;

vector<otai_attr_id_t> g_otn_cfg_attrs =
{
    OTAI_OTN_ATTR_CHANNEL_ID,
    OTAI_OTN_ATTR_TTI_MSG_EXPECTED,
    OTAI_OTN_ATTR_TTI_MSG_TRANSMIT,
    OTAI_OTN_ATTR_TTI_MSG_AUTO,
    OTAI_OTN_ATTR_DELAY_MEASUREMENT_ENABLED,
    OTAI_OTN_ATTR_DELAY_MEASUREMENT_MODE,
    OTAI_OTN_ATTR_MAINTENANCE,
};

vector<string> g_otn_auxiliary_fields =
{
    "index",
};

OtnOrch::OtnOrch(DBConnector *db, std::vector<TableConnector>& connectors)
    : OtaiObjectOrch(db, connectors, OTAI_OBJECT_TYPE_OTN, g_otn_cfg_attrs, g_otn_auxiliary_fields)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OT_OTN_TABLE_NAME));
    m_countersTable = COUNTERS_OT_OTN_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OT_OTN_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OT_OTN_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OT_OTN_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OT_OTN_REPLY);

    m_createFunc = otai_otn_api->create_otn;
    m_removeFunc = otai_otn_api->remove_otn;
    m_setFunc = otai_otn_api->set_otn_attribute;
    m_getFunc = otai_otn_api->get_otn_attribute;
}

void OtnOrch::setFlexCounter(otai_object_id_t id, vector<otai_attribute_t> &attrs)
{            
    gFlexCounterOrch->getCounterGroup()->setCounterIdList(id, CounterType::OTN_COUNTER, otn_counter_ids_counter);
    gFlexCounterOrch->getGaugeGroup()->setCounterIdList(id, CounterType::OTN_GAUGE, otn_counter_ids_gauge);
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::OTN_STATUS, otn_counter_ids_status);
}

void OtnOrch::clearFlexCounter(otai_object_id_t id, string key)
{
    gFlexCounterOrch->getCounterGroup()->clearCounterIdList(id);
    gFlexCounterOrch->getGaugeGroup()->clearCounterIdList(id);
    gFlexCounterOrch->getStatusGroup()->clearCounterIdList(id);
}

