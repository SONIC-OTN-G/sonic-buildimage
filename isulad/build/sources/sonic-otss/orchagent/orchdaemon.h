#ifndef SWSS_ORCHDAEMON_H
#define SWSS_ORCHDAEMON_H

#include <vector>
#include <string>
#include "dbconnector.h"
#include "producerstatetable.h"
#include "consumertable.h"
#include "select.h"

#include "ochorch.h"
#include "logicalchannelorch.h"
#include "physicalchannelorch.h"
#include "otnorch.h"
#include "ethernetorch.h"
#include "lldporch.h"
#include "assignmentorch.h"
#include "interfaceorch.h"
#include "oaorch.h"
#include "oscorch.h"
#include "apsorch.h"
#include "apsportorch.h"
#include "attenuatororch.h"
#include "transceiverorch.h"
#include "portorch.h"
#include "ocmorch.h"
#include "otdrorch.h"
#include "linecardorch.h"
#include "flexcounterorch.h"
#include "diagorch.h"
#include "directory.h"

using namespace swss;
using namespace std;

class OrchDaemon
{
public:
    OrchDaemon(DBConnector *, DBConnector *, DBConnector *);
    ~OrchDaemon();

    bool init();
    void start();
private:
    DBConnector *m_applDb;
    DBConnector *m_configDb;
    DBConnector *m_stateDb;

    std::vector<Orch *> m_orchList;
    Select *m_select;

    void flush();
};

#endif /* SWSS_ORCHDAEMON_H */
