// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

/***************************************************/
// Implements a wrapper around a pointer that checks for null dereferences
/***************************************************/
#ifndef SYSTEMATIC_TESTING_NULL_SAFE_PTR_H
#define SYSTEMATIC_TESTING_NULL_SAFE_PTR_H

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
        return "NULL pointer dereference detected";
    }
};

template<typename VariableType>
class NullSafePtr
{
public:
    explicit NullSafePtr(VariableType* const& val = NULL) :
        m_var(val)
    {

    }

    operator VariableType*() const
    {
        return m_var;
    } 

    void operator = (VariableType* const& val)
    {
        m_var = val;
    }

    VariableType* operator -> () const
    {
        throw_if_null();
        return m_var;
    }

    VariableType& operator * () const
    {
        throw_if_null();
        return *m_var;
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
};

#endif // SYSTEMATIC_TESTING_NULL_SAFE_PTR_H