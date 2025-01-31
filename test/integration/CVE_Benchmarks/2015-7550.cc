#include <iostream>
//#include "pthread.h"
#include <stdio.h>
#include <malloc.h>
//#include <unistd.h>
#include <time.h>


#include "test.h"
#include "controlled_task.h"
#include "systematic_testing_resources.h"
#include "racy_variable.h" 
#include "malloc_wrapper.h"
#include "null_safe_ptr.h"

// #define TEST_TIME

#define EOPNOTSUPP      45
#define ENOKEY          132
#define EKEYEXPIRED     133
#define EKEYREVOKED     134
#define KEY_FLAG_DEAD           1
#define KEY_FLAG_REVOKED        2
#define KEY_FLAG_INVALIDATED    7

struct assoc_array
{
    unsigned long nr_leaves_on_tree;
};


struct key
{
    unsigned long flags;
    Resources::SynchronizedResource* sem;
    NullSafePtr<struct assoc_array> keys;
};

int key_validate(const RacyPointer<struct key> key)
{
    unsigned long flags = key->flags;
    printf("flags = %ld\n", flags);

    if (flags & (1 << KEY_FLAG_INVALIDATED))
        return -ENOKEY;

    if (flags & ((1 << KEY_FLAG_REVOKED) |
                 (1 << KEY_FLAG_DEAD)))
        return -EKEYREVOKED;

    return 0;
}

static long keyring_read(const RacyPointer<struct key> keyring)
{
    unsigned long nr_keys;
    nr_keys = keyring->keys->nr_leaves_on_tree;
    printf("nr_keys = %ld\n", nr_keys);
    return nr_keys;
}

long keyctl_read_key(RacyPointer<struct key> key)
{
    long ret = key_validate(key);

    if (ret == 0)
    {
        ret = -EOPNOTSUPP;
        key->sem->acquire();
        ret = keyring_read(key);
        key->sem->release();
    }
    return ret;
}


void keyring_revoke(RacyPointer<struct key> keyring)
{
    keyring->keys = NULL;
}

void key_revoke(RacyPointer<struct key> key)
{
    key->sem->acquire();
    key->flags = KEY_FLAG_REVOKED;
    puts("revoke key");
    keyring_revoke(key);
    key->sem->release();
}

void thread1(void* arg)
{
    puts("thread 1");
    RacyPointer<struct key> key;
    key = (struct key*)arg;
    key_revoke(key);
}

void thread2(void* arg)
{
    puts("thread 2");
    RacyPointer<struct key> key;
    key = (struct key*)arg;
    keyctl_read_key(key);
}

void run_iteration()
{
    struct key* key = (struct key*)malloc(sizeof(struct key));
    key->flags = 0;
    //pthread_mutex_init(&(key->sem), NULL);
    key->sem = new Resources::SynchronizedResource();
    key->keys = (struct assoc_array*)malloc(sizeof(struct assoc_array));
    key->keys->nr_leaves_on_tree = 1;

    /*struct key key;
    key.flags = 0;*/

    /*
    pthread_t t1, t2;

    pthread_create(&t2, NULL, thread2, key);
    pthread_create(&t1, NULL, thread1, key);

    pthread_join(t2, NULL);
    pthread_join(t1, NULL);
    */
    ControlledTask<void> t1([&key] { try { thread1(key); } catch (std::exception& e) { std::cout << e.what() << std::endl; } });
    ControlledTask<void> t2([&key] { try { thread2(key); } catch (std::exception& e) { std::cout << e.what() << std::endl; } });

    t2.start();
    t1.start();
    
    t2.wait();
    t1.wait();
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
    struct key* key = (struct key*)malloc(sizeof(struct key));
    key->flags = 0;
    //pthread_mutex_init(&(key->sem), NULL);
    key->sem = new Resources::SynchronizedResource();
    key->keys = (struct assoc_array*)malloc(sizeof(struct assoc_array));
    key->keys->nr_leaves_on_tree = 1;

    //struct key key;
    //key.flags = 0;

    pthread_t t1, t2;

    pthread_create(&t2, NULL, thread2, key);
    pthread_create(&t1, NULL, thread1, key);

    pthread_join(t2, NULL);
    pthread_join(t1, NULL);

    printf("\nprogram-successful-exit\n");
#ifdef TEST_TIME
    run_time_end = clock();
    run_time_total = run_time_end - run_time_begin;
    printf("test-the-total-time: %.3lf\n", (double)(run_time_total/CLOCKS_PER_SEC)*1000);
#endif
    */

    /*
    std::cout << "[test] started." << std::endl;
    auto start_time = std::chrono::steady_clock::now();

    try
    {
        auto settings = CreateDefaultSettings();
        settings.with_resource_race_checking_enabled(true);
        settings.with_random_generator_seed(time(NULL));
        settings.with_prioritization_strategy(10);
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
    */
    std::vector<int> bugs_found, context_switches;
    std::vector<int> depths({1, 2, 3, 4, 5, 10});
    try
    {
        auto settings = CreateDefaultSettings();
        settings.with_random_generator_seed(time(NULL));
        settings.with_random_strategy(10);
        settings.with_resource_race_checking_enabled(true);
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
        bugs_found.push_back(report.bugs_found());
        context_switches.push_back(report.avg_scheduling_decisions());

        assert(context.total_iterations() == report.iterations(), "Number of iterations is not correct.");
        assert(context.total_iterations() * 3 == report.total_controlled_operations(), "Number of controlled operations is not correct.");
        assert(0 == report.total_uncontrolled_threads(), "Number of uncontrolled threads is not correct.");
    }
    catch (std::string error)
    {
        std::cout << "[test] failed: " << error << std::endl;
        return 1;
    }
    for (auto d : depths)
    {
        try
        {
            auto settings = CreateDefaultSettings();
            settings.with_random_generator_seed(time(NULL));
            settings.with_prioritization_strategy(d);
            settings.with_resource_race_checking_enabled(true);
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
            bugs_found.push_back(report.bugs_found());
            context_switches.push_back(report.avg_scheduling_decisions());

            assert(context.total_iterations() == report.iterations(), "Number of iterations is not correct.");
            assert(context.total_iterations() * 3 == report.total_controlled_operations(), "Number of controlled operations is not correct.");
            assert(0 == report.total_uncontrolled_threads(), "Number of uncontrolled threads is not correct.");
        }
        catch (std::string error)
        {
            std::cout << "[test] failed: " << error << std::endl;
            return 1;
        }
    }

    std::cout << "Random Strategy: " << std::dec << static_cast<int>(bugs_found[0]) << std::endl;
    for (int i = 1; i < bugs_found.size(); i++)
    {
        std::cout << "PCT Strategy with depth " << std::dec << depths[i-1] << ": " << std::dec << static_cast<int>(bugs_found[i]) << ", scheduling decision: " << std::dec << context_switches[i] << std::endl;
    }
}
