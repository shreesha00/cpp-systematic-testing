//#include <pthread.h>
#include <stdio.h>
//#include <unistd.h>
#include <iostream>
#include <stdlib.h>
//#include <pthread.h>
#include <time.h>


#include "test.h"
#include "controlled_task.h"
#include "systematic_testing_resources.h"
#include "malloc_wrapper.h"
#include "racy_variable.h"
//#define MUTEX_FOR_CORRECT_EXE_SEQUENCE

#ifdef MUTEX_FOR_CORRECT_EXE_SEQUENCE
Resources::SynchronizedResource* lock_for_correct_exe;
#endif

// #define TEST_TIME

#define offsetof(TYPE,MEMBER) ((int) &((TYPE *)0)->MEMBER)

#define container_of(ptr, type, member) \
    (type *)((char *)(ptr) - (char *) &((type *)0)->member)

struct sock
{
    int sk_err;

    sock(int arg)
    {
        sk_err = arg;
    }
};

struct sk_buff
{
    struct sock *sk;
    int priority;
};

struct netlink_callback {
	RacyPointer<struct sk_buff> skb;
};

struct netlink_sock {
	struct sock		sk;
	struct netlink_callback	cb;
};

struct pthread_args
{
	void* sk;
	int thread_id;
};

static inline struct netlink_sock *nlk_sk(struct sock *sk)
{
	return container_of(sk, struct netlink_sock, sk);
}

Resources::SynchronizedResource* mutex;
int cnt = 1;

void netlink_dump(void* args)
{
	void* sk = ((struct pthread_args*)args)->sk;
	int thread_id = ((struct pthread_args*)args)->thread_id;
	if(thread_id==2)
#ifdef MUTEX_FOR_CORRECT_EXE_SEQUENCE
		lock_for_correct_exe->acquire();
#else
		/*_sleep(1)*/;
#endif

	struct netlink_sock *nlk = nlk_sk((struct sock*)sk);
	printf("sk %p, nlk %p, &nlk->cb %p\n", sk, nlk, &(nlk->cb));
	struct netlink_callback *cb;
	struct sk_buff *skb = NULL;

	printf("%x: mutex %p\n", (unsigned int)thread_id, &mutex);
	mutex->acquire();

    //std::cout << nlk << std::endl;
	cb = &nlk->cb;

    if(!skb)
    {
        skb = (struct sk_buff*)malloc_safe(cnt*sizeof(struct sk_buff));
        cnt *= 4;
    }

	skb->sk = (struct sock*)sk;
	cb->skb = skb;

	mutex->release();

    //std::cout << "Freeing " << cb->skb << std::endl;
	free_safe(cb->skb);

# ifdef MUTEX_FOR_CORRECT_EXE_SEQUENCE
	if(thread_id == 1)
		lock_for_correct_exe->release();
#endif
}

void run_iteration()
{
#ifdef TEST_TIME
	static double run_time_begin;
	static double run_time_end;
	static double run_time_total;
	run_time_begin = clock();
#endif 

#ifdef MUTEX_FOR_CORRECT_EXE_SEQUENCE
	lock_for_correct_exe = new Resources::SynchronizedResource();
    lock_for_correct_exe->acquire();
#endif
    mutex = new Resources::SynchronizedResource();
    struct sock* sk = new sock(5);

    struct pthread_args arg1, arg2;
    cnt = 1;
    arg1.sk=(void*)sk;
    arg1.thread_id=1;
    arg2.sk=(void*)sk;
    arg2.thread_id=2;

    ControlledTask<void> t1([&arg1] { try { netlink_dump(&arg1); } catch (std::exception& e) { std::cout << e.what() << std::endl; } });
    ControlledTask<void> t2([&arg2] { try { netlink_dump(&arg2); } catch (std::exception& e) { std::cout << e.what() << std::endl; } });

    t1.start();
    t2.start();
    t1.wait();
    t2.wait();

    delete mutex;
    printf("\nprogram-successful-exit\n");
#ifdef TEST_TIME
    run_time_end = clock();
    run_time_total = run_time_end - run_time_begin;
    printf("test-the-total-time: %.3lf\n", (double)(run_time_total/CLOCKS_PER_SEC)*1000);
#endif
}

int main()
{
    /*
    std::cout << "[test] started." << std::endl;
    auto start_time = std::chrono::steady_clock::now();

    try
    {
        auto settings = CreateDefaultSettings();
        settings.with_resource_race_checking_enabled(true);
        settings.with_random_strategy();
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
