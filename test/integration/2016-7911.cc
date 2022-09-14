#include <stdio.h>
//#include <pthread.h>
//#include <unistd.h>
#include <time.h>


#include "test.h"
#include "controlled_task.h"
#include "systematic_testing_resources.h"
// #define TEST_TIME

typedef struct {
	int counter;
} atomic_t;

#define spinlock_t Resources::SynchronizedResource*

struct io_context {
	atomic_t nr_tasks;
	unsigned short ioprio;
};

struct task_struct {
	struct io_context *io_context;
	spinlock_t alloc_lock;
};

#define IOPRIO_CLASS_SHIFT	(13)
#define IOPRIO_PRIO_VALUE(class, data)	(((class) << IOPRIO_CLASS_SHIFT) | data)
enum {
	IOPRIO_CLASS_NONE,
	IOPRIO_CLASS_RT,
	IOPRIO_CLASS_BE,
	IOPRIO_CLASS_IDLE,
};
#define IOPRIO_NORM	(4)

static inline void task_lock(struct task_struct *p)
{
	p->alloc_lock->acquire();
}

static inline void task_unlock(struct task_struct *p)
{
	p->alloc_lock->release();
}

static inline void atomic_dec(atomic_t * v){
	(v->counter)--;
}

int security_task_getioprio(struct task_struct *p)
{
	return 0;
}

void put_io_context_active(struct io_context *ioc)
{
}

static int get_task_ioprio(struct task_struct *p)
{
	int ret;

	ret = security_task_getioprio(p);
	if (ret)
		goto out;
	ret = IOPRIO_PRIO_VALUE(IOPRIO_CLASS_NONE, IOPRIO_NORM);
	//std::cout << p->io_context << std::endl;
	if (p->io_context)
	{
		auto test_engine = GetTestEngine();
		test_engine->schedule_next_operation();

		//std::cout << p->io_context << std::endl;
		if(p->io_context == NULL)
		{
			test_engine->notify_assertion_failure("NULL ptr dereference");
			return ret;
		}
		ret = p->io_context->ioprio;
		puts("after use");
	}
out:
	return ret;
}

void exit_io_context(struct task_struct *task)
{
	struct io_context *ioc;

	task_lock(task);
	ioc = task->io_context;
	task->io_context = NULL;
	puts("NULL");
	task_unlock(task);

	atomic_dec(&ioc->nr_tasks);
	put_io_context_active(ioc);
}

void thread_one(void* args){
	struct task_struct *p = (task_struct *)args;
	get_task_ioprio(p);
	puts("exit thread 1");
}

void thread_two(void* args){
	struct task_struct *p = (task_struct *)args;
	exit_io_context(p);
	puts("exit thread 2");
}


void run_iteration() 
{
    struct task_struct task_test;
	struct io_context ioc_test;

    ioc_test.ioprio = 1;
	task_test.alloc_lock = new Resources::SynchronizedResource();
	task_test.io_context = &ioc_test;

    ControlledTask<void> t1([&task_test] { thread_one(&task_test); });
    ControlledTask<void> t2([&task_test] { thread_two(&task_test); });

	t1.start();
    t2.start();

    t1.wait();
    t2.wait();
    printf("\nprogram-successful-exit\n");
}

int main(){
/*
#ifdef TEST_TIME
	static double run_time_begin;
	static double run_time_end;
	static double run_time_total;
	run_time_begin = clock();
#endif
	struct task_struct task_test;
	struct io_context ioc_test;

    ioc_test.ioprio = 1;
	pthread_mutex_init(&(task_test.alloc_lock), NULL);
	task_test.io_context = &ioc_test;

	pthread_t t1, t2;
	pthread_create(&t1, NULL, thread_one, &task_test);
	pthread_create(&t2, NULL, thread_two, &task_test);

	pthread_join(t1, NULL);
	pthread_join(t2, NULL);
    printf("\nprogram-successful-exit\n");
#ifdef TEST_TIME
    run_time_end = clock();
    run_time_total = run_time_end - run_time_begin;
    printf("test-the-total-time: %.3lf\n", (double)(run_time_total/CLOCKS_PER_SEC)*1000);
#endif
	return 0;
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
            try
            {
                run_iteration();
            }
            catch(const std::exception& e)
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
}