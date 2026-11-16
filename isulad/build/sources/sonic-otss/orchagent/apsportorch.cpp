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

#include "apsportorch.h"
#include "flexcounterorch.h"

using namespace std;
using namespace swss;

extern FlexCounterOrch *gFlexCounterOrch;
extern std::unordered_set<std::string> apsport_counter_ids_status;
extern std::unordered_set<std::string> apsport_counter_ids_gauge;

vector<otai_attr_id_t> g_apsport_cfg_attrs =
{
    OTAI_APSPORT_ATTR_ID,
    OTAI_APSPORT_ATTR_PORT_TYPE,
    OTAI_APSPORT_ATTR_ENABLED,
    OTAI_APSPORT_ATTR_TARGET_ATTENUATION,
    OTAI_APSPORT_ATTR_POWER_LOW_THRESHOLD,
};

ApsportOrch::ApsportOrch(DBConnector *db, const vector<string> &table_names)
    : OtaiObjectOrch(db, table_names, OTAI_OBJECT_TYPE_APSPORT, g_apsport_cfg_attrs)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OT_APSPORT_TABLE_NAME));
    m_countersTable = COUNTERS_OT_APSPORT_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OT_APSPORT_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OT_APSPORT_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OT_APSPORT_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OT_APSPORT_REPLY);

    m_createFunc = otai_apsport_api->create_apsport;
    m_removeFunc = otai_apsport_api->remove_apsport;
    m_setFunc = otai_apsport_api->set_apsport_attribute;
    m_getFunc = otai_apsport_api->get_apsport_attribute;
}

void ApsportOrch::setFlexCounter(otai_object_id_t id, vector<otai_attribute_t> &attrs)
{
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::APSPORT_STATUS, apsport_counter_ids_status);
    gFlexCounterOrch->getGaugeGroup()->setCounterIdList(id, CounterType::APSPORT_GAUGE, apsport_counter_ids_gauge);
}

