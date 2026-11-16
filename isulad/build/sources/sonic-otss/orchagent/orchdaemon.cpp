#include <unistd.h>
#include <unordered_map>
#include <limits.h>
#include "orchdaemon.h"
#include "logger.h"
#include <otairedis.h>

using namespace std;
using namespace swss;

/* select() function timeout retry time */
#define SELECT_TIMEOUT 1000

extern otai_linecard_api_t* otai_linecard_api;
extern otai_object_id_t             gLinecardId;

/*
 * Global orch daemon variables
 */
PortOrch* gPortOrch;
TransceiverOrch* gTransceiverOrch;
LogicalChannelOrch* gLogicalChannelOrch;
PhysicalChannelOrch* gPhysicalChannelOrch;
OtnOrch* gOtnOrch;
OchOrch* gOchOrch;
EthernetOrch* gEthernetOrch;
LldpOrch* gLldpOrch;
AssignmentOrch* gAssignmentOrch;
InterfaceOrch *gInterfaceOrch;
OaOrch *gOaOrch;
OscOrch *gOscOrch;
ApsOrch *gApsOrch;
ApsportOrch *gApsportOrch;
AttenuatorOrch *gAttenuatorOrch;
OcmOrch *gOcmOrch;
OtdrOrch *gOtdrOrch;
LinecardOrch* gLinecardOrch;
Directory<Orch*> gDirectory;
FlexCounterOrch* gFlexCounterOrch;
DiagOrch* gDiagOrch;

OrchDaemon::OrchDaemon(DBConnector* applDb, DBConnector* configDb, DBConnector* stateDb) :
    m_applDb(applDb),
    m_configDb(configDb),
    m_stateDb(stateDb)
{
    SWSS_LOG_ENTER();
}

OrchDaemon::~OrchDaemon()
{
    SWSS_LOG_ENTER();

    /*
     * Some orchagents call other agents in their destructor.
     * To avoid accessing deleted agent, do deletion in reverse order.
     * NOTE: This is still not a robust solution, as order in this list
     *       does not strictly match the order of construction of agents.
     * For a robust solution, first some cleaning/house-keeping in
     * orchagents management is in order.
     * For now it fixes, possible crash during process exit.
     */
    auto it = m_orchList.rbegin();
    for (; it != m_orchList.rend(); ++it) {
        delete(*it);
    }
}

