#include "test.h"
#include "controlled_task.h"
#include "systematic_testing_resources.h"
#include <iostream>
int* p;
void thread_one()
{
    if(p == NULL)
    {
        auto test_engine = GetTestEngine();
        test_engine->notify_assertion_failure("NULL ptr dereference");
        return;
    }
    *p = 4;
}

void thread_two()
{
    delete p;
    p = NULL;
}

void run_iteration()
{
    ControlledTask<void> t1(thread_one);
    ControlledTask<void> t2(thread_two);
    p = new int;
    t1.start();
    t2.start();
    t1.wait();
    t2.wait();
    std::cout << "Successful exit" << std::endl;
}

int main()
{
    std::cout << "[test] started." << std::endl;
    auto start_time = std::chrono::steady_clock::now();

    try
    {
        auto settings = CreateDefaultSettings();
        settings.with_resource_race_checking_enabled(true);
        settings.with_random_strategy();
        SystematicTestEngineContext context(settings, 100);
        while (auto iteration = context.next_iteration())
        {
            run_iteration();
        }

        auto report = context.report();
        std::cout << report.to_string() << std::endl;

        //std::cout << context.total_iterations() << std::endl;
        //std::cout << report.total_controlled_operations() << std::endl;
        assert(context.total_iterations() == report.iterations(), "Number of iterations is not correct.");
        assert(context.total_iterations() * 3 == report.total_controlled_operations(), "Number of controlled operations is not correct.");
        assert(0 == report.total_uncontrolled_threads(), "Number of uncontrolled threads is not correct.");
    }
    catch (std::string error)
    {
        std::cout << "[test] failed: " << error << std::endl;
        return 1;
    }
  
    std::cout << "[test] done in " << total_time(start_time) << "ms." << std::endl;
    return 0;
    return 0;
}