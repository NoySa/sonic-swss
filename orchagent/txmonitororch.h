/*
 * Copyright (C) Mellanox Technologies, Ltd. 2001-2014 ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Mellanox Technologies, Ltd.
 * (the "Company") and all right, title, and interest in and to the software product,
 * including all associated intellectual property rights, are and shall
 * remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 *
 */

#ifndef SRC_SONIC_SWSS_ORCHAGENT_TXMONITORORCH_H_
#define SRC_SONIC_SWSS_ORCHAGENT_TXMONITORORCH_H_


#include <string>
#include <map>
#include <array>

#include "orch.h"
#include "port.h"
#include "timer.h"
#include "selectabletimer.h"

using namespace std;
using namespace swss;

static const string counterName = "SAI_PORT_STAT_IF_OUT_ERRORS";

static const string FIELD_VALUE = "Value";

static const string KEY_PoolingPeriod = "pooling_period";
static const string KEY_Threshold = "threshold";

#define Default_PoolingPeriod 60
#define Default_Threshold 10

class TxMonitorOrch : public Orch {

    public:
        TxMonitorOrch(TableConnector configDbConnector, TableConnector stateDbConnector);
        virtual ~TxMonitorOrch(void);
        void doTask(Consumer &consumer);
        void doTask(SelectableTimer &timer);

    private:
        bool                initDone;
        uint64_t            m_pooling_period;
        uint64_t            m_threshold;
        SelectableTimer     *m_timer;

        Table               m_monitorStateTable;

        shared_ptr<swss::DBConnector>     m_countersDb = nullptr;
        shared_ptr<swss::Table>           m_countersTable = nullptr;
        shared_ptr<swss::Table>           m_countersPortNameMap = nullptr;

        map<string, int>            m_portsMap_txCounter;   //<alias, txCounter>
        map<string, string>         m_portsStringsMap;      //<alias, oid>

        void initMaps();
        void insertInfoToDbState(string key, string val);
        void updateParams(const string& key, const vector<FieldValueTuple>& data);
        void mapPortToName();

        void initReadCounter();
        void initTimer();

        void setTimer(const vector<FieldValueTuple>& data);
        void setThreshold(const vector<FieldValueTuple>& data);

        void txCounterCheck();
        string readCounterForPort(string oid, string counterName);

};


#endif /* SRC_SONIC_SWSS_ORCHAGENT_TXMONITORORCH_H_ */
