#include "test.h"
#include "controlled_task.h"
#include "systematic_testing_resources.h"
#include <iostream>

int x, y, z;
Resources::SynchronizedResource *l1, *l2, *l3;
void thread_one()
{
    l1->acquire();
    x = 1;
    l1->release();
}

void thread_two()
{
    l1->acquire();
    x++;
    l1->release();
    l2->acquire();
    y++;
    l2->release();
    l3->acquire();
    z = 3;
    l3->release();
}

void thread_three()
{
    l2->acquire();
    y = 2;
    l2->release();
    l3->acquire();
    z++;
    l3->release();
    l1->acquire();
    l2->acquire();
    l3->acquire();
    if (x + y + z >= 9)
    {
        (GetTestEngine())->notify_assertion_failure("Bug!");
    }
    l3->release();
    l2->release();
    l1->release();
}

void run_iteration()
{
    l1 = new Resources::SynchronizedResource();
    l2 = new Resources::SynchronizedResource();
    l3 = new Resources::SynchronizedResource();
    ControlledTask<void> t1(thread_one);
    ControlledTask<void> t2(thread_two);
    ControlledTask<void> t3(thread_three);
    t1.start();
    t2.start();
    t3.start();
    t1.wait();
    t2.wait();
    t3.wait();
    x = y = z = 0;
    delete l1;
    delete l2;
    delete l3;
}

int main()
{
    std::cout << "[test] started." << std::endl;
    auto start_time = std::chrono::steady_clock::now();

    try
    {
        auto settings = CreateDefaultSettings();
        settings.with_random_generator_seed(time(NULL));
        settings.with_resource_race_checking_enabled(true);
        settings.with_pos_strategy();
        SystematicTestEngineContext context(settings, 1000);
        while (auto iteration = context.next_iteration())
        {
            run_iteration();
        }

        auto report = context.report();
        std::cout << report.to_string() << std::endl;

        //std::cout << context.total_iterations() << std::endl;
        //std::cout << report.total_controlled_operations() << std::endl;
        assert(context.total_iterations() == report.iterations(), "Number of iterations is not correct.");
        assert(context.total_iterations() * 4 == report.total_controlled_operations(), "Number of controlled operations is not correct.");
        assert(0 == report.total_uncontrolled_threads(), "Number of uncontrolled threads is not correct.");
    }
    catch (std::string error)
    {
        std::cout << "[test] failed: " << error << std::endl;
        return 1;
    }
  
    std::cout << "[test] done in " << total_time(start_time) << "ms." << std::endl;
    return 0;
}