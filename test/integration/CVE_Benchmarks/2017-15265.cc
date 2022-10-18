//#include <pthread.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
//#include <unistd.h>
#include <time.h>

#include "test.h"
#include "controlled_task.h"
#include "systematic_testing_resources.h"
#include "racy_variable.h"
#include "malloc_wrapper.h"
//#define MUTEX_FOR_CORRECT_EXE_SEQUENCE
#ifdef MUTEX_FOR_CORRECT_EXE_SEQUENCE
pthread_mutex_t lock_for_correct_exe;
#endif

// #define TEST_TIME

#define SNDRV_SEQ_PORT_FLG_GIVEN_PORT	(1<<0)

#define SNDRV_SEQ_EVENT_PORT_START	63

#define	EPERM		 1
#define	ENOMEM		12
#define	ENOENT		 2

struct list_head {
    struct list_head *next, *prev;
};

struct snd_seq_addr {
	unsigned char client;
	unsigned char port;
};

struct snd_seq_client {
	int number;
	int num_ports;
	struct list_head ports_list_head;
	Resources::SynchronizedResource* ports_mutex;
	Resources::SynchronizedResource* ports_lock;
};


struct snd_seq_port_info {
	struct snd_seq_addr addr;
	unsigned int type;
	unsigned int flags;
};

struct snd_seq_client_port {
	struct snd_seq_addr addr;
	struct list_head list;
	unsigned int type;
	Resources::SynchronizedResource* use_lock;
};

struct thread_args{
	snd_seq_client* clt;
	snd_seq_port_info* info;
};

#define container_of(ptr, type, member) \
    (type *)((char *)(ptr) - (char *) &((type *)0)->member)

#define __container_of(ptr, sample, member)				\
    container_of((ptr), typeof(*(sample)), member)

#define list_for_each_entry(pos, head, member)				\
    for (pos = __container_of((head)->next, pos, member);		\
	 &pos->member != (head);					\
	 pos = __container_of(pos->member.next, pos, member))

static inline void
__list_add(struct list_head *entry,
                struct list_head *prev, struct list_head *next)
{
    next->prev = entry;
    entry->next = next;
    entry->prev = prev;
    prev->next = entry;
}

static inline void
list_add_tail(struct list_head *entry, struct list_head *head)
{
    __list_add(entry, head->prev, head);
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
		free(p);
}

#define snd_seq_system_client_ev_port_start(client, port) snd_seq_system_broadcast(client, port, SNDRV_SEQ_EVENT_PORT_START)

void snd_seq_system_broadcast(int client, int port, int type)
{

}

int snd_seq_set_port_info(RacyPointer<struct snd_seq_client_port> port,
			  struct snd_seq_port_info * info)
{
	port->type = info->type;
	printf("write, port = %p\n", port.read());

	return 0;
}

RacyPointer<struct snd_seq_client_port>snd_seq_create_port(struct snd_seq_client *client,
						int port)
{
	RacyPointer<struct snd_seq_client_port>new_port, p;
	int num = -1;

	new_port = (snd_seq_client_port*)malloc_safe(sizeof(*new_port));
	if (!new_port)
	{
		new_port = NULL;
		return new_port;
	}
	new_port->addr.client = client->number;
	new_port->addr.port = -1;

	new_port->use_lock = new Resources::SynchronizedResource();

	num = port >= 0 ? port : 0;

	client->ports_mutex->acquire();
	client->ports_lock->acquire();
	list_for_each_entry(p, &client->ports_list_head, list) {

		if (p->addr.port > num)
			break;
		if (port < 0)
			num = p->addr.port + 1;
	}
	list_add_tail(&new_port->list, &p->list);
	client->num_ports++;
	new_port->addr.port = num;
	client->ports_lock->release();
	client->ports_mutex->release();

	return new_port;
}

