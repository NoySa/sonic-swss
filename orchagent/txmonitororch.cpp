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

#include "txmonitororch.h"
#include "portsorch.h"
#include "select.h"
#include "notifier.h"
#include "redisclient.h"
#include "sai_serialize.h"
#include <inttypes.h>

extern PortsOrch* gPortsOrch;


TxMonitorOrch::TxMonitorOrch(TableConnector configDbConnector, TableConnector stateDbConnector) :
        Orch(configDbConnector.first, configDbConnector.second),
        m_countersDb(new DBConnector("COUNTERS_DB", 0)),
        m_countersTable(new Table(m_countersDb.get(), COUNTERS_TABLE)),
        m_countersPortNameMap(new Table(m_countersDb.get(), COUNTERS_PORT_NAME_MAP)),
        m_monitorStateTable(stateDbConnector.first, stateDbConnector.second)
{
    SWSS_LOG_ENTER();

    initDone = false;
    m_pooling_period = Default_PoolingPeriod;
    m_threshold = Default_Threshold;

    initTimer();
}


TxMonitorOrch::~TxMonitorOrch(void)
{
    SWSS_LOG_ENTER();
}


void TxMonitorOrch::doTask(SelectableTimer &timer)
{
    SWSS_LOG_ENTER();

    initMaps();

    if (!initDone)
    {
        return;
    }

    txCounterCheck();
}

void TxMonitorOrch::doTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();

    if (!gPortsOrch->allPortsReady())
     {
         return;
     }

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
       KeyOpFieldsValuesTuple t = it->second;

       string key = kfvKey(t);
       string op = kfvOp(t);

       if (op == SET_COMMAND)
       {
           updateParams(key, kfvFieldsValues(t));
       }
       else
       {
           SWSS_LOG_ERROR("Unknown operation type %s", op.c_str());
       }

       consumer.m_toSync.erase(it++);
    }
}


void TxMonitorOrch::initMaps()
{
    SWSS_LOG_ENTER();

    if (!gPortsOrch->allPortsReady() || initDone)
    {
        return;
    }

    mapPortToName();
    initReadCounter();

    initDone = true;
}

void TxMonitorOrch::insertInfoToDbState(string key, string val)
{
    SWSS_LOG_ENTER();

    vector<FieldValueTuple> fieldValuesVector;
    fieldValuesVector.emplace_back("Value", val);
    m_monitorStateTable.set(key, fieldValuesVector);
}


void TxMonitorOrch::updateParams(const string& key, const vector<FieldValueTuple>& data)
{
    SWSS_LOG_ENTER();

    if(key == KEY_PoolingPeriod)
    {
        setTimer(data);
    }
    else if (key == KEY_Threshold)
    {
        setThreshold(data);
    }
    else
    {
        SWSS_LOG_ERROR("Unknown key %s", key.c_str());
    }
}



void TxMonitorOrch::mapPortToName()
{
    SWSS_LOG_ENTER();

    for (auto const &curr : gPortsOrch->getAllPorts())
    {
        string portName = curr.first;
        Port port = curr.second;
        string portOid;

        if (port.m_type != Port::Type::PHY)
        {
            continue;
        }

        if (!m_countersPortNameMap->hget("", portName, portOid))
        {
            SWSS_LOG_ERROR("error getting port name from counters");
            continue;
        }
        m_portsStringsMap[port.m_alias] = portOid;
    }
}

void TxMonitorOrch::initReadCounter()
{
    SWSS_LOG_ENTER();

    for (auto &entry : m_portsStringsMap)
    {
        string portAlias = entry.first;
        string portOid = entry.second;
        string portState = "OK";
        string strVal;

        if (!m_countersTable->hget(portOid, counterName, strVal))
        {
            SWSS_LOG_WARN("Error reading counters table");
            SWSS_LOG_ERROR("Cannot take information from table for port %s", portOid.c_str());
            strVal = "0";
            portState = "UNKNOWN";
        }

        int newTxCounter;

        try
        {
            newTxCounter = stoi(strVal);

            if(newTxCounter < 0)
            {
                throw logic_error("Non-logical tx-counter value");
            }
        }
        catch (...)
        {
            newTxCounter = 0;
            portState = "UNKNOWN";

            SWSS_LOG_ERROR("Tx-counter value invalid");
        }

        m_portsMap_txCounter[portAlias] = newTxCounter;
        insertInfoToDbState(portAlias, portState);
    }
    m_monitorStateTable.flush();
}



