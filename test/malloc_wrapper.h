#ifndef SYSTEMATIC_TESTING_MALLOC_WRAPPER_H
#define SYSTEMATIC_TESTING_MALLOC_WRAPPER_H

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


std::unordered_set<void*> __freed;
void* malloc_safe(size_t size)
{
    void* p = malloc(size);
    if (__freed.find(p) != __freed.end())
    {
        __freed.erase(p);
    }
    return p;
}

void free_safe(void* p)
{
    if (__freed.find(p) != __freed.end())
    {
        (GetTestEngine())->notify_assertion_failure("Double Free detected");
        throw DoubleFreeException();
    }
    __freed.insert(p);
}

#endif // SYSTEMATIC_TESTING_MALLOC_WRAPPER_H
