#pragma once

#include "orch.h"
#include "timer.h"
#include "dbconnector.h"
#include "otaiobjectorch.h"
#include "orchfsm.h"

struct WarmRestartCheck
{
    bool checkRestartReadyState;
    bool noFreeze;
    bool skipPendingTaskCheck;
};

class LinecardOrch : public OtaiObjectOrch
{
public:
    LinecardOrch(swss::DBConnector* db, std::vector<TableConnector>& connectors);
    bool checkRestartReady() { return m_warmRestartCheck.checkRestartReadyState; }
    bool checkRestartNoFreeze() { return m_warmRestartCheck.noFreeze; }
    bool skipPendingTaskCheck() { return m_warmRestartCheck.skipPendingTaskCheck; }
    void checkRestartReadyDone() { m_warmRestartCheck.checkRestartReadyState = false; }
    void createLinecard(const std::string& key, const map<string, string>& create_attrs);
    void initConfigTotalNum(int num);
    void incConfigNum();
    void stopPreConfigProc();

private:
    void doTask(Consumer& consumer);
    void doAppLinecardTableTask(Consumer& consumer);
    void doLinecardStateTask(Consumer& consumer);
    void setFlexCounter(otai_object_id_t id);
    void setBoardMode(std::string mode);

    swss::DBConnector* m_db;
    WarmRestartCheck m_warmRestartCheck = { false, false, false };
    int m_config_total_num;
    bool m_config_total_num_inited;
    int m_config_num;
};

