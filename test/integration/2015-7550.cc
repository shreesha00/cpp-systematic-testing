#include <iostream>
//#include "pthread.h"
#include <stdio.h>
#include <malloc.h>
//#include <unistd.h>
#include <time.h>


#include "test.h"
#include "controlled_task.h"
#include "systematic_testing_resources.h"

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
    struct assoc_array *keys;
};

int key_validate(const struct key *key)
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

static long keyring_read(const struct key* keyring)
{
    unsigned long nr_keys;
    nr_keys = keyring->keys->nr_leaves_on_tree;
    printf("nr_keys = %ld\n", nr_keys);
    return nr_keys;
}

long keyctl_read_key(struct key *key)
{
    long ret = key_validate(key);

    if (ret == 0)
    {
        ret = -EOPNOTSUPP;
        key->sem->acquire();
        if(key->keys == nullptr)
        {
            auto test_engine = GetTestEngine();
            test_engine->notify_assertion_failure("NULL ptr dereference");
            return -1;
        }
        ret = keyring_read(key);
        key->sem->release();
    }
    return ret;
}


void keyring_revoke(struct key* keyring)
{
    keyring->keys = NULL;
}

void key_revoke(struct key *key)
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
    struct key *key = (struct key*)arg;
    key_revoke(key);
}

void thread2(void* arg)
{
    puts("thread 2");
    struct key *key = (struct key*)arg;
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
    ControlledTask<void> t1([&key] { thread1(key); });
    ControlledTask<void> t2([&key] { thread2(key); });

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

    std::cout << "[test] started." << std::endl;
    auto start_time = std::chrono::steady_clock::now();

    try
    {
        auto settings = CreateDefaultSettings();
        settings.with_resource_race_checking_enabled(true);
        settings.with_random_strategy();
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
