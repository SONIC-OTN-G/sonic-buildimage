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

#include "apsorch.h"
#include "flexcounterorch.h"
#include "notifications.h"

using namespace std;
using namespace swss;

extern FlexCounterOrch *gFlexCounterOrch;
extern std::unordered_set<std::string> aps_counter_ids_status;

vector<otai_attr_id_t> g_aps_cfg_attrs =
{
    OTAI_APS_ATTR_ID,
    OTAI_APS_ATTR_TYPE,
    OTAI_APS_ATTR_REVERTIVE,
    OTAI_APS_ATTR_WAIT_TO_RESTORE_TIME,
    OTAI_APS_ATTR_HOLD_OFF_TIME,
    OTAI_APS_ATTR_PRIMARY_SWITCH_THRESHOLD,
    OTAI_APS_ATTR_PRIMARY_SWITCH_HYSTERESIS,
    OTAI_APS_ATTR_SECONDARY_SWITCH_THRESHOLD,
    OTAI_APS_ATTR_RELATIVE_SWITCH_THRESHOLD,
    OTAI_APS_ATTR_RELATIVE_SWITCH_THRESHOLD_OFFSET,
    OTAI_APS_ATTR_FORCE_TO_PORT,
    OTAI_APS_ATTR_ALARM_HYSTERESIS,
    OTAI_APS_ATTR_COLLECT_SWITCH_INFO,
    OTAI_APS_ATTR_ACTIVE_PATH,
};

vector<string> g_aps_auxiliary_fields =
{
    "name",
    "location",
    "parent",
    "subcomponents",
};

ApsOrch::ApsOrch(DBConnector *db, const vector<string> &table_names)
    : OtaiObjectOrch(db, table_names, OTAI_OBJECT_TYPE_APS, g_aps_cfg_attrs, g_aps_auxiliary_fields)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OT_APS_TABLE_NAME));
    m_countersTable = COUNTERS_OT_APS_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OT_APS_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OT_APS_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OT_APS_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OT_APS_REPLY);

    m_createFunc = otai_aps_api->create_aps;
    m_removeFunc = otai_aps_api->remove_aps;
    m_setFunc = otai_aps_api->set_aps_attribute;
    m_getFunc = otai_aps_api->get_aps_attribute;
}

void ApsOrch::addExtraAttrsOnCreate(vector<otai_attribute_t> &attrs)
{
    SWSS_LOG_ENTER();

    otai_attribute_t attr;

    attr.id = OTAI_APS_ATTR_SWITCH_INFO_NOTIFY;
    attr.value.ptr = (void*)onApsSwitchInfoNotify;
    attrs.push_back(attr);

    attr.id = OTAI_APS_ATTR_COLLECT_SWITCH_INFO;
    attr.value.booldata = true;
    attrs.push_back(attr);
}

void ApsOrch::setFlexCounter(otai_object_id_t id, vector<otai_attribute_t> &attrs)
{
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::APS_STATUS, aps_counter_ids_status);
}