void TxMonitorOrch::initTimer()
{
    SWSS_LOG_ENTER();

    auto interv = timespec { .tv_sec = (int)m_pooling_period, .tv_nsec = 0 };
    m_timer = new SelectableTimer(interv);
    auto executor = new ExecutableTimer(m_timer, this, "Tx_Port_Monitor");
    Orch::addExecutor(executor);
    m_timer->start();
}

void TxMonitorOrch::setTimer(const vector<FieldValueTuple>& data)
{
    SWSS_LOG_ENTER();

    int new_PoolingPeriod;

    for (auto i : data)
    {
        new_PoolingPeriod = Default_PoolingPeriod;
        const auto &field = fvField(i);
        const auto &value = fvValue(i);

        if (field == FIELD_VALUE)
        {
            try
            {
                new_PoolingPeriod = stoi(value);
                if(new_PoolingPeriod <= 0)
                {
                    throw invalid_argument("Illegal pooling period");
                }
            }
            catch(...)
            {
                SWSS_LOG_ERROR("Illegal pooling period");
                continue;
            }
        }
        else
        {
            SWSS_LOG_ERROR("Unknown value type");
            continue;
        }

        m_pooling_period = new_PoolingPeriod;
    }

    auto interv = timespec { .tv_sec = (int)m_pooling_period, .tv_nsec = 0 };
    m_timer->setInterval(interv);
    m_timer->reset();
}

void TxMonitorOrch::setThreshold(const vector<FieldValueTuple>& data)
{
    SWSS_LOG_ENTER();

    int new_Threshold;

    for (auto i : data)
    {
        new_Threshold = Default_Threshold;
        const auto &field = fvField(i);
        const auto &value = fvValue(i);

        if (field == FIELD_VALUE)
        {
            try
            {
                new_Threshold = stoi(value);
                if(new_Threshold <= 0)
                {
                    throw invalid_argument("Illegal threshold");
                }
            }
            catch(...)
            {
                SWSS_LOG_ERROR("Illegal threshold");
                continue;
            }
        }
        else
        {
            SWSS_LOG_ERROR("Unknown value type");
            continue;
        }

        m_threshold = new_Threshold;
    }
}


void TxMonitorOrch::txCounterCheck()
{
    SWSS_LOG_ENTER();

    for (auto& i : m_portsMap_txCounter)
    {
        string portAlias = i.first;
        int txCounter = i.second;

        string portOid = m_portsStringsMap.find(portAlias)->second;
        string strVal = readCounterForPort(portOid, counterName);

        int newTxCounter;
        string portState;

        try
        {
            if(strVal == "-1")
            {
                throw logic_error("Missing tx-counter value");
            }

            newTxCounter = stoi(strVal);

            if(newTxCounter < 0)
            {
                throw logic_error("Non-logical tx-counter value");
            }
        }
        catch (...)
        {
            m_portsMap_txCounter[portAlias] = 0;

            insertInfoToDbState(portAlias, "UNKNOWN");

            SWSS_LOG_ERROR("Tx-counter value invalid");
            continue;
        }

        if(newTxCounter - txCounter > (int)m_threshold)     /* Port is not valid */
        {
            portState = "NOT_OK";
        }
        else                                                /* Port is valid */
        {
            portState = "OK";
        }

        m_portsMap_txCounter[portAlias] = newTxCounter;
        insertInfoToDbState(portAlias, portState);
    }
    m_monitorStateTable.flush();
}

string TxMonitorOrch::readCounterForPort(string oid, string counterName){
    SWSS_LOG_ENTER();

    string strVal;

    if (!m_countersTable->hget(oid, counterName, strVal))
    {
        strVal = "-1";
    }
    return strVal;
}



