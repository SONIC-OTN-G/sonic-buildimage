#pragma once

#include "otaiobjectorch.h"

class TransceiverOrch: public OtaiObjectOrch
{
public:
    TransceiverOrch(DBConnector *db, std::vector<TableConnector>& connectors);
    void setFlexCounter(otai_object_id_t id, vector<otai_attribute_t> &attrs);
    void clearFlexCounter(otai_object_id_t id, string key);
    void doSubobjectStateTask(const string &key, const string &present);

private:
    void doTask(NotificationConsumer& consumer);
    void doUpgradeTask(NotificationConsumer& consumer);
    void getUpgradeState(otai_object_id_t oid);
    swss::NotificationConsumer *m_upgrade_notification_consumer;
    swss::DBConnector* m_db;
    std::unique_ptr<swss::Table> m_pchTable;
    std::unique_ptr<swss::Table> m_lchTable;
    std::unique_ptr<swss::Table> m_otnTable;
    std::unique_ptr<swss::Table> m_ethTable;
    std::unique_ptr<swss::Table> m_ochTable;
    std::unique_ptr<swss::Table> m_intfTable;
};

