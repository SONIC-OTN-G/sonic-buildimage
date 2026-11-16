/**
 * Copyright (c) 2023 Alibaba Group Holding Limited
 * Copyright (c) 2023 Accelink Technologies Co., Ltd.
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

#include "logger.h"
#include "dbconnector.h"
#include "producerstatetable.h"
#include "tokenize.h"
#include "ipprefix.h"
#include "linecardmgr.h"
#include "exec.h"
#include "shellcmd.h"

using namespace std;
using namespace swss;

LineCardMgr::LineCardMgr(DBConnector* cfgDb, DBConnector* appDb, DBConnector* stateDb, const vector<string>& tableNames) :
    Orch(cfgDb, tableNames),
    m_cfgLineCardTable(cfgDb, CFG_OT_LINECARD_TABLE_NAME),
    m_stateLineCardTable(stateDb, STATE_OT_LINECARD_TABLE_NAME),
    m_appLineCardTable(appDb, APP_OT_LINECARD_TABLE_NAME)
{
}

bool LineCardMgr::setLineCardAttr(const string& alias, std::vector<FieldValueTuple>& fvs)
{
    SWSS_LOG_ENTER();

    m_appLineCardTable.set(alias, fvs);

    return true;
}

bool LineCardMgr::isLineCardStateOk(const string& alias)
{
    SWSS_LOG_ENTER();

    vector<FieldValueTuple> temp;
    const string field("power-admin-state");
    string value;
    if (m_stateLineCardTable.hget(alias, field, value))
    {
        if (value == "POWER_ENABLED")
        {
            SWSS_LOG_NOTICE("power enabled");
            return true;
        }
    }
    SWSS_LOG_NOTICE("power disabled");

    return false;
}

void LineCardMgr::doTask(Consumer& consumer)
{
    SWSS_LOG_ENTER();

    auto it = consumer.m_toSync.begin();

    while (it != consumer.m_toSync.end())
    {
        KeyOpFieldsValuesTuple t = it->second;

        string alias = kfvKey(t);
        string op = kfvOp(t);
        string line_card;
        auto& fvs = kfvFieldsValues(t);

        if (op == SET_COMMAND)
        {
            if (!isLineCardStateOk(alias))
            {
                it++;
                continue;
            }

            vector<FieldValueTuple> newfvs;
            for (auto i : fvs)
            {
                auto field = fvField(i);

                newfvs.emplace_back(i);
                SWSS_LOG_NOTICE("fvField is %s fvValue is %s", fvField(i).c_str(), fvValue(i).c_str());
            }
            if (newfvs.size() > 0)
            {
                setLineCardAttr(alias, newfvs);
            }
        }
        else if (op == DEL_COMMAND)
        {
            SWSS_LOG_NOTICE("Delete LineCard: %s", alias.c_str());
        }

        it = consumer.m_toSync.erase(it);
    }
}
