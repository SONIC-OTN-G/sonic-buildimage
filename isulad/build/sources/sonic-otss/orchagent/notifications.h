#pragma once

extern "C" {
#include "otai.h"
}

void onApsSwitchInfoNotify();
void onLinecardAlarmNotify();
void onLinecardActive();
void onLinecardStateChange(_In_ otai_object_id_t linecard_id,
                           _In_ otai_oper_status_t linecard_oper_status);
void onOcmSpectrumPowerNotify(_In_ otai_object_id_t linecard_id,
                              _In_ otai_object_id_t ocm_id,
                              _In_ otai_spectrum_power_t ocm_result);

void onOtdrResultNotify(_In_ otai_object_id_t linecard_id,
                        _In_ otai_object_id_t ocm_id,
                        _In_ otai_otdr_result_t otdr_result);

