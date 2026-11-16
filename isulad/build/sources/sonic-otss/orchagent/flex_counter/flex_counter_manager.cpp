#include "flex_counter_manager.h"

#include <vector>

#include "schema.h"
#include "rediscommand.h"
#include "logger.h"
#include "otai_serialize.h"

using std::shared_ptr;
using std::string;
using std::unordered_map;
using std::unordered_set;
using std::vector;
using swss::DBConnector;
using swss::FieldValueTuple;
using swss::ProducerTable;
using swss::Table;

const string FLEX_COUNTER_ENABLE("enable");
const string FLEX_COUNTER_DISABLE("disable");

const unordered_map<StatsMode, string> FlexCounterManager::stats_mode_lookup =
{
    { StatsMode::STATS_MODE_COUNTER, "STATS_MODE_COUNTER" },
    { StatsMode::STATS_MODE_GAUGE, "STATS_MODE_GAUGE" },
    { StatsMode::STATS_MODE_STATUS, "STATS_MODE_STATUS" },
};

const unordered_map<bool, string> FlexCounterManager::status_lookup =
{
    { false, FLEX_COUNTER_DISABLE },
    { true,  FLEX_COUNTER_ENABLE }
};

const unordered_map<CounterType, string> FlexCounterManager::counter_id_field_lookup =
{
    { CounterType::LINECARD_STATUS,  OT_LINECARD_STATUS_ID_LIST },
    { CounterType::LINECARD_GAUGE,   OT_LINECARD_GAUGE_ID_LIST },
    { CounterType::LINECARD_COUNTER, OT_LINECARD_COUNTER_ID_LIST },

    { CounterType::PORT_STATUS,   OT_PORT_STATUS_ID_LIST },
    { CounterType::PORT_GAUGE,    OT_PORT_GAUGE_ID_LIST },
    { CounterType::INPORT_GAUGE,  OT_INPORT_GAUGE_ID_LIST },
    { CounterType::OUTPORT_GAUGE, OT_OUTPORT_GAUGE_ID_LIST },

    { CounterType::TRANSCEIVER_STATUS, OT_TRANSCEIVER_STATUS_ID_LIST },
    { CounterType::TRANSCEIVER_GAUGE,  OT_TRANSCEIVER_GAUGE_ID_LIST },
    { CounterType::TRANSCEIVER_COUNTER,OT_TRANSCEIVER_COUNTER_ID_LIST },

    { CounterType::LOGICALCH_STATUS,   OT_LOGICALCH_STATUS_ID_LIST },
    { CounterType::LOGICALCH_COUNTER,  OT_LOGICALCH_COUNTER_ID_LIST },

    { CounterType::OTN_STATUS,         OT_OTN_STATUS_ID_LIST },
    { CounterType::OTN_GAUGE,          OT_OTN_GAUGE_ID_LIST },
    { CounterType::OTN_COUNTER,        OT_OTN_COUNTER_ID_LIST },

    { CounterType::ETHERNET_STATUS,    OT_ETHERNET_STATUS_ID_LIST },
    { CounterType::ETHERNET_COUNTER,   OT_ETHERNET_COUNTER_ID_LIST },

    { CounterType::PHYSICALCH_STATUS,  OT_PHYSICALCH_STATUS_ID_LIST },
    { CounterType::PHYSICALCH_GAUGE,   OT_PHYSICALCH_GAUGE_ID_LIST },
    { CounterType::PHYSICALCH_COUNTER, OT_PHYSICALCH_COUNTER_ID_LIST },

    { CounterType::OPTICALCH_STATUS,  OT_OPTICALCH_STATUS_ID_LIST },
    { CounterType::OPTICALCH_GAUGE,   OT_OPTICALCH_GAUGE_ID_LIST },
    { CounterType::OPTICALCH_COUNTER, OT_OPTICALCH_COUNTER_ID_LIST },

    { CounterType::LLDP_STATUS, OT_LLDP_STATUS_ID_LIST },

    { CounterType::ASSIGNMENT_STATUS, OT_ASSIGNMENT_STATUS_ID_LIST },

    { CounterType::INTERFACE_STATUS,  OT_INTERFACE_STATUS_ID_LIST },

    { CounterType::INTERFACE_COUNTER, OT_INTERFACE_COUNTER_ID_LIST },

    { CounterType::OA_STATUS, OT_OA_STATUS_ID_LIST },
    { CounterType::OA_GAUGE, OT_OA_GAUGE_ID_LIST },

    { CounterType::OSC_STATUS, OT_OSC_STATUS_ID_LIST },
    { CounterType::OSC_GAUGE, OT_OSC_GAUGE_ID_LIST },

    { CounterType::APS_STATUS, OT_APS_STATUS_ID_LIST },

    { CounterType::APSPORT_STATUS, OT_APSPORT_STATUS_ID_LIST },
    { CounterType::APSPORT_GAUGE, OT_APSPORT_GAUGE_ID_LIST },

    { CounterType::ATTENUATOR_STATUS, OT_ATTENUATOR_STATUS_ID_LIST },
    { CounterType::ATTENUATOR_GAUGE, OT_ATTENUATOR_GAUGE_ID_LIST },

    { CounterType::OCM_STATUS, OT_OCM_STATUS_ID_LIST },
    { CounterType::OTDR_STATUS, OT_OTDR_STATUS_ID_LIST },
};

