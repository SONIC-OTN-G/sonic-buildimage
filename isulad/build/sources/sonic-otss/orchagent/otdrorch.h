#pragma once

#include "otaiobjectorch.h"

class OtdrOrch: public OtaiObjectOrch
{
public:
    OtdrOrch(DBConnector *db, const vector<string> &table_names);

    void setFlexCounter(otai_object_id_t id, vector<otai_attribute_t> &attrs);

    void doTask(swss::NotificationConsumer &consumer);

    void setSelfProcessAttrs(const string &key,
                             vector<FieldValueTuple> &auxiliary_fv,
                             string operation_id="");
};

