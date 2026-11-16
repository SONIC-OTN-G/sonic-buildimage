#pragma once

#include <string>

extern otai_linecard_api_t*              otai_linecard_api;
extern otai_port_api_t*                  otai_port_api;
extern otai_transceiver_api_t*           otai_transceiver_api;
extern otai_physicalchannel_api_t*       otai_physicalchannel_api;
extern otai_otn_api_t*                   otai_otn_api;
extern otai_och_api_t*                   otai_och_api;
extern otai_logicalchannel_api_t*        otai_logicalchannel_api;
extern otai_lldp_api_t*                  otai_lldp_api;
extern otai_assignment_api_t*            otai_assignment_api;
extern otai_ethernet_api_t*              otai_ethernet_api;
extern otai_interface_api_t*             otai_interface_api;
extern otai_oa_api_t*                    otai_oa_api;
extern otai_osc_api_t*                   otai_osc_api;
extern otai_aps_api_t*                   otai_aps_api;
extern otai_apsport_api_t*               otai_apsport_api;
extern otai_attenuator_api_t*            otai_attenuator_api;
extern otai_ocm_api_t*                   otai_ocm_api;
extern otai_otdr_api_t*                  otai_otdr_api;

void initOtaiApi();
