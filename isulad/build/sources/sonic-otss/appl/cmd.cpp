/**
 * Copyright (c) 2023 Alibaba Group Holding Limited
 * Copyright (c) 2023 Accelink Technologies Co., Ltd.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License"); you may
 *    not use this file except in compliance with the License. You may obtain
 *    a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 *    THIS CODE IS PROVIDED ON AN *AS IS* BASIS, WITHOUT WARRANTIES OR
 *    CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 *    LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
 *    FOR A PARTICULAR PURPOSE, MERCHANTABILITY OR NON-INFRINGEMENT.
 *
 *    See the Apache Version 2.0 License for specific language governing
 *    permissions and limitations under the License.
 *
 */

#include <iostream>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <arpa/inet.h>
#include "notificationproducer.h"
#include "notificationconsumer.h"
#include "select.h"
#include "logger.h"

using namespace std;

#define COUNTOF(ary)        ((int) (sizeof (ary) / sizeof ((ary)[0])))

void printUsage()
{
    SWSS_LOG_ENTER();

    std::cout << "Usage: cmd [set|get|state]" << std::endl;
    std::cout << "    cmd set <TABLE> <KEY> <FIELD>=<VALUE>" << std::endl;
    std::cout << "    cmd get <TABLE> <KEY> <FIELD>" << std::endl;
    std::cout << "" << std::endl;
    std::cout << "    cmd state" << std::endl;
}

typedef bool (*cmd_func_t)(int argc, char **argv);

typedef struct cmd_s
{
    char * c_cmd;
    cmd_func_t c_f;

} cmd_t;

bool cmd_set(int argc, char **argv);
bool cmd_get(int argc, char **argv);
bool cmd_state(int argc, char **argv);

cmd_t cmd_list[] = {
    {"set", cmd_set}, 
    {"get", cmd_get},
    {"state", cmd_state},
};

int cmd_cnt = COUNTOF(cmd_list);

bool cmd_set(int argc, char **argv)
{
    if (argc != 4)
    {
        printUsage();
        exit(EXIT_FAILURE);
    }

    string query_channel;
    string reply_channel;

    string op = "set";
    string table  = argv[1];
    string key = argv[2];

    if (table == "APS")
    {
        query_channel = OT_APS_NOTIFICATION;
        reply_channel = OT_APS_REPLY;
    }
    else if (table == "ETHERNET")
    {
        query_channel = OT_ETHERNET_NOTIFICATION;
        reply_channel = OT_ETHERNET_REPLY;
    }
    else if (table == "TRANSCEIVER")
    {
        query_channel = OT_TRANSCEIVER_NOTIFICATION;
        reply_channel = OT_TRANSCEIVER_REPLY;
    }
    else if (table == "PORT")
    {
        query_channel = OT_PORT_NOTIFICATION;
        reply_channel = OT_PORT_REPLY;
    }
    else if (table == "LINECARD")
    {
        query_channel = OT_LINECARD_NOTIFICATION;
        reply_channel = OT_LINECARD_REPLY;
    }
    else if (table == "OTDR")
    {
        query_channel = OT_OTDR_NOTIFICATION;
        reply_channel = OT_OTDR_REPLY;
    }
    else
    {
        SWSS_LOG_NOTICE("Invalid table %s", table.c_str());
        return false; 
    }
 
    swss::DBConnector db("APPL_DB", 0);
    swss::NotificationProducer query(&db, query_channel);
    swss::NotificationConsumer reply(&db, reply_channel);

    swss::Select s;
    s.addSelectable(&reply);
    swss::Selectable *sel;

    std::vector<swss::FieldValueTuple> values;

    char *field = strtok(argv[3], "=");
    char *value = strtok(NULL, "=");

    if (value == NULL || field == NULL)
    {
        printUsage();
        exit(EXIT_FAILURE);
    }

    values.emplace_back(field, value);

    query.send(op, key, values);

    SWSS_LOG_NOTICE("set %s, %s=%s", key.c_str(), field, value);
    
    std::string op_ret, data;
    std::vector<swss::FieldValueTuple> values_ret;
    int wait_time = 15000;
    int result = s.select(&sel, wait_time);
    if (result == swss::Select::OBJECT) {
        reply.pop(op_ret, data, values_ret);
        if (op_ret == "SUCCESS") {
            return true;
        } else {
            SWSS_LOG_NOTICE("command exec failed, op_ret %s status %s", op_ret.c_str(), data.c_str());
        }
    } else if (result == swss::Select::TIMEOUT) {
        SWSS_LOG_NOTICE("command exec failed for %s timed out", op_ret.c_str());
    } else {
        SWSS_LOG_NOTICE("command exec failed for %s error", op_ret.c_str());
    }
    values_ret.clear();

    return false;
}

