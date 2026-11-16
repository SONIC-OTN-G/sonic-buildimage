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

#include <string>
#include <set>
#include <map>
#include <iostream>
#include <memory>
#include "dbconnector.h"

using namespace std;
using namespace swss;

void dump_table(shared_ptr<DBConnector> db, string key)
{
    map<std::string, std::string> kvs;
    db->hgetall(key, inserter(kvs, kvs.end()));

    for (auto& kv : kvs)
    {   
        const string& skey = kv.first;
        const string& svalue = kv.second;
    
        cout << key \
             << " attr value " << skey \
             << " with on " << svalue << endl;
    
    } 
}

void dump_db(shared_ptr<DBConnector> db)
{
    string pattern = "*";
    std::set<std::string> keys_sorted;

    auto keys = db->keys(pattern);
    for (auto k : keys)
    {
        keys_sorted.insert(k);
    }

    for (auto k : keys_sorted)
    {
        dump_table(db, k);
    }
}

int main()
{
    auto state_db = shared_ptr<DBConnector>(new DBConnector("STATE_DB", 0));
    auto counters_db = shared_ptr<DBConnector>(new DBConnector("COUNTERS_DB", 0));

    cout << "dump state-db" << endl;
    dump_db(state_db);
    
    cout << "dump counters-db" << endl;
    dump_db(counters_db);
    
    return 0;
}
