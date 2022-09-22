// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef SYSTEMATIC_TESTING_RACY_VARIABLE_H
#define SYSTEMATIC_TESTING_RACY_VARIABLE_H

#include <any>
#include <future>
#include <optional>
#include <iostream>
#include <thread>

#include "systematic_testing.h"

using namespace std::chrono_literals;
using namespace SystematicTesting;

template<typename VariableType>
class RacyVariable
{
public: 
    explicit RacyVariable() :
    m_test_engine(GetTestEngine()),
    m_var()
    {

    }

    VariableType read()
    {
        auto read_val = m_var;
        m_test_engine->schedule_next_operation();
        return read_val;
    }

    VariableType read_wo_interleaving()
    {
        return m_var;
    }

    void write(VariableType write_val)
    {
        m_var = write_val;
        m_test_engine->schedule_next_operation();
    }

    void write_wo_interleaving(VariableType write_val)
    {
        m_var = write_val;
    }

private:
    TestEngine* m_test_engine;
    VariableType m_var;
};


#endif // SYSTEMATIC_TESTING_RACY_VARIABLE_H
