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

#include <vector>
#include <iostream>
#include "configsync.h"

ConfigSync::ConfigSync(string service_name,
                       string cfg_table_name,
                       string app_table_name):
    m_service_name(service_name),
    m_cfg_db("CONFIG_DB", 0), 
    m_appl_db("APPL_DB", 0),
    m_cfg_table(&m_cfg_db, cfg_table_name.c_str()),
    m_pst(&m_appl_db, app_table_name.c_str()),
    m_sst(&m_cfg_db, cfg_table_name.c_str())
{
    SWSS_LOG_ENTER();
}

ConfigSync::~ConfigSync()
{
    SWSS_LOG_ENTER();
}

vector<Selectable *> ConfigSync::getSelectables()
{
    SWSS_LOG_ENTER();

    vector<Selectable *> selectables;
    selectables.push_back(&m_sst);

    return selectables;
}

void ConfigSync::doTask(Selectable *select)
{
    SWSS_LOG_ENTER();

    if (select == (Selectable *)&m_sst)
    {
        std::deque<KeyOpFieldsValuesTuple> entries;
        m_sst.pops(entries);
    
        for (auto entry: entries)
        {
            string key = kfvKey(entry);
            if (m_entries.find(key) == m_entries.end())
            {
                SWSS_LOG_ERROR("%s|%s is invalid", m_service_name.c_str(), key.c_str());

                continue;
            }

            SWSS_LOG_NOTICE("Getting %s configuration changed, key is %s",
                            m_service_name.c_str(), key.c_str());

            if (m_cfg_map.find(key) != m_cfg_map.end())
            {
                SWSS_LOG_NOTICE("droping previous pending config");
                m_cfg_map.erase(key);
            }
            m_cfg_map[key] = entry;
        }

        handleConfig(m_cfg_map);
    }

    return;
}

bool ConfigSync::handleConfigFromConfigDB()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_NOTICE("Getting %s configuration from ConfigDB", m_service_name.c_str());

    vector<string> keys;

    m_cfg_table.getKeys(keys);

    if (keys.empty())
    {
        SWSS_LOG_NOTICE("No %s configuration in ConfigDB", m_service_name.c_str());
        return false;
    }

    for (auto &k: keys)
    {
        bool valid = false;

        vector<FieldValueTuple> fvs;

        m_cfg_table.get(k, fvs);

        for (auto &fv : fvs)
        {
            if (fv.first == "index")
            {
                valid = true;
            }
        }
        if (valid)
        {
            m_pst.set(k, fvs);
            m_entries.insert(k);
        }
        else
        {
            SWSS_LOG_ERROR("%s|%s doesn't have index field.", m_service_name.c_str(), k.c_str());
        }
    }
    FieldValueTuple count("count", to_string(m_entries.size()));
    vector<FieldValueTuple> configDoneFvs = { count };
    m_pst.set("ConfigDone", configDoneFvs);

    return true;
}

void ConfigSync::handleConfig(map<string, KeyOpFieldsValuesTuple> &cfg_map)
{
    SWSS_LOG_ENTER();

    auto it = cfg_map.begin();

    while (it != cfg_map.end())
    {
        KeyOpFieldsValuesTuple entry = it->second;
        string key = kfvKey(entry);
        string op = kfvOp(entry);
        auto values = kfvFieldsValues(entry);

        SWSS_LOG_NOTICE("Updating %s configuration, key is %s, op is %s ",
                        m_service_name.c_str(), key.c_str(), op.c_str());
        if (op == SET_COMMAND)
        {
            m_pst.set(key, values);
        }

        it = cfg_map.erase(it);
    }
}

