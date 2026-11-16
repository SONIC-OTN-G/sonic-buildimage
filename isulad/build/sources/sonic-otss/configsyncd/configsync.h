#pragma once
#include <string>
#include <set>
#include <vector>
#include "dbconnector.h"
#include "producerstatetable.h"
#include "subscriberstatetable.h"
#include "select.h"

#define DEFAULT_SELECT_TIMEOUT 1000 /* ms */

using namespace std;
using namespace swss;

class ConfigSync
{
public:

    ConfigSync(string service_name, string cfg_table_name, string app_table_name);

    virtual ~ConfigSync();

    virtual vector<Selectable *> getSelectables();

    virtual void doTask(Selectable *);

    virtual bool handleConfigFromConfigDB();

protected:

    DBConnector m_cfg_db;

    DBConnector m_appl_db;

    Table m_cfg_table;

    ProducerStateTable m_pst;

    SubscriberStateTable m_sst;

    string m_service_name;

    set<string> m_entries;

    map<string, KeyOpFieldsValuesTuple> m_cfg_map;

    virtual void handleConfig(map<string, KeyOpFieldsValuesTuple> &cfg_map);
};

