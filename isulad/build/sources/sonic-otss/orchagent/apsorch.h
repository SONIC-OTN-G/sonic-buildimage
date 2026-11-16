#pragma once

#include "otaiobjectorch.h"

class ApsOrch: public OtaiObjectOrch
{
public:
    ApsOrch(DBConnector *db, const vector<string> &table_names);
    void setFlexCounter(otai_object_id_t id, vector<otai_attribute_t> &attrs);
    void addExtraAttrsOnCreate(vector<otai_attribute_t> &attrs);
};