FlexCounterManager::FlexCounterManager(
    const string& group_name,
    const StatsMode stats_mode,
    const uint polling_interval,
    const bool enabled) :
    group_name(group_name),
    stats_mode(stats_mode),
    polling_interval(polling_interval),
    enabled(enabled),
    flex_counter_db(new DBConnector("FLEX_COUNTER_DB", 0)),
    flex_counter_group_table(new ProducerTable(flex_counter_db.get(), FLEX_COUNTER_GROUP_TABLE)),
    flex_counter_table(new ProducerTable(flex_counter_db.get(), FLEX_COUNTER_TABLE))
{
    SWSS_LOG_ENTER();

    Table table(flex_counter_db.get(), FLEX_COUNTER_TABLE);
    vector<string> keys;
    table.getKeys(keys);
    for (auto &k: keys)
    {
        if (k.find(group_name) == std::string::npos)
        {
            continue;
        }        
        flex_counter_table->del(k);
    }

    keys.clear();
    Table table2(flex_counter_db.get(), FLEX_COUNTER_GROUP_TABLE);
    table2.getKeys(keys);
    for (auto &k: keys)
    {
        if (k == group_name)
        {
            flex_counter_group_table->del(k);
            break;
        }
    }

    applyGroupConfiguration();

    SWSS_LOG_DEBUG("Initialized flex counter group '%s'.", group_name.c_str());
}

FlexCounterManager::~FlexCounterManager()
{
    SWSS_LOG_ENTER();

    for (const auto& counter : installed_counters)
    {
        flex_counter_table->del(getFlexCounterTableKey(group_name, counter));
    }

    flex_counter_group_table->del(group_name);

    SWSS_LOG_DEBUG("Deleted flex counter group '%s'.", group_name.c_str());
}

void FlexCounterManager::applyGroupConfiguration()
{
    SWSS_LOG_ENTER();

    vector<FieldValueTuple> field_values =
    {
        FieldValueTuple(STATS_MODE_FIELD, stats_mode_lookup.at(stats_mode)),
        FieldValueTuple(POLL_INTERVAL_FIELD, std::to_string(polling_interval)),
        FieldValueTuple(FLEX_COUNTER_STATUS_FIELD, status_lookup.at(enabled))
    };

    flex_counter_group_table->set(group_name, field_values);
}

void FlexCounterManager::updateGroupPollingInterval(
    const uint polling_interval)
{
    SWSS_LOG_ENTER();

    vector<FieldValueTuple> field_values =
    {
        FieldValueTuple(POLL_INTERVAL_FIELD, std::to_string(polling_interval))
    };
    flex_counter_group_table->set(group_name, field_values);

    SWSS_LOG_DEBUG("Set polling interval for flex counter group '%s' to %d ms.",
        group_name.c_str(), polling_interval);
}

