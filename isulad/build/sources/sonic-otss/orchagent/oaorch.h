#pragma once

#include "otaiobjectorch.h"

class OaOrch: public OtaiObjectOrch
{
public:
    OaOrch(DBConnector *db, const vector<string> &table_names);
    void setFlexCounter(otai_object_id_t id, vector<otai_attribute_t> &attrs);
};

