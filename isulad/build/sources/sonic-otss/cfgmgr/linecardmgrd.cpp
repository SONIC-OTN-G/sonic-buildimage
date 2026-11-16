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

#include <fstream>
#include <iostream>
#include <mutex>
#include <unistd.h>
#include <vector>

#include "exec.h"
#include "linecardmgr.h"
#include "schema.h"
#include "select.h"

using namespace std;
using namespace swss;

/* select() function timeout retry time, in millisecond */
#define SELECT_TIMEOUT 1000

/*
 * Following global variables are defined here for the purpose of
 * using existing Orch class which is to be refactored soon to
 * eliminate the direct exposure of the global variables.
 *
 * Once Orch class refactoring is done, these global variables
 * should be removed from here.
 */
int gBatchSize = 0;
/* Global database mutex */
mutex gDbMutex;

int main(int argc, char **argv)
{
    Logger::linkToDbNative("linecardmgrd");
    SWSS_LOG_ENTER();

    SWSS_LOG_NOTICE("--- Starting linecardmgrd ---");

    try
    {
        vector<string> cfg_cardmgr_tables = {
            CFG_OT_LINECARD_TABLE_NAME,
        };

        DBConnector cfgDb("CONFIG_DB", 0);
        DBConnector appDb("APPL_DB", 0);
        DBConnector stateDb("STATE_DB", 0);

        LineCardMgr linecardmgr(&cfgDb, &appDb, &stateDb, cfg_cardmgr_tables);

        // TODO: add tables in stateDB which interface depends on to monitor list
        vector<Orch *> cfgOrchList = {&linecardmgr};

        swss::Select s;
        for (Orch *o : cfgOrchList)
        {
            s.addSelectables(o->getSelectables());
        }

        while (true)
        {
            Selectable *sel;
            int ret;

            ret = s.select(&sel, SELECT_TIMEOUT);
            if (ret == Select::ERROR)
            {
                SWSS_LOG_NOTICE("Error: %s!", strerror(errno));
                continue;
            }
            if (ret == Select::TIMEOUT)
            {
                linecardmgr.doTask();
                continue;
            }

            auto *c = (Executor *)sel;
            c->execute();
        }
    }
    catch (const exception &e)
    {
        SWSS_LOG_ERROR("Runtime error: %s", e.what());
    }
    return -1;
}
