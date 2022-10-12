#include <sys/types.h>
//#include <pthread.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
//#include <unistd.h>
#include <atomic>
#include <iostream>
#include <time.h>

#include "test.h"
#include "controlled_task.h"
#include "systematic_testing_resources.h"
#include "racy_variable.h" 
#include "malloc_wrapper.h"
// #define TEST_TIME

#define PACKET_FANOUT_ROLLOVER		3
#define PACKET_FANOUT_FLAG_ROLLOVER	0x1000

#define	EINVAL		22
#define	ENOMEM		12
#define ENOSPC		28

#define u8 unsigned char
#define u16 unsigned short

Resources::SynchronizedResource* fanout_mutex;

typedef struct {
	std::atomic_int counter;
} atomic_t;

typedef atomic_t atomic_long_t;

struct sock{
    int num;
};

struct packet_rollover {
	int			sock;
	atomic_long_t		num;
};

struct packet_sock {
	struct sock		sk;
	RacyPointer<struct packet_rollover> rollover;
};

struct pthread_args{
    struct sock* sk;
    u16 type_flags;
    int thread_id;
};


static struct packet_sock *pkt_sk(struct sock *sk)
{
	return (struct packet_sock *)sk;
}

static void *kzalloc(unsigned size)
{
    void *p = memalign(8, size);
    if (!p)
        return p;
    memset(p, 0, size);

    return p;
}

static void kfree(void *p)
{
	if (p)
    {
        free(p);
    }

}

void atomic_long_set(atomic_long_t* n, int v){
    n->counter = v;
}

static int fanout_add(struct sock *sk, u16 type_flags, int thread_id)
{
    struct packet_sock *po = pkt_sk(sk);
    u8 type = type_flags & 0xff;
    int err;

    if (type == PACKET_FANOUT_ROLLOVER ||
        (type_flags & PACKET_FANOUT_FLAG_ROLLOVER)) {

        po->rollover = (packet_rollover*)malloc_safe(sizeof(packet_rollover));

        if (!po->rollover)
            return -ENOMEM;
        printf("thread %d kzalloc, po->rollover = %p\n", thread_id, po->rollover);
        atomic_long_set(&po->rollover->num, 0);
        printf("thread %d change.\n", thread_id);

    }

    fanout_mutex->acquire();
    err = -EINVAL;
    fanout_mutex->release();


    if (err) {
        free_safe(po->rollover.read_wo_interleaving());
        printf("thread %d free.\n", thread_id);
        po->rollover = NULL;
    }

    return err;
}

void* thread_func(void* args){
    struct sock *sk = ((pthread_args*)args)->sk;
    u16 type_flags = ((pthread_args*)args)->type_flags;
    int id = ((pthread_args*)args)->thread_id;

    fanout_add(sk, type_flags, id);
}

void run_iteration()
{
#ifdef TEST_TIME
    static double run_time_begin;
    static double run_time_end;
    static double run_time_total;
    run_time_begin = clock();
#endif

    struct packet_sock pktsk;
    struct sock sk;
    pktsk.sk = sk;

    struct pthread_args args_one, args_two;
    args_one.sk = &pktsk.sk;
    args_one.type_flags = 3;
    args_one.thread_id = 1;

    args_two.sk = &pktsk.sk;
    args_two.type_flags = 3;
    args_two.thread_id = 2;

    fanout_mutex = new Resources::SynchronizedResource();

    ControlledTask<void> t1([&args_one] { try { thread_func(&args_one); } catch (std::exception& e) { std::cout << e.what() << std::endl; } });
    ControlledTask<void> t2([&args_two] { try { thread_func(&args_two); } catch (std::exception& e) { std::cout << e.what() << std::endl; } });
    
    t1.start();
    t2.start();
    t1.wait();
    t2.wait();

    delete fanout_mutex;
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
        settings.with_prioritization_strategy();
        SystematicTestEngineContext context(settings, 10000);
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
    */
    std::vector<int> bugs_found;
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
        std::cout << "PCT Strategy with depth " << std::dec << depths[i-1] << ": " << std::dec << static_cast<int>(bugs_found[i]) << std::endl;
    }
}
