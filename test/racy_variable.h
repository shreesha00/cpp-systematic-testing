// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef SYSTEMATIC_TESTING_RACY_VARIABLE_H
#define SYSTEMATIC_TESTING_RACY_VARIABLE_H

#include <any>
#include <future>
#include <optional>
#include <iostream>
#include <thread>
#include <type_traits>
#include <exception>

#include "systematic_testing.h"

using namespace std::chrono_literals;
using namespace SystematicTesting;

class NullDereferenceException : public std::exception 
{ 
public:
    std::string what()
    {
        return "NULL pointer dereference";
    }
};

template<typename VariableType>
class RacyVariable
{
public: 
    explicit RacyVariable() :
        m_var()
    {

    }

    VariableType read() const
    {
        (GetTestEngine())->schedule_next_operation();
        auto read_val = m_var;
        return m_var;
    }

    VariableType read_wo_interleaving() const
    {
        return m_var;
    }

    operator VariableType() const
    {
        return read();
    } 

    void operator = (const VariableType& write_val)
    {
        write(write_val);
    }

    void write(const VariableType& write_val)
    {
        (GetTestEngine())->schedule_next_operation();
        m_var = write_val;
    }

    void write_wo_interleaving(const VariableType& write_val)
    {
        m_var = write_val;
    }

private:
    VariableType m_var;
};

template<typename VariableType>
class RacyPointer
{
public: 
    explicit RacyPointer() :
        m_var()
    {

    }

    VariableType* read() const
    {
        (GetTestEngine())->schedule_next_operation();
        return m_var;
    }

    VariableType* read_wo_interleaving() const
    {
        return m_var;
    }

    operator VariableType*() const
    {
        return read();
    } 

    void operator = (VariableType* const& write_val)
    {
        write(write_val);
    }
    
    VariableType* operator -> () const
    {
        (GetTestEngine())->schedule_next_operation();
        throw_if_null();
        return m_var;
    }

    VariableType& operator * () const
    {
        (GetTestEngine())->schedule_next_operation();
        throw_if_null();
        return *m_var;
    }

    void write(VariableType* const& write_val)
    {
        (GetTestEngine())->schedule_next_operation();
        m_var = write_val;
    }

    void write_wo_interleaving(VariableType* const& write_val)
    {
        m_var = write_val;
    }

private:
    VariableType* m_var;
    void throw_if_null() const
    {
        if(!m_var)
        {
            (GetTestEngine())->notify_assertion_failure("NULL pointer dereference");
            throw NullDereferenceException();
        }
    }
};

#endif // SYSTEMATIC_TESTING_RACY_VARIABLE_H
