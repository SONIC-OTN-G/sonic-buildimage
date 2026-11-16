#pragma once

#include <memory>
#include <condition_variable>
#include <mutex>

#include "dbconnector.h"
#include "configsync.h"
#include "producerstatetable.h"
#include "notificationproducer.h"
#include "notificationconsumer.h"

namespace swss
{
    typedef enum OcmScanStatus_E
    {
        OCM_SCAN_SUCCESS,
        OCM_SCAN_FAILURE,
        OCM_SCAN_TIMEOUT,
        OCM_SCAN_UNAVAILABLE,
    } OcmScanStatus;

    class OcmGroupMgr
    {

    public:

        OcmGroupMgr(std::string groupName);

        ~OcmGroupMgr();

        void startScanningThread();

        void stopScanningThread();

        void scanningThreadRunFunction();

        void handleConfig(KeyOpFieldsValuesTuple &entry);

        std::string getGroupName();

    private:

        std::mutex m_mtx;

        std::mutex m_mtxSleep;

        std::condition_variable m_cvSleep;

        DBConnector m_cfgDb;

        DBConnector m_applDb;

        DBConnector m_stateDb;

        Table m_cfgOcmGroupTable;

        Table m_stateTable;

        ProducerStateTable m_appTable;

        std::string m_groupName;

        bool m_runScanningThread;

        std::vector<std::string> m_ocmList;

        FieldValueTuple m_freqGranularity;

        swss::NotificationProducer m_queryChannel;

        swss::NotificationConsumer m_replyChannel;

        std::shared_ptr<std::thread> m_scanningThread;

        OcmScanStatus scan(std::string ocm);

    };

    class OcmConfigSync : public ConfigSync
    {

    public:

        OcmConfigSync();

        ~OcmConfigSync();

        bool handleConfigFromConfigDB();

        vector<Selectable *> getSelectables();

        void doTask(Selectable *);

    private:

        Table m_cfgOcmGroupTable;

        SubscriberStateTable m_cfgOcmGroupSubStateTable;

        void handleOcmGroupConfig();

        std::shared_ptr<OcmGroupMgr> m_ocmGroupMgr;

    };

}

