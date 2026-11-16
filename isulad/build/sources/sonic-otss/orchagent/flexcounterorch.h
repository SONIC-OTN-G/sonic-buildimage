#ifndef FLEXCOUNTER_ORCH_H
#define FLEXCOUNTER_ORCH_H

#include "orch.h"
#include "producertable.h"
#include "flex_counter_manager.h"

extern "C" {
#include "otai.h"
}

#define STAT_GAUGE_FLEX_COUNTER_GROUP "1S_STAT_GAUGE"
#define STAT_COUNTER_COUNTER_FLEX_COUNTER_GROUP "1S_STAT_COUNTER"
#define STAT_STATUS_COUNTER_FLEX_COUNTER_GROUP "1S_STAT_STATUS"
#define DEFAULT_POLL_TIME 1000

class FlexCounterOrch: public Orch
{
public:
    void doTask(Consumer &consumer);
    FlexCounterOrch(swss::DBConnector *db, std::vector<std::string> &tableNames);
    virtual ~FlexCounterOrch(void);
//    void doCounterTableTask(Consumer &consumer);
    void initCounterTable();
    void doCounterGroupTask(Consumer &consumer);
    FlexCounterManager *getCounterGroup(){return &m_counterManager;};
    FlexCounterManager *getGaugeGroup(){return &m_gaugeManager;};
    FlexCounterManager *getStatusGroup(){return &m_statusManager;};
    bool checkFlexCounterInit(){return m_flexCounterInit;};
 
private:
    std::shared_ptr<swss::DBConnector> m_flexCounterDb = nullptr;
    std::shared_ptr<swss::ProducerTable> m_flexCounterGroupTable = nullptr;
private:
    bool m_flexCounterInit;
    bool m_flexCounterGroupInit;
    FlexCounterManager m_gaugeManager;
    FlexCounterManager m_counterManager;
    FlexCounterManager m_statusManager;
};

#endif

