// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

/***************************************************/
// Implements a wrapper around variable access that introduces interleavings prior to variable accesses. 
/***************************************************/
#ifndef SYSTEMATIC_TESTING_RACY_VARIABLE_H
#define SYSTEMATIC_TESTING_RACY_VARIABLE_H

#include <any>
#include <future>
#include <optional>
#include <iostream>
#include <thread>
#include <type_traits>
#include <exception>
#include <unordered_set>

#include "systematic_testing.h"
#include "malloc_wrapper.h"

using namespace std::chrono_literals;
using namespace SystematicTesting;

class NullDereferenceException : public std::exception 
{ 
public:
    std::string what()
    {
        return "NULL pointer dereferenc detected";
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

    operator VariableType() const
    {
        return read();
    } 

    void operator = (const VariableType& write_val)
    {
        write(write_val);
    }

    // Read from the racy variable. Introduces an interleaving prior to the memory access. 
    VariableType read() const
    {
        (GetTestEngine())->schedule_next_operation();
        auto read_val = m_var;
        return m_var;
    }

    // Read from the racy variable without introducing an interleaving
    VariableType read_wo_interleaving() const
    {
        return m_var;
    }

    // Write to the racy variable. Introduces an interleaving prior to the memory access. 
    void write(const VariableType& write_val)
    {
        (GetTestEngine())->schedule_next_operation();
        m_var = write_val;
    }

    // Write to the racy variable without introducing an interleaving
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
    explicit RacyPointer(VariableType* const& val = NULL) :
        m_var(val)
    {
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
        detect_use_after_free();
        return m_var;
    }

    VariableType& operator * () const
    {
        (GetTestEngine())->schedule_next_operation();
        throw_if_null();
        detect_use_after_free();
        return *m_var;
    }

    // Read from the racy pointer. Introduces an interleaving prior to the memory access. 
    VariableType* read() const
    {
        (GetTestEngine())->schedule_next_operation();
        return m_var;
    }

    // Read from the racy pointer without introducing an interleaving
    VariableType* read_wo_interleaving() const
    {
        return m_var;
    }

    // Write to the racy pointer. Introduces an interleaving prior to the memory access. 
    void write(VariableType* const& write_val)
    {
        (GetTestEngine())->schedule_next_operation();
        m_var = write_val;
    }

    // Write to the racy pointer without introducing an interleaving
    void write_wo_interleaving(VariableType* const& write_val)
    {
        m_var = write_val;
    }

private:
    VariableType* m_var;

    // Throw a null pointer exception if m_var is NULL. 
    void throw_if_null() const
    {
        if (!m_var)
        {
            (GetTestEngine())->notify_assertion_failure("NULL pointer dereference detected");
            throw NullDereferenceException();
        }
    }

    // Detect use after free of pointer in m_var
    void detect_use_after_free() const 
    {
        if (__freed.find((void*)m_var) != __freed.end())
        {
            (GetTestEngine())->notify_assertion_failure("Use After Free detected");
            throw UseAfterFreeException();
        }
    }
};

#endif // SYSTEMATIC_TESTING_RACY_VARIABLE_H
