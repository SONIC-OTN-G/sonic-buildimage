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

#include <map>
#include <inttypes.h>

#include "diagorch.h"
#include "converter.h"
#include "notifier.h"
#include "notificationproducer.h"
#include "subscriberstatetable.h"
#include "notifications.h"
#include "orchfsm.h"

using namespace std;
using namespace swss;

DiagOrch::DiagOrch(swss::DBConnector *db, const std::vector<std::string> &table_names):
    Orch(db, table_names),
    m_db(db)
{
    SWSS_LOG_ENTER();

    m_diag_consumer = new NotificationConsumer(db, "SWSS_DIAG_CHANNEL");
    auto diag_notifier = new Notifier(m_diag_consumer, this, "SWSS_DIAG_CHANNEL");
    Orch::addExecutor(diag_notifier);
}

DiagOrch::~DiagOrch()
{
}

void DiagOrch::doTask(Consumer& consumer)
{
    SWSS_LOG_ENTER();
}

void DiagOrch::doTask(NotificationConsumer& consumer)
{
    SWSS_LOG_ENTER();

    if (&consumer == m_diag_consumer)
    {
        std::string op;
        std::string data;
        std::vector<swss::FieldValueTuple> values;
        consumer.pop(op, data, values);

        if (op == "state")
        {
            std::string current_state;
            NotificationProducer reply(m_db, "SWSS_DIAG_REPLY");

            if (OrchFSM::getState() == ORCH_STATE_NOT_READY)
            {
                current_state = "not_ready";
            }
            else if (OrchFSM::getState() == ORCH_STATE_READY)
            {
                current_state = "ready";
            }
            else if (OrchFSM::getState() == ORCH_STATE_WORK)
            {
                current_state = "work";
            }
            else if (OrchFSM::getState() == ORCH_STATE_PAUSE)
            {
                current_state = "pause";
            }

            swss::FieldValueTuple fv("state", current_state);
            std::vector<swss::FieldValueTuple> fvs;
            fvs.push_back(fv);
            op = "SUCCESS";
            reply.send(op, data, fvs);
        }
    }
}

