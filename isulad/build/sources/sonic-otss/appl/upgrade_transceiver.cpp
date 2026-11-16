/**
 * Copyright (c) 2023 Alibaba Group Holding Limited
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

#include <unistd.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "notificationproducer.h"
#include "notificationconsumer.h"
#include "select.h"
#include "logger.h"


void printUsage()
{
    SWSS_LOG_ENTER();

    std::cout << "Usage: upgrade_transceiver [switch|backup|state|download]" << std::endl;
    std::cout << "    -s --slot" << std::endl;
    std::cout << "        Slot id" << std::endl;
    std::cout << "    -l --line" << std::endl;
    std::cout << "        Line id" << std::endl;
    std::cout << "    -p --partition" << std::endl;
    std::cout << "        Partition: [A|B]" << std::endl;
    std::cout << "    -h --help:" << std::endl;
    std::cout << "        Print out this message" << std::endl;
}

int main(int argc, char **argv)
{
    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_INFO);
    SWSS_LOG_ENTER();
    std::string partition;
    std::string slot_id;
    std::string line_id;
    const char* const optstring = "s:l:p:";
    std::string op;
    
    if (argc <= 1) {
        printUsage();
        exit(EXIT_FAILURE);
    }

    if (!strcmp(argv[1], "switch") || 
        !strcmp(argv[1], "backup") ||
        !strcmp(argv[1], "state") ||
        !strcmp(argv[1], "download")) {
        op = argv[1];
    } else {
        printUsage();
        exit(EXIT_FAILURE);
    }
    SWSS_LOG_NOTICE("%s", op.c_str());

    while(true) {
        static struct option long_options[] = {
            { "slot-id", required_argument, 0, 's' },
            { "line-id", required_argument, 0, 'l' },
            { "partition", required_argument, 0, 'p' },
        };

        int option_index = 0;

        int c = getopt_long(argc, argv, optstring, long_options, &option_index);

        if (c == -1) {
            break;
        }

        switch (c) {
            case 's':
                SWSS_LOG_NOTICE("Slot ID: %s", optarg);
                slot_id = optarg;
                break;
            case 'l':
                SWSS_LOG_NOTICE("Line ID: %s", optarg);
                line_id = optarg;
                break;
            case 'p':
                SWSS_LOG_NOTICE("Partition: %s", optarg);
                partition = optarg;
                break;
            case 'h':
                printUsage();
                exit(EXIT_SUCCESS);
            case '?':
                SWSS_LOG_WARN("unknown option %c", optopt);
                printUsage();
                exit(EXIT_FAILURE);
            default:
                SWSS_LOG_ERROR("getopt_long failure");
                exit(EXIT_FAILURE);
        }
    }

    swss::DBConnector db("APPL_DB", 0);
    swss::NotificationProducer upgrade_transceiver_query(&db, "UPGRADE_TRANSCEIVER");
    swss::NotificationConsumer upgrade_transceiver_reply(&db, "UPGRADE_TRANSCEIVER_REPLY");

    swss::Select s;
    s.addSelectable(&upgrade_transceiver_reply);
    swss::Selectable *sel;

    std::vector<swss::FieldValueTuple> values;

    if (op == "switch" || op == "backup") {
        if (partition == "") {
            std::cout << "Miss argument! -p is necessary" << std::endl;
            printUsage();
            exit(EXIT_FAILURE);
        }
        if (partition != "A" && partition != "B") {
            std::cout << "partition should be A or B" << std::endl;
        }
        values.emplace_back("partition", partition);
    }

    if (slot_id.size() != 1 || !isdigit((slot_id.c_str())[0])) {
        std::cout << "Slot-id is invalid, should be a digital" << std::endl;
        printUsage();
        exit(EXIT_FAILURE);
    }
    if (line_id.size() != 1 || !isdigit((line_id.c_str())[0])) {
        std::cout << "Line-id is invalid, should be a digital" << std::endl;
        printUsage();
        exit(EXIT_FAILURE);
    }

    std::string key = "TRANSCEIVER-1-" + slot_id + "-L" + line_id;
    values.emplace_back("key", key);

    SWSS_LOG_NOTICE("requested %s", op.c_str());
    upgrade_transceiver_query.send(op, op, values);

    std::string op_ret, data;
    std::vector<swss::FieldValueTuple> values_ret;
    int wait_time = 1000;
    int result = s.select(&sel, wait_time);
    if (result == swss::Select::OBJECT) {
        upgrade_transceiver_reply.pop(op_ret, data, values_ret);
        if (data == "SUCCESS") {
            if (op != "state") {
                return EXIT_SUCCESS;
            }
            
            for (auto v: values_ret) {
                if (std::get<0>(v) != "UPGRADE_STATE") {
                    continue;
                }
                std::cout << "Upgrade-state: " << std::get<1>(v) << std::endl;
                return EXIT_SUCCESS;
            }
        } else {
            SWSS_LOG_NOTICE("command exec failed, op_ret %s status %s", op_ret.c_str(), data.c_str());
        }
    } else if (result == swss::Select::TIMEOUT) {
        SWSS_LOG_NOTICE("command exec failed for %s timed out", op_ret.c_str());
    } else {
        SWSS_LOG_NOTICE("command exec failed for %s error", op_ret.c_str());
    }
    values_ret.clear();

    std::cout << "command exec failed" << std::endl;
    return EXIT_FAILURE;
}