bool OrchDaemon::init()
{
    SWSS_LOG_ENTER();

    TableConnector app_linecard_table(m_applDb, APP_OT_LINECARD_TABLE_NAME);
    TableConnector state_linecard_table(m_stateDb, STATE_OT_LINECARD_TABLE_NAME);

    vector<TableConnector> linecard_tables = {
        app_linecard_table,
        state_linecard_table,
    };
    gLinecardOrch = new LinecardOrch(m_applDb, linecard_tables);

    const vector<string> port_tables = {
        APP_OT_PORT_TABLE_NAME,
    };
    gPortOrch = new PortOrch(m_applDb, port_tables);

    TableConnector app_transceiver_table(m_applDb, APP_OT_TRANSCEIVER_TABLE_NAME);
    TableConnector state_transceiver_table(m_stateDb, STATE_OT_TRANSCEIVER_TABLE_NAME);
    vector<TableConnector> transceiver_tables = {
        app_transceiver_table,
        state_transceiver_table,
    };
    gTransceiverOrch = new TransceiverOrch(m_applDb, transceiver_tables);

    TableConnector app_otn_table(m_applDb, APP_OT_OTN_TABLE_NAME);
    TableConnector state_otn_table(m_stateDb, STATE_OT_OTN_TABLE_NAME);
    vector<TableConnector> otn_tables = {
        app_otn_table,
        state_otn_table,
    };
    gOtnOrch = new OtnOrch(m_applDb, otn_tables);

    TableConnector app_ethernet_table(m_applDb, APP_OT_ETHERNET_TABLE_NAME);
    TableConnector state_ethernet_table(m_stateDb, STATE_OT_ETHERNET_TABLE_NAME);
    vector<TableConnector> ethernet_tables = {
        app_ethernet_table,
        state_ethernet_table,
    };
    gEthernetOrch = new EthernetOrch(m_applDb, ethernet_tables);

    TableConnector app_och_table(m_applDb, APP_OT_OCH_TABLE_NAME);
    TableConnector state_och_table(m_stateDb, STATE_OT_OCH_TABLE_NAME);
    vector<TableConnector> och_tables = {
        app_och_table,
        state_och_table,
    };
    gOchOrch = new OchOrch(m_applDb, och_tables);

    TableConnector app_logical_channel_table(m_applDb, APP_OT_LOGICALCHANNEL_TABLE_NAME);
    TableConnector state_logical_channel_table(m_stateDb, STATE_OT_LOGICALCHANNEL_TABLE_NAME);
    vector<TableConnector> logical_channel_tables = {
        app_logical_channel_table,
        state_logical_channel_table,
    };
    gLogicalChannelOrch = new LogicalChannelOrch(m_applDb, logical_channel_tables);

    TableConnector app_physical_channel_table(m_applDb, APP_OT_PHYSICALCHANNEL_TABLE_NAME);
    TableConnector state_physical_channel_table(m_stateDb, STATE_OT_PHYSICALCHANNEL_TABLE_NAME);
    vector<TableConnector> physical_channel_tables = {
        app_physical_channel_table,
        state_physical_channel_table,
    };
    gPhysicalChannelOrch = new PhysicalChannelOrch(m_applDb, physical_channel_tables);

    TableConnector app_interface_table(m_applDb, APP_OT_INTERFACE_TABLE_NAME);
    TableConnector state_interface_table(m_stateDb, STATE_OT_INTERFACE_TABLE_NAME);
    vector<TableConnector> interface_tables = {
        app_interface_table,
        state_interface_table,
    };
    gInterfaceOrch = new InterfaceOrch(m_applDb, interface_tables);

    const vector<string> assignment_tables = {
        APP_OT_ASSIGNMENT_TABLE_NAME,
    };
    gAssignmentOrch = new AssignmentOrch(m_applDb, assignment_tables);

    const vector<string> oa_tables = {
        APP_OT_OA_TABLE_NAME,
    };
    gOaOrch = new OaOrch(m_applDb, oa_tables);

    const vector<string> osc_tables = {
        APP_OT_OSC_TABLE_NAME,
    };
    gOscOrch = new OscOrch(m_applDb, osc_tables);

    const vector<string> aps_tables = {
        APP_OT_APS_TABLE_NAME,
    };
    gApsOrch = new ApsOrch(m_applDb, aps_tables);

    const vector<string> apsport_tables = {
        APP_OT_APSPORT_TABLE_NAME,
    };
    gApsportOrch = new ApsportOrch(m_applDb, apsport_tables);

    const vector<string> attenuator_tables = {
        APP_OT_ATTENUATOR_TABLE_NAME,
    };
    gAttenuatorOrch = new AttenuatorOrch(m_applDb, attenuator_tables);

    const vector<string> ocm_tables = {
        APP_OT_OCM_TABLE_NAME,
    };
    gOcmOrch = new OcmOrch(m_applDb, ocm_tables);

    const vector<string> otdr_tables = {
        APP_OT_OTDR_TABLE_NAME,
    };
    gOtdrOrch = new OtdrOrch(m_applDb, otdr_tables);

    const vector<string> lldp_tables = {
        APP_OT_LLDP_TABLE_NAME,
    };
    gLldpOrch = new LldpOrch(m_applDb, lldp_tables);

    const vector<string> diag_tables = {
        "SWSS_DIAG",
    };
    gDiagOrch = new DiagOrch(m_applDb, diag_tables);

    /*
     * The order of the orch list is important for state restore of warm start and
     * the queued processing in m_toSync map after gPortsOrch->allPortsReady() is set.
     *
     * For the multiple consumers in Orchs, tasks in a table which name is smaller in lexicographic order are processed first
     * when iterating ConsumerMap. This is ensured implicitly by the order of keys in ordered map.
     * For cases when Orch has to process tables in specific order, like PortsOrch during warm start, it has to override Orch::doTask()
     */
    m_orchList = { gLinecardOrch,
                   gPortOrch,
                   gTransceiverOrch,
                   gOtnOrch,
                   gLogicalChannelOrch,
                   gPhysicalChannelOrch,
                   gOchOrch,
                   gEthernetOrch,
                   gLldpOrch,
                   gAssignmentOrch,
                   gInterfaceOrch,
                   gOaOrch,
                   gOscOrch,
                   gApsOrch,
                   gApsportOrch,
                   gAttenuatorOrch,
                   gOcmOrch,
                   gOtdrOrch,
                   gDiagOrch };

    m_select = new Select();

    vector<string> flex_counter_tables = {
        CFG_FLEX_COUNTER_GROUP_TABLE_NAME,
        CFG_FLEX_COUNTER_TABLE_NAME
    };
    gFlexCounterOrch = new FlexCounterOrch(m_configDb, flex_counter_tables);
    m_orchList.push_back(gFlexCounterOrch);

    return true;
}

/* Flush redis through otairedis interface */
void OrchDaemon::flush()
{
    SWSS_LOG_ENTER();

    otai_attribute_t attr;
    memset(&attr, 0, sizeof(attr));
    attr.id = OTAI_REDIS_LINECARD_ATTR_FLUSH;
    otai_status_t status = otai_linecard_api->set_linecard_attribute(gLinecardId, &attr);
    if (status != OTAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to flush redis pipeline %d", status);
        exit(EXIT_FAILURE);
    }
}

void OrchDaemon::start()
{
    SWSS_LOG_ENTER();

    for (Orch* o : m_orchList)
    {
        m_select->addSelectables(o->getSelectables());
    }

    while (true)
    {
        Selectable* s;
        int ret;

        ret = m_select->select(&s, SELECT_TIMEOUT);

        if (ret == Select::ERROR)
        {
            SWSS_LOG_NOTICE("Error: %s!\n", strerror(errno));
            continue;
        }

        if (ret == Select::TIMEOUT)
        {
            /* Let otairedis to flush all OTAI function call to ASIC DB.
             * Normally the redis pipeline will flush when enough request
             * accumulated. Still it is possible that small amount of
             * requests live in it. When the daemon has nothing to do, it
             * is a good chance to flush the pipeline  */
            flush();
            continue;
        }

        auto* c = (Executor*)s;
        c->execute();

        /* After each iteration, periodically check all m_toSync map to
         * execute all the remaining tasks that need to be retried. */

         /* TODO: Abstract Orch class to have a specific todo list */
        for (Orch* o : m_orchList)
            o->doTask();
    }
}

