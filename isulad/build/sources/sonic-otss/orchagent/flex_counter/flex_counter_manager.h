#ifndef ORCHAGENT_FLEX_COUNTER_MANAGER_H
#define ORCHAGENT_FLEX_COUNTER_MANAGER_H

#include <string>
#include <unordered_set>
#include <unordered_map>
#include "dbconnector.h"
#include "producertable.h"
#include <inttypes.h>

extern "C" {
#include "otai.h"
}

enum class StatsMode
{
    STATS_MODE_COUNTER,
    STATS_MODE_GAUGE,
    STATS_MODE_STATUS
};

enum class CounterType
{
    LINECARD_STATUS,
    LINECARD_GAUGE,
    LINECARD_COUNTER,

    PORT_STATUS,
    PORT_GAUGE,
    INPORT_GAUGE,
    OUTPORT_GAUGE,

    TRANSCEIVER_STATUS,
    TRANSCEIVER_GAUGE,
    TRANSCEIVER_COUNTER,

    LOGICALCH_COUNTER,
    LOGICALCH_STATUS,

    OTN_STATUS,
    OTN_GAUGE,
    OTN_COUNTER,

    ETHERNET_STATUS,
    ETHERNET_COUNTER,

    PHYSICALCH_STATUS,
    PHYSICALCH_GAUGE,
    PHYSICALCH_COUNTER,

    OPTICALCH_STATUS,
    OPTICALCH_GAUGE,
    OPTICALCH_COUNTER,

    LLDP_STATUS,

    ASSIGNMENT_STATUS,

    INTERFACE_STATUS,
    INTERFACE_GAUGE,
    INTERFACE_COUNTER,

    OA_STATUS,
    OA_GAUGE,

    OSC_STATUS,
    OSC_GAUGE,

    APS_STATUS,

    APSPORT_STATUS,
    APSPORT_GAUGE,

    ATTENUATOR_STATUS,
    ATTENUATOR_GAUGE,

    OCM_STATUS,
    OTDR_STATUS,
};

// FlexCounterManager allows users to manage a group of flex counters.
//
// TODO: FlexCounterManager doesn't currently support the full range of
// flex counter features. In particular, support for standard (i.e. non-debug)
// counters and support for plugins needs to be added.
class FlexCounterManager
{
public:
    FlexCounterManager(
        const std::string& group_name,
        const StatsMode stats_mode,
        const uint polling_interval,
        const bool enabled);

    FlexCounterManager(const FlexCounterManager&) = delete;
    FlexCounterManager& operator=(const FlexCounterManager&) = delete;
    virtual ~FlexCounterManager();

    void updateGroupPollingInterval(const uint polling_interval);
    void enableFlexCounterGroup();
    void disableFlexCounterGroup();

    void setCounterIdList(
        const otai_object_id_t object_id,
        const CounterType counter_type,
        const std::unordered_set<std::string>& counter_stats);
    void clearCounterIdList(const otai_object_id_t object_id);

protected:
    void applyGroupConfiguration();

private:
    std::string getFlexCounterTableKey(
        const std::string& group_name,
        const otai_object_id_t object_id) const;
    std::string serializeCounterStats(
        const std::unordered_set<std::string>& counter_stats) const;

    std::string group_name;
    StatsMode stats_mode;
    uint polling_interval;
    bool enabled;
    std::unordered_set<otai_object_id_t> installed_counters;

    std::shared_ptr<swss::DBConnector> flex_counter_db = nullptr;
    std::shared_ptr<swss::ProducerTable> flex_counter_group_table = nullptr;
    std::shared_ptr<swss::ProducerTable> flex_counter_table = nullptr;

    static const std::unordered_map<StatsMode, std::string> stats_mode_lookup;
    static const std::unordered_map<bool, std::string> status_lookup;
    static const std::unordered_map<CounterType, std::string> counter_id_field_lookup;
};

#endif // ORCHAGENT_FLEX_COUNTER_MANAGER_H
