extern "C" {

#include "otai.h"
#include "otaistatus.h"
}

#include <inttypes.h>
#include <string.h>
#include <fstream>
#include <map>
#include <logger.h>
#include <otairedis.h>
#include <set>
#include <tuple>
#include <vector>
#include <linux/limits.h>
#include <net/if.h>
#include "timestamp.h"
#include "otai_serialize.h"
#include "otaihelper.h"
#include "orch.h"

using namespace std;
using namespace swss;

#define _STR(s) #s
#define STR(s) _STR(s)

#define CONTEXT_CFG_FILE "/usr/share/sonic/hwsku/context_config.json"
#define OTAI_REDIS_SYNC_OPERATION_RESPONSE_TIMEOUT (480*1000)

// hwinfo = "INTERFACE_NAME/PHY ID", mii_ioctl_data->phy_id is a __u16
#define HWINFO_MAX_SIZE IFNAMSIZ + 1 + 5

/* Initialize all global api pointers */
otai_linecard_api_t*              otai_linecard_api;
otai_port_api_t*                  otai_port_api;
otai_transceiver_api_t*           otai_transceiver_api;
otai_physicalchannel_api_t*       otai_physicalchannel_api;
otai_otn_api_t*                   otai_otn_api;
otai_och_api_t*                   otai_och_api;
otai_logicalchannel_api_t*        otai_logicalchannel_api;
otai_lldp_api_t*                  otai_lldp_api;
otai_assignment_api_t*            otai_assignment_api;
otai_ethernet_api_t*              otai_ethernet_api;
otai_interface_api_t*             otai_interface_api;
otai_oa_api_t*                    otai_oa_api;
otai_osc_api_t*                   otai_osc_api;
otai_aps_api_t*                   otai_aps_api;
otai_apsport_api_t*               otai_apsport_api;
otai_attenuator_api_t*            otai_attenuator_api;
otai_ocm_api_t*                   otai_ocm_api;
otai_otdr_api_t*                  otai_otdr_api;

extern otai_object_id_t gLinecardId;

map<string, string> gProfileMap;

const char *test_profile_get_value (
    _In_ otai_linecard_profile_id_t profile_id,
    _In_ const char *variable)
{
    SWSS_LOG_ENTER();

    auto it = gProfileMap.find(variable);

    if (it == gProfileMap.end())
        return NULL;
    return it->second.c_str();
}

int test_profile_get_next_value (
    _In_ otai_linecard_profile_id_t profile_id,
    _Out_ const char **variable,
    _Out_ const char **value)
{
    SWSS_LOG_ENTER();

    static auto it = gProfileMap.begin();

    if (value == NULL)
    {
        // Restarts enumeration
        it = gProfileMap.begin();
    }
    else if (it == gProfileMap.end())
    {
        return -1;
    }
    else
    {
        *variable = it->first.c_str();
        *value = it->second.c_str();
        it++;
    }

    if (it != gProfileMap.end())
        return 0;
    else
        return -1;
}

const otai_service_method_table_t test_services = {
    test_profile_get_value,
    test_profile_get_next_value
};

void initOtaiApi()
{
    SWSS_LOG_ENTER();

    if (ifstream(CONTEXT_CFG_FILE))
    {
        SWSS_LOG_NOTICE("Context config file %s exists", CONTEXT_CFG_FILE);
        gProfileMap[OTAI_REDIS_KEY_CONTEXT_CONFIG] = CONTEXT_CFG_FILE;
    }

    otai_api_initialize(0, (const otai_service_method_table_t *)&test_services);

    otai_api_query(OTAI_API_LINECARD,               (void **)&otai_linecard_api);
    otai_api_query(OTAI_API_PORT,                   (void **)&otai_port_api);
    otai_api_query(OTAI_API_TRANSCEIVER,            (void **)&otai_transceiver_api);
    otai_api_query(OTAI_API_PHYSICALCHANNEL,        (void **)&otai_physicalchannel_api);
    otai_api_query(OTAI_API_OTN,                    (void **)&otai_otn_api);
    otai_api_query(OTAI_API_OCH,                    (void **)&otai_och_api);
    otai_api_query(OTAI_API_LOGICALCHANNEL,         (void **)&otai_logicalchannel_api);
    otai_api_query(OTAI_API_LLDP,                   (void **)&otai_lldp_api);
    otai_api_query(OTAI_API_ASSIGNMENT,             (void **)&otai_assignment_api);
    otai_api_query(OTAI_API_ETHERNET,               (void **)&otai_ethernet_api);
    otai_api_query(OTAI_API_INTERFACE,              (void **)&otai_interface_api);
    otai_api_query(OTAI_API_OA,                     (void **)&otai_oa_api);
    otai_api_query(OTAI_API_OSC,                    (void **)&otai_osc_api);
    otai_api_query(OTAI_API_APS,                    (void **)&otai_aps_api);
    otai_api_query(OTAI_API_APSPORT,                (void **)&otai_apsport_api);
    otai_api_query(OTAI_API_ATTENUATOR,             (void **)&otai_attenuator_api);
    otai_api_query(OTAI_API_OCM,                    (void **)&otai_ocm_api);
    otai_api_query(OTAI_API_OTDR,                   (void **)&otai_otdr_api);

    otai_log_set(OTAI_API_LINECARD,                 OTAI_LOG_LEVEL_WARN);
    otai_log_set(OTAI_API_PORT,                     OTAI_LOG_LEVEL_WARN);
    otai_log_set(OTAI_API_TRANSCEIVER,              OTAI_LOG_LEVEL_WARN);
    otai_log_set(OTAI_API_PHYSICALCHANNEL,          OTAI_LOG_LEVEL_WARN);
    otai_log_set(OTAI_API_OTN,                      OTAI_LOG_LEVEL_WARN);
    otai_log_set(OTAI_API_OCH,                      OTAI_LOG_LEVEL_WARN);
    otai_log_set(OTAI_API_LOGICALCHANNEL,           OTAI_LOG_LEVEL_WARN);
    otai_log_set(OTAI_API_LLDP,                     OTAI_LOG_LEVEL_CRITICAL);
    otai_log_set(OTAI_API_ASSIGNMENT,               OTAI_LOG_LEVEL_WARN);
    otai_log_set(OTAI_API_ETHERNET,                 OTAI_LOG_LEVEL_WARN);
    otai_log_set(OTAI_API_INTERFACE,                OTAI_LOG_LEVEL_WARN);
    otai_log_set(OTAI_API_OA,                       OTAI_LOG_LEVEL_WARN);
    otai_log_set(OTAI_API_OSC,                      OTAI_LOG_LEVEL_WARN);
    otai_log_set(OTAI_API_APS,                      OTAI_LOG_LEVEL_WARN);
    otai_log_set(OTAI_API_APSPORT,                  OTAI_LOG_LEVEL_WARN);
    otai_log_set(OTAI_API_ATTENUATOR,               OTAI_LOG_LEVEL_WARN);
    otai_log_set(OTAI_API_OCM,                      OTAI_LOG_LEVEL_WARN);
    otai_log_set(OTAI_API_OTDR,                     OTAI_LOG_LEVEL_WARN);
}

