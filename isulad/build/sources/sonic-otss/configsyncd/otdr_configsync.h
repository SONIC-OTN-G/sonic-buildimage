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
    typedef enum OtdrScanStatus_E
    {
        OTDR_SCAN_SUCCESS,
        OTDR_SCAN_FAILURE,
        OTDR_SCAN_TIMEOUT,
        OTDR_SCAN_UNAVAILABLE,
        OTDR_SCAN_ACTIVE,
        OTDR_SCAN_INACTIVE,
    } OtdrScanStatus;

    typedef enum OtdrScanningStatus_E
    {
        OTDR_SCANNING_STATUS_ACTIVE,
        OTDR_SCANNING_STATUS_INACTIVE,
    } OtdrScanningStatus;

    class OtdrScanningMgr
    {
 
    public:
 
        OtdrScanningMgr(std::string otdrName,
                        bool enable,
                        uint64_t startTime,
                        uint32_t period);
 
        ~OtdrScanningMgr();
 
        void startScanningThread();
 
        void stopScanningThread();
 
        void scanningThreadRunFunction();
 
        void updatePeriodicScanningParameter(bool enable,
                                             uint64_t startTime,
                                             uint32_t period);

    private:

        static std::mutex m_mtx;
 
        std::mutex m_mtxSleep;
 
        std::condition_variable m_cvSleep;
 
        DBConnector m_applDb;

        DBConnector m_stateDb;

        Table m_stateTable;
 
        bool m_runScanningThread;
 
        std::string m_otdrName;

        uint64_t m_startTime; /* units: nanosecond */

        uint32_t m_period; /* units: millisecond */
 
        swss::NotificationProducer m_queryChannel;
 
        swss::NotificationConsumer m_replyChannel;
 
        std::shared_ptr<std::thread> m_scanningThread;
 
        void scan();

        bool queryScanningStatus(OtdrScanningStatus &status); 

    };
 
    class OtdrConfigSync : public ConfigSync
    {
 
    public:
 
        OtdrConfigSync();
 
        ~OtdrConfigSync();
 
        bool handleConfigFromConfigDB();
 
        vector<Selectable *> getSelectables();
 
        void doTask(Selectable *);
 
    private:
 
        std::map<std::string, std::shared_ptr<OtdrScanningMgr>> m_otdrScanningMgrs;
 
        void setPeriodicScanningParameter(KeyOpFieldsValuesTuple &entry);
    };

}