static int snd_seq_ioctl_create_port(struct snd_seq_client *client, void *arg)
{
	struct snd_seq_port_info *info = (snd_seq_port_info*)arg;
	RacyPointer<struct snd_seq_client_port>port;

	if (info->addr.client != client->number)
		return -EPERM;

	port = snd_seq_create_port(client, (info->flags & SNDRV_SEQ_PORT_FLG_GIVEN_PORT) ? info->addr.port : -1);
	if (port == NULL)
		return -ENOMEM;
	printf("create, port = %p\n", port.read());

	printf("use, port = %p\n", port.read());
	snd_seq_set_port_info(port, info);
	snd_seq_system_client_ev_port_start(port->addr.client, port->addr.port);

# ifdef MUTEX_FOR_CORRECT_EXE_SEQUENCE
	lock_for_correct_exe->release();
#endif

	return 0;
}

static inline void
__list_del(struct list_head *prev, struct list_head *next)
{
    next->prev = prev;
    prev->next = next;
}

static inline void
list_del(struct list_head *entry)
{
    __list_del(entry->prev, entry->next);
}

static int port_delete(struct snd_seq_client *client,
		       RacyPointer<struct snd_seq_client_port>port)
{
	port->use_lock->acquire();
	free_safe(port);
	printf("delete, port = %p\n", port.read());
	return 0;
}

int snd_seq_delete_port(struct snd_seq_client *client, int port)
{
	RacyPointer<struct snd_seq_client_port> found, p;
	found = NULL;

	client->ports_mutex->acquire();
	client->ports_lock->acquire();
	list_for_each_entry(p, &client->ports_list_head, list) {
	    printf("found  %d,port =  %d\n", p->addr.port, port);
		if (p->addr.port == port) {
			list_del(&p->list);
			client->num_ports--;
			found = p;
			printf("found = %p\n", found);
			break;
		}
	}
	client->ports_lock->release();
	client->ports_mutex->release();
	if (found)
		return port_delete(client, found);
	else
		return -ENOENT;
}

void* thread_one(void* args){
	snd_seq_client* clt = ((thread_args*)args)->clt;
	snd_seq_port_info* info = ((thread_args*)args)->info;
    snd_seq_ioctl_create_port(clt, info);
}

void* thread_two(void* args){
#ifdef MUTEX_FOR_CORRECT_EXE_SEQUENCE
	lock_for_correct_exe->acquire();
#else
	/*sleep(1)*/;
#endif
	snd_seq_client* clt = (snd_seq_client*)args;
    snd_seq_delete_port(clt, 1);
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

	struct snd_seq_client clt_test;
	struct snd_seq_client_port port_test;
	struct snd_seq_port_info info_test;

	port_test.addr.client = 1;
	port_test.addr.port = 0;
	port_test.list.next = &(clt_test.ports_list_head);
	port_test.list.prev = &(clt_test.ports_list_head);
    port_test.use_lock = new Resources::SynchronizedResource();

	clt_test.number = 1;
	clt_test.num_ports = 1;
	clt_test.ports_list_head.next = &(port_test.list);
	clt_test.ports_list_head.prev = &(port_test.list);
	clt_test.ports_mutex = new Resources::SynchronizedResource();
 	clt_test.ports_lock = new Resources::SynchronizedResource();

	info_test.addr.client = 1;
	info_test.addr.port = 1;
	info_test.flags = 1;


	struct thread_args args;
	args.clt = &clt_test;
	args.info = &info_test;
	ControlledTask<void> t1([&args] { try { thread_one(&args); } catch (std::exception& e) { std::cout << e.what() << std::endl; } } );
	ControlledTask<void> t2([&clt_test] { try { thread_two(&clt_test); } catch (std::exception& e) { std::cout << e.what() << std::endl; } } );
    t1.start();
	t2.start();
	t1.wait();
	t2.wait();

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
			settings.with_resource_race_checking_enabled(true);
            settings.with_prioritization_strategy(d);
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
