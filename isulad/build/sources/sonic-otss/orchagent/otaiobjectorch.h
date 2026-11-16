#pragma once

#include <string>
#include <vector>
#include <map>
#include <tuple>
#include "orch.h"
#include "otaihelper.h"
#include "notifier.h"
#include "notificationproducer.h"
#include "notifications.h"
#include "timer.h"

using namespace std;
using namespace swss;

extern otai_object_id_t gLinecardId;

typedef otai_status_t (*CreateObjectFunc)(
        otai_object_id_t *oid,
        otai_object_id_t linecard_id,
        uint32_t attr_count,
        const otai_attribute_t *attr_list);

typedef otai_status_t (*RemoveObjectFunc)(
        otai_object_id_t oid);

typedef otai_status_t (*SetObjectAttrFunc)(
        otai_object_id_t oid,
        const otai_attribute_t *attr);

typedef otai_status_t (*GetObjectAttrFunc)(
        otai_object_id_t oid,
        uint32_t attr_count,
        otai_attribute_t *attr_list);

typedef enum _ConfigState_E
{
    CONFIG_MISSING = 0,
    CONFIG_RECEIVED,
    CONFIG_CREATED,
    CONFIG_DONE,
} ConfigState_E;

class OtaiObjectOrch: public Orch
{
public:
    OtaiObjectOrch(DBConnector *db, const vector<string> &table_names): Orch(db, table_names) {}

    OtaiObjectOrch(DBConnector *db,
                  const vector<string> &table_names,
                  otai_object_type_t obj_type,
                  const vector<otai_attr_id_t> &cfg_attrs);

    OtaiObjectOrch(DBConnector *db,
                  const vector<string> &table_names,
                  otai_object_type_t obj_type,
                  const vector<otai_attr_id_t> &cfg_attrs,
                  const vector<string> &auxiliary_fields);

    OtaiObjectOrch(DBConnector *db,
                  vector<TableConnector> &connectors,
                  otai_object_type_t obj_type,
                  const vector<otai_attr_id_t> &cfg_attrs);

    OtaiObjectOrch(DBConnector *db,
                  vector<TableConnector> &connectors,
                  otai_object_type_t obj_type,
                  const vector<otai_attr_id_t> &cfg_attrs,
                  const vector<string> &auxiliary_fields);

    void localDataInit(DBConnector* db,
                       otai_object_type_t obj_type,
                       const vector<otai_attr_id_t>& cfg_attrs);

    void doTask(Consumer &consumer);

    virtual void doTask(NotificationConsumer& consumer);

    void doStateTask(Consumer &consumer);

    bool createOtaiObject(const string &key);

    virtual void addExtraAttrsOnCreate(vector<otai_attribute_t> &attrs) {};

    bool syncStateTable(otai_object_id_t oid, const string &key);

    bool setOtaiObjectAttrs(const string &key,
                           map<string, string> &field_values,
                           string operation_id="");

    virtual void setSelfProcessAttrs(const string &key,
                                     vector<FieldValueTuple> &auxiliary_fv,
                                     string operation_id="");

    otai_status_t setOtaiObjectAttr(otai_object_id_t oid, const string &field, const string &value);

    otai_status_t getOtaiObjectAttr(otai_object_id_t oid, const string &field, string &value);

    virtual void setFlexCounter(otai_object_id_t id, vector<otai_attribute_t> &attrs){};

    virtual void clearFlexCounter(otai_object_id_t id, string key){};

    virtual void doSubobjectStateTask(const string &key, const string &present){};

    void publishOperationResult(string operation_id, int status_code, string message);

    bool translateOtaiObjectAttr(_In_ const string &field,
                                _In_ const string &value,
                                _Out_ otai_attribute_t &attr);

protected:

    shared_ptr<DBConnector> m_stateDb;

    unique_ptr<Table> m_stateTable;

    shared_ptr<DBConnector> m_countersDb;

    string m_countersTable;

    unique_ptr<Table> m_nameMapTable;

    unique_ptr<Table> m_vid2NameTable;

    CreateObjectFunc m_createFunc;

    RemoveObjectFunc m_removeFunc;

    SetObjectAttrFunc m_setFunc;

    GetObjectAttrFunc m_getFunc;

    otai_object_type_t m_objectType;

    string m_objectName;

    /*
     * Attributes that can be modified by OTAI at anytime.
     */

    map<string, otai_attr_id_t> m_createandsetAttrs;

    /*
     * Attributes that can only be set during creation.
     */

    map<string, otai_attr_id_t> m_createonlyAttrs;

    /*
     * Irrecoverable attributes like OTAI_LINECARD_ATTR_RESET.
     * These attributes are modified by notification channels instead of config table.
     */

    map<string, otai_attr_id_t> m_irrecoverableAttrs;

    map<string, otai_attr_id_t> m_mandatoryAttrs;

    map<string, otai_attr_id_t> m_readonlyAttrs;

    ConfigState_E m_configState = CONFIG_MISSING;

    uint32_t m_count;

    set<string> m_keys;

    map<string, otai_object_id_t> m_key2oid;

    map<string, map<string, string>> m_key2createonlyAttrs;

    map<string, map<string, string>> m_key2createandsetAttrs;

    map<string, string> m_key2present;

    set<string> m_auxiliaryFields;

    map<string, vector<FieldValueTuple>> m_key2auxiliaryFvs;

    set<string> m_needToCache;

    NotificationConsumer *m_notificationConsumer;

    NotificationProducer *m_notificationProducer;

};