bool cmd_get(int argc, char **argv)
{
    if (argc != 4)
    {   
        printUsage();
        exit(EXIT_FAILURE);
    }

    string query_channel;
    string reply_channel;

    string op = "get";
    string table  = argv[1];
    string key = argv[2];
    string field = argv[3];
    string value;

    if (table == "LINECARD")
    {
        query_channel = OT_LINECARD_NOTIFICATION;
        reply_channel = OT_LINECARD_REPLY;
    }
    else
    {
        SWSS_LOG_NOTICE("Invalid table %s", table.c_str());
        return false;
    }

    swss::DBConnector db("APPL_DB", 0);
    swss::NotificationProducer query(&db, query_channel);
    swss::NotificationConsumer reply(&db, reply_channel);

    swss::Select s;
    s.addSelectable(&reply);
    swss::Selectable *sel;

    std::vector<swss::FieldValueTuple> values;
     
    values.emplace_back(field, value);
    query.send(op, key, values);
    
    std::string op_ret, data;
    std::vector<swss::FieldValueTuple> values_ret;
    int wait_time = 15000;
    int result = s.select(&sel, wait_time);
    if (result == swss::Select::OBJECT) {
        reply.pop(op_ret, data, values_ret);
        if (op_ret == "SUCCESS") {
            for (auto v: values_ret) {
                if (std::get<0>(v) != field) {
                    continue;
                }
                std::cout << std::get<1>(v) << std::endl;
                return true; 
            }
        } else {
            SWSS_LOG_NOTICE("command exec failed, op_ret %s status %s", op_ret.c_str(), data.c_str());
        }
    } else if (result == swss::Select::TIMEOUT) {
        SWSS_LOG_NOTICE("command exec failed (timed out)");
    } else {
        SWSS_LOG_NOTICE("command exec failed");
    }
    values_ret.clear();

    std::cout << "command exec failed" << std::endl;
    return EXIT_FAILURE;
}

bool cmd_state(int argc, char **argv)
{
    swss::DBConnector db("APPL_DB", 0);
    swss::NotificationProducer query(&db, "SWSS_DIAG_CHANNEL");
    swss::NotificationConsumer reply(&db, "SWSS_DIAG_REPLY");

    string op = "state";
    
    swss::Select s;
    s.addSelectable(&reply);
    swss::Selectable *sel;

    std::vector<swss::FieldValueTuple> values;

    query.send(op, op, values);
    
    std::string op_ret, data;
    std::vector<swss::FieldValueTuple> values_ret;
    int wait_time = 15000;
    int result = s.select(&sel, wait_time);
    if (result == swss::Select::OBJECT) {
        reply.pop(op_ret, data, values_ret);
        if (op_ret == "SUCCESS") {
            for (auto v: values_ret) {
                std::cout << "state: " << std::get<1>(v) << std::endl;
            }
            return true;
        } else {
            SWSS_LOG_NOTICE("command exec failed, op_ret %s status %s", op_ret.c_str(), data.c_str());
        }
    } else if (result == swss::Select::TIMEOUT) {
        SWSS_LOG_NOTICE("command exec failed for %s timed out", op_ret.c_str());
    } else {
        SWSS_LOG_NOTICE("command exec failed for %s error", op_ret.c_str());
    }
    values_ret.clear();

    return false;
}

int main(int argc, char **argv)
{
    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_INFO);
    SWSS_LOG_ENTER();
    bool rv = false;
 
    if (argc <= 1)
    {
        printUsage();
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < cmd_cnt; i++)
    {
        if (!strcmp(cmd_list[i].c_cmd, argv[1]))
        {
           rv = cmd_list[i].c_f(argc-1, &argv[1]);
           break;
        }
    }
    if (rv == true)
    {
        return EXIT_SUCCESS; 
    }

    std::cout << "command exec failed" << std::endl;

    return EXIT_FAILURE;
}

