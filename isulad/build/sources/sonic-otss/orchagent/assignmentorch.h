#pragma once

#include "otaiobjectorch.h"

class AssignmentOrch: public OtaiObjectOrch
{
public:
    AssignmentOrch(DBConnector *db, const vector<string> &table_names);
    void setFlexCounter(otai_object_id_t id, vector<otai_attribute_t> &attrs);
};

