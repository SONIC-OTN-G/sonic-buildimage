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

#include "oaorch.h"
#include "flexcounterorch.h"

using namespace std;
using namespace swss;

extern FlexCounterOrch *gFlexCounterOrch;
extern std::unordered_set<std::string> oa_counter_ids_status;
extern std::unordered_set<std::string> oa_counter_ids_gauge;

vector<otai_attr_id_t> g_oa_cfg_attrs =
{
    OTAI_OA_ATTR_ID,
    OTAI_OA_ATTR_TARGET_GAIN,
    OTAI_OA_ATTR_TARGET_GAIN_TILT,
    OTAI_OA_ATTR_AMP_MODE,
    OTAI_OA_ATTR_TARGET_OUTPUT_POWER,
    OTAI_OA_ATTR_MAX_OUTPUT_POWER,
    OTAI_OA_ATTR_ENABLED,
    OTAI_OA_ATTR_FIBER_TYPE_PROFILE,
    OTAI_OA_ATTR_WORKING_STATE,
    OTAI_OA_ATTR_INPUT_LOS_THRESHOLD,
    OTAI_OA_ATTR_INPUT_LOS_HYSTERESIS,
    OTAI_OA_ATTR_OUTPUT_LOS_THRESHOLD,
    OTAI_OA_ATTR_OUTPUT_LOS_HYSTERESIS,
    OTAI_OA_ATTR_GAIN_LOW_THRESHOLD,
    OTAI_OA_ATTR_GAIN_LOW_HYSTERESIS,
    OTAI_OA_ATTR_INPUT_LOW_THRESHOLD,
    OTAI_OA_ATTR_OUTPUT_LOW_THRESHOLD,
    OTAI_OA_ATTR_LOS_ASE_DELAY,
    OTAI_OA_ATTR_APR_NODE_ENABLE,
    OTAI_OA_ATTR_APR_NODE_REFLECTION_THRESHOLD,
    OTAI_OA_ATTR_APR_LINE_ENABLE,
    OTAI_OA_ATTR_APR_LINE_VALID_LLDP,
};

vector<string> g_oa_auxiliary_fields =
{
    "name",
    "location",
    "component",
    "parent",
    "subcomponents",
};

OaOrch::OaOrch(DBConnector *db, const vector<string> &table_names)
    : OtaiObjectOrch(db, table_names, OTAI_OBJECT_TYPE_OA, g_oa_cfg_attrs, g_oa_auxiliary_fields)
{
    m_stateTable = unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OT_OA_TABLE_NAME));
    m_countersTable = COUNTERS_OT_OA_TABLE_NAME;
    m_nameMapTable = unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OT_OA_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OT_OA_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OT_OA_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OT_OA_REPLY);

    m_createFunc = otai_oa_api->create_oa;
    m_removeFunc = otai_oa_api->remove_oa;
    m_setFunc = otai_oa_api->set_oa_attribute;
    m_getFunc = otai_oa_api->get_oa_attribute;
}

void OaOrch::setFlexCounter(otai_object_id_t id, vector<otai_attribute_t> &attrs)
{
    gFlexCounterOrch->getStatusGroup()->setCounterIdList(id, CounterType::OA_STATUS, oa_counter_ids_status);
    gFlexCounterOrch->getGaugeGroup()->setCounterIdList(id, CounterType::OA_GAUGE, oa_counter_ids_gauge);
}

