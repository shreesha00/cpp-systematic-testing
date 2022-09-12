//#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
//#include <sys/time.h>
//#include <unistd.h>
#include <time.h>


#include "test.h"
#include "controlled_task.h"
#include "systematic_testing_resources.h"
// #define TEST_TIME

struct CRITICAL_SECTION{
    Resources::SynchronizedResource* mutex;

    CRITICAL_SECTION()
    {
        mutex = new Resources::SynchronizedResource();
    }
};

void Inc(long *num)
{
    ++(*num);
}

long Dec(long *num)
{
    return --(*num);
}

void Enter(Resources::SynchronizedResource* l)
{
    l->acquire();
}

void Exit(Resources::SynchronizedResource* l)
{
    l->release();
}

CRITICAL_SECTION* lock = NULL; 
long waiters;
int done;

void init_vars()
{
    waiters = 0;
    done = 0;
    if(lock != NULL)
    {
        delete lock;
    }
    lock = new CRITICAL_SECTION();
}

std::string err_path;
void/*void**/ once(void* arg, int thread_num)
{
    //static pthread_mutex_t* lock = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    /*
    static CRITICAL_SECTION* lock = new CRITICAL_SECTION();
    static long waiters = 0;
    static int done = 0;
    */

    if(done)
        return;

    auto test_engine = GetTestEngine();
    test_engine->schedule_next_operation();
    Inc(&waiters);

    if(!lock)
    {
        test_engine->notify_assertion_failure("NULL ptr dereference");
        //Dec(&waiters);
        return;
    }
    Enter(lock->mutex);
    printf("T-%d, Enter\n", thread_num);

    if(!done)
    {
        done = 1;
    }

    printf("T-%d, Exit\n", thread_num);

    Exit(lock->mutex);

    if(!Dec(&waiters))
    {
        printf("T-%lx, After  Decrement waiters = %ld\n", thread_num, waiters);
        printf("T-%lx, free lock %p\n", thread_num, lock);
        delete lock;
        lock = NULL;
    }
}

#define thread_size 2

void run_iteration()
{
    ControlledTask<void>* t[thread_size];

    void* arg = NULL;
    init_vars();
    for(int i = 0; i < thread_size;)
    {
        int p = i + 1;
        t[i] = new ControlledTask<void>([&arg, i] { once(arg, i); });
        t[i+1] = new ControlledTask<void>([&arg, p] { once(arg, p); });
        t[i]->start();
        t[i+1]->start();
        i += 2;
    }

    /*
    struct timeval start, end;
    gettimeofday( &start, NULL );
    */

    for(int i = 0; i < thread_size;)
    {
        t[i]->wait();
        t[i+1]->wait();
        i += 2;
    }

    /*
    gettimeofday( &end, NULL );
    int timeuse = 1000000 * ( end.tv_sec - start.tv_sec ) + end.tv_usec -start.tv_usec;
    printf("time: %d us\n", timeuse);
    */

    printf("\nprogram-successful-exit\n");
}

int main()
{
    /*
#ifdef TEST_TIME
    static double run_time_begin;
    static double run_time_end;
    static double run_time_total;
    run_time_begin = clock();
#endif
    ControlledTask<void>* t[thread_size];

    void* arg = NULL;
    for(int i = 0; i < thread_size;)
    {

        t[i] = new ControlledTask<void>([&arg] { once(arg); });
        t[i+1] = new ControlledTask<void>([&arg] { once(arg); });
        t[i]->start();
        t[i+1]->start();
        i += 2;
    }

    struct timeval start, end;
    gettimeofday( &start, NULL );

    for(int i = 0; i < thread_size;)
    {
        t[i]->wait();
        t[i+1]->wait();
        i += 2;
    }

    gettimeofday( &end, NULL );
    int timeuse = 1000000 * ( end.tv_sec - start.tv_sec ) + end.tv_usec -start.tv_usec;
    printf("time: %d us\n", timeuse);

    printf("\nprogram-successful-exit\n");
#ifdef TEST_TIME
    run_time_end = clock();
    run_time_total = run_time_end - run_time_begin;
    printf("test-the-total-time: %.3lf\n", (double)(run_time_total/CLOCKS_PER_SEC)*1000);
#endif
*/
    std::cout << "[test] started." << std::endl;
    auto start_time = std::chrono::steady_clock::now();

    try
    {
        auto settings = CreateDefaultSettings();
        //settings.with_resource_race_checking_enabled(true);
        settings.with_prioritization_strategy();
        SystematicTestEngineContext context(settings, 1000);
        while (auto iteration = context.next_iteration())
        {
            run_iteration();
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
}
