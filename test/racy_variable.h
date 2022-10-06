// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

/***************************************************/
// Implements a wrapper around variable access that introduces interleavings prior to variable accesses. 
// Wrapper also implements instrumentation that catches Double Free, Use After Free and Null pointer dereference errors
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

class DoubleFreeException : public std::exception
{
public:
    std::string what()
    {
        return "Double Free detected";
    }
};

class UseAfterFreeException : public std::exception 
{
public:
    std::string what()
    {
        return "Use After Free detected";
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
    explicit RacyPointer() :
        m_var()
    {
        m_freed.clear();
    }

    RacyPointer(const RacyPointer& p)
    {
        m_var = p.m_var;
        m_freed.clear();
        m_freed = p.m_freed;
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

    // To be called after the = operator is invoked with the RHS being a memory allocation. Tracks use after free and double free errors
    void allocated()
    {   
        if (m_freed.find((void*)m_var) != m_freed.end())
        {
            m_freed.erase((void*)m_var);
        }
    }

    // To be called before invoking free(<RacyPointer>.read_wo_interleaving()). 
    // Detects double free if the allocation and previous de-allocations have been instrumented correctly. 
    void freeing()
    {
        (GetTestEngine())->schedule_next_operation();
        detect_double_free();
        mark_free();
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
    std::unordered_set<void*> m_freed;

    // Throw a null pointer exception if m_var is NULL. 
    void throw_if_null() const
    {
        if (!m_var)
        {
            (GetTestEngine())->notify_assertion_failure("NULL pointer dereference detected");
            throw NullDereferenceException();
        }
    }

    // Throw a double free exception if detected in m_var
    void detect_double_free() const
    {
        if (m_freed.find((void*)m_var) != m_freed.end())
        {
            (GetTestEngine())->notify_assertion_failure("Double Free detected");
            throw DoubleFreeException();
        }
    }

    // Mark the address contained in m_var as freed
    void mark_free()
    {
        m_freed.insert((void*)m_var);
    }

    // Throw a use after free exception if detected in m_var
    void detect_use_after_free() const
    {
        if (m_freed.find((void*)m_var) != m_freed.end())
        {
            (GetTestEngine())->notify_assertion_failure("Use After Free detected");
            throw UseAfterFreeException();
        }
    }
};

#endif // SYSTEMATIC_TESTING_RACY_VARIABLE_H
