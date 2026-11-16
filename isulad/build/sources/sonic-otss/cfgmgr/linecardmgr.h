#pragma once

#include "dbconnector.h"
#include "orch.h"
#include "producerstatetable.h"

#include <map>
#include <set>
#include <string>

namespace swss
{
    class LineCardMgr : public Orch
    {
    public:
        LineCardMgr(DBConnector* cfgDb, DBConnector* appDb, DBConnector* stateDb, const std::vector<std::string>& tableNames);

        using Orch::doTask;
    private:
        Table m_cfgLineCardTable;
        Table m_stateLineCardTable;
        ProducerStateTable m_appLineCardTable;

        void doTask(Consumer& consumer);
        bool setLineCardAttr(const std::string& alias, std::vector<FieldValueTuple>& fvs);
        bool isLineCardStateOk(const std::string& alias);
    };

}
