#include "test.h"
#include "controlled_task.h"
#include "systematic_testing_resources.h"
#include <iostream>

int x, y, z, w;
Resources::SynchronizedResource *xl, *yl, *zl, *wl;
int a, b;
void thread_two()
{
    xl->acquire();
    x = 1;
    xl->release();
    xl->acquire();
    a = x;
    xl->release();
    yl->acquire();
    y = a;
    yl->release();
    wl->acquire();
    if (w != 1)
    {
        wl->release();
        GetTestEngine()->pause_operation_until_condition([&] { return w == 1; });
    }
    else
    {
        wl->release();
    }
    yl->acquire();
    b = y;
    yl->release();
    zl->acquire();
    z = a + b;
    zl->release();

    
}

void thread_one()
{
    std::cout << "started thread_one" << std::endl;
    xl->acquire();
    x++;
    xl->release();
    yl->acquire();
    y++;
    yl->release();
    wl->acquire();
    w = 1;
    wl->release();
    zl->acquire();
    if (z >= 5)
    {
        GetTestEngine()->notify_assertion_failure("Bug!");
    }
    zl->release();
}

void run_iteration()
{
    xl = new Resources::SynchronizedResource();
    yl = new Resources::SynchronizedResource();
    zl = new Resources::SynchronizedResource();
    wl = new Resources::SynchronizedResource();
    ControlledTask<void> t1(thread_one);
    ControlledTask<void> t2(thread_two);
    t1.start();
    t2.start();
    t1.wait();
    t2.wait();
    x = y = z = a = b = w = 0;
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
        SystematicTestEngineContext context(settings, 10000);
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
}