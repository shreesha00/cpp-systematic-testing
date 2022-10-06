//#include <pthread.h>
#include <stdio.h>
//#include <unistd.h>
#include <iostream>
#include <stdlib.h>
#include <time.h>

#include "test.h"
#include "controlled_task.h"
#include "systematic_testing_resources.h"
#include "racy_variable.h"
#include "malloc_wrapper.h"
// #define TEST_TIME

using namespace std;
using namespace SystematicTesting;

struct pipe_inode_info
{
    unsigned int writers;
    unsigned int readers;

    pipe_inode_info()
    {
        writers = 0;
        readers = 0;
    }
};

struct INODE
{
    Resources::SynchronizedResource* i_mutex;
    RacyPointer<struct pipe_inode_info> i_pipe;

    INODE()
    {
        i_mutex = new Resources::SynchronizedResource();
        i_pipe.write_wo_interleaving(new pipe_inode_info());
    }
};


struct INODE* inode = new INODE();
struct pipe_inode_info* i_pipe = inode->i_pipe.read_wo_interleaving();

void/*static void**/ pipe_write_open(void* arg)
{
    inode->i_mutex->acquire();
    inode->i_pipe->readers++;
    cout << "threadA: " << std::hex << inode->i_pipe << endl;

    inode->i_mutex->release();
}

void/*static void**/ involve(void* arg)
{
    inode->i_mutex->acquire();
    inode->i_pipe = NULL;
    inode->i_mutex->release();
    cout << "threadB: " << std::hex << inode->i_pipe << endl;
}

void run_iteration()
{
    void* args(NULL);
    ControlledTask<void> t1([&args] { try { pipe_write_open(args); } catch (std::exception& e) { std::cout << e.what() << std::endl; } });
    ControlledTask<void> t2([&args] { try { involve(args); } catch (std::exception& e) { std::cout << e.what() << std::endl; } });
    t1.start();
    t2.start();
    t1.wait();
    t2.wait();
    inode->i_pipe.write_wo_interleaving(i_pipe);
    printf("\nprogram-successful-exit\n");
}


int main()
{
    std::cout << "[test] started." << std::endl;
    auto start_time = std::chrono::steady_clock::now();

    try
    {
        auto settings = CreateDefaultSettings();
        settings.with_random_generator_seed(time(NULL));
        settings.with_prioritization_strategy(10);
        SystematicTestEngineContext context(settings, 1000);
        while (auto iteration = context.next_iteration())
        {
            try
            {
                run_iteration();
            }
            catch (const std::exception& e)
            {
                std::cerr << e.what() << '\n';
            }
            
        }

        auto report = context.report();
        std::cout << report.to_string() << std::endl;

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


    /*
#ifdef TEST_TIME
    static double run_time_begin;
    static double run_time_end;
    static double run_time_total;
    run_time_begin = clock();
#endif

    pthread_t t1, t2;

    pthread_create(&t1, NULL, pipe_write_open, NULL);
    pthread_create(&t2, NULL, involve, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("\nprogram-successful-exit\n");


#ifdef TEST_TIME
    run_time_end = clock();
    run_time_total = run_time_end - run_time_begin;
    printf("test-the-total-time: %.3lf\n", (double)(run_time_total/CLOCKS_PER_SEC)*1000);
#endif

    */
    
    return 0;
}