// enableFlexCounterGroup will do nothing if the flex counter group is already
// enabled.
void FlexCounterManager::enableFlexCounterGroup()
{
    SWSS_LOG_ENTER();

    if (enabled)
    {
        return;
    }

    vector<FieldValueTuple> field_values =
    {
        FieldValueTuple(FLEX_COUNTER_STATUS_FIELD, FLEX_COUNTER_ENABLE)
    };
    flex_counter_group_table->set(group_name, field_values);
    enabled = true;

    SWSS_LOG_DEBUG("Enabling flex counters for group '%s'.",
        group_name.c_str());
}

// disableFlexCounterGroup will do nothing if the flex counter group has been
// disabled.
void FlexCounterManager::disableFlexCounterGroup()
{
    SWSS_LOG_ENTER();

    if (!enabled)
    {
        return;
    }

    vector<FieldValueTuple> field_values =
    {
        FieldValueTuple(FLEX_COUNTER_STATUS_FIELD, FLEX_COUNTER_DISABLE)
    };
    flex_counter_group_table->set(group_name, field_values);
    enabled = false;

    SWSS_LOG_DEBUG("Disabling flex counters for group '%s'.",
        group_name.c_str());
}

// setCounterIdList configures a flex counter to poll the set of provided stats
// that are associated with the given object.
void FlexCounterManager::setCounterIdList(
    const otai_object_id_t object_id,
    const CounterType counter_type,
    const unordered_set<string>& counter_stats)
{
    SWSS_LOG_ENTER();

    auto counter_type_it = counter_id_field_lookup.find(counter_type);
    if (counter_type_it == counter_id_field_lookup.end())
    {
        SWSS_LOG_ERROR("Could not update flex counter id list for group '%s': counter type not found.",
            group_name.c_str());
        return;
    }

    std::vector<swss::FieldValueTuple> field_values =
    {
        FieldValueTuple(counter_type_it->second, serializeCounterStats(counter_stats))
    };
    flex_counter_table->set(getFlexCounterTableKey(group_name, object_id), field_values);
    installed_counters.insert(object_id);

    SWSS_LOG_DEBUG("Updated flex counter id list for object '%" PRIu64 "' in group '%s'.",
        object_id,
        group_name.c_str());
}

// clearCounterIdList clears all stats that are currently being polled from
// the given object.
void FlexCounterManager::clearCounterIdList(const otai_object_id_t object_id)
{
    SWSS_LOG_ENTER();

    auto counter_it = installed_counters.find(object_id);
    if (counter_it == installed_counters.end())
    {
        SWSS_LOG_WARN("No counters found on object '%" PRIu64 "' in group '%s'.",
            object_id,
            group_name.c_str());
        return;
    }

    flex_counter_table->del(getFlexCounterTableKey(group_name, object_id));
    installed_counters.erase(counter_it);

    SWSS_LOG_DEBUG("Cleared flex counter id list for object '%" PRIu64 "' in group '%s'.",
        object_id,
        group_name.c_str());
}

string FlexCounterManager::getFlexCounterTableKey(
    const string& group_name,
    const otai_object_id_t object_id) const
{
    SWSS_LOG_ENTER();

    return group_name + flex_counter_table->getTableNameSeparator() + otai_serialize_object_id(object_id);
}

// serializeCounterStats turns a set of stats into a format suitable for FLEX_COUNTER_DB.
string FlexCounterManager::serializeCounterStats(
    const unordered_set<string>& counter_stats) const
{
    SWSS_LOG_ENTER();

    string stats_string;
    for (const auto& stat : counter_stats)
    {
        stats_string.append(stat);
        stats_string.append(",");
    }

    if (!stats_string.empty())
    {
        // Fence post: remove the trailing comma
        stats_string.pop_back();
    }

    return stats_string;
}




