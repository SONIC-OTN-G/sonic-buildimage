#pragma once

#include "orch.h"
#include "timer.h"
#include "dbconnector.h"
#include "otaiobjectorch.h"

class DiagOrch : public Orch
{
public:
    DiagOrch(swss::DBConnector *db, const std::vector<std::string> &table_names);
    ~DiagOrch();
private:
    void doTask(Consumer& consumer);
    swss::NotificationConsumer* m_diag_consumer;
    void doTask(swss::NotificationConsumer& consumer);
    swss::DBConnector* m_db;
};


