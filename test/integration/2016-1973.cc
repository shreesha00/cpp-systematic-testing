#include <stdio.h>
//#include <pthread.h>
//#include <assert.h>
//#include <unistd.h>
#include <atomic>
#include <map>
#include <time.h>

#include "test.h"
#include "controlled_task.h"
#include "systematic_testing_resources.h"
#include "malloc_wrapper.h"
#include "racy_variable.h"
// #define SLEEP_FOR_RACE
// #define TEST_TIME

namespace webrtc {

enum CountOperation {
	kRelease,
	kAddRef,
	kAddRefNoCreate
};

enum CreateOperation {
	kInstanceExists,
	kCreate,
	kDestroy
};

/*
long _InterlockedIncrement(std::atomic_long volatile * ref){
	return std::atomic_fetch_add(ref, (long)1) + 1;
}

long _InterlockedDecrement(std::atomic_long volatile * ref){
	return std::atomic_fetch_sub(ref, (long)1) - 1;
}
*/

long _InterlockedIncrement(std::atomic_long volatile * ref){
	GetTestEngine()->schedule_next_operation();
	return std::atomic_fetch_add(ref, (long)1) + 1;
}

long _InterlockedDecrement(std::atomic_long volatile * ref){
	GetTestEngine()->schedule_next_operation();
	return std::atomic_fetch_sub(ref, (long)1) - 1;
}

template <class T>
inline T InterlockedExchangePointer(T volatile* target, T value) {
	GetTestEngine()->schedule_next_operation();
	return std::atomic_exchange((std::atomic<T>*)target, value);
}

#define InterlockedIncrement _InterlockedIncrement
#define InterlockedDecrement _InterlockedDecrement

template <class T>
static T* GetStaticInstance(CountOperation count_operation, int thread_num) {
    auto test_engine = GetTestEngine();
	static volatile std::atomic_long instance_count(0);
	static T* volatile instance = NULL;

	/* reset if thread_num = 0 */
	if (thread_num == 0)
	{
		instance = NULL;
		instance_count.store(0);
		return NULL;
	}
    
	CreateOperation state = kInstanceExists;
	if (count_operation == kAddRefNoCreate && instance_count == 0) {
    	return NULL;
	}
  	if (count_operation == kAddRefNoCreate) {
    	if (1 == InterlockedIncrement(&instance_count)) {
      		InterlockedDecrement(&instance_count);
      		//assert(false);
      		return NULL;
    	}
    	if (instance == NULL) {
      		//assert(false);
      		InterlockedDecrement(&instance_count);
      		return NULL;
    	}
  	} else if (count_operation == kAddRef) {
  		printf("thread %d: kAddRef\n", thread_num);
    	if (instance_count == 0) {
      		state = kCreate;
    		printf("thread %d: count = 0, state = kCreate\n", thread_num);
    	} else {
    		printf("thread %d: count != 0\n", thread_num);
      		if (1 == InterlockedIncrement(&instance_count)) {
      			printf("thread %d: count++, count == 1\n", thread_num);
        		InterlockedDecrement(&instance_count);
        		state = kCreate;
        		printf("thread %d: count--, state = kCreate\n", thread_num);
      		}
    	}
  	} else {
    	int new_value = InterlockedDecrement(&instance_count);
  		printf("thread %d: count--, new_value = %d\n", thread_num, new_value);
    	if (new_value == 0) {
      		state = kDestroy;
  			printf("thread %d: set state = kDestroy\n", thread_num);
    	}
  	}
  	if (state == kCreate) {
  		printf("thread %d: kCreate\n", thread_num);
    	T* new_instance = T::CreateInstance();
    	printf("thread %d: new_instance\n", thread_num);
#ifdef SLEEP_FOR_RACE
    	if(thread_num == 1)
			sleep(3);
		else sleep(4);
#endif
    	if (1 == InterlockedIncrement(&instance_count)) {
    		printf("thread %d: count++, count == 1\n", thread_num);
      		InterlockedExchangePointer((T**)(&instance), new_instance);
      		printf("thread %d: exchange new_instance, instance\n", thread_num);
    	} else {
      		InterlockedDecrement(&instance_count);
    		printf("thread %d: count--\n", thread_num);
      		if (new_instance) {
        		free_safe(static_cast<T*>(new_instance));
      			printf("thread %d: delete new_instance\n", thread_num);
      		}
    	}
  	} else if (state == kDestroy) {
  		printf("thread %d: kDestroy\n", thread_num);
    	T* old_value = static_cast<T*>(InterlockedExchangePointer((T**)(&instance), (T*)NULL));
    	printf("thread %d: exchange, instance, NULL\n", thread_num);
    	if (old_value) {
    		printf("thread %d: old_value = %p\n", thread_num, old_value);
      		free_safe(static_cast<T*>(old_value));
    		printf("thread %d: delete old_value\n", thread_num);
    	}
    	return NULL;
  	}

  	return instance;
}

class SSRCDatabase{
public:
    static SSRCDatabase* GetSSRCDatabase(int thread_num);
    static void ReturnSSRCDatabase(int thread_num);
    std::map<uint32_t, uint32_t>  _ssrcMap;
protected:
	SSRCDatabase();
	static SSRCDatabase* CreateInstance() { return new(malloc_safe(sizeof(SSRCDatabase))) SSRCDatabase(); }
private:
	friend SSRCDatabase* GetStaticInstance<SSRCDatabase>(CountOperation count_operation, int thread_num);
    static SSRCDatabase* StaticInstance(CountOperation count_operation, int thread_num);
};

SSRCDatabase* SSRCDatabase::StaticInstance(CountOperation count_operation, int thread_num){
  SSRCDatabase* impl = GetStaticInstance<SSRCDatabase>(count_operation, thread_num);
  return impl;
}

SSRCDatabase* SSRCDatabase::GetSSRCDatabase(int thread_num){
    return StaticInstance(kAddRef, thread_num);
}

void SSRCDatabase::ReturnSSRCDatabase(int thread_num){
    StaticInstance(kRelease, thread_num);
}

SSRCDatabase::SSRCDatabase(){}
}

int accessMap(RacyPointer<webrtc::SSRCDatabase> ssrcdb){
	return ssrcdb->_ssrcMap.size();
}

void thread_one(void* args){
    auto test_engine = GetTestEngine();
	RacyPointer<webrtc::SSRCDatabase> ssrcdb(webrtc::SSRCDatabase::GetSSRCDatabase(1));
	printf("thread 1: ssrcdb = %p\n", ssrcdb);
	accessMap(ssrcdb);
#ifdef SLEEP_FOR_RACE
	sleep(5);
#endif
	webrtc::SSRCDatabase::ReturnSSRCDatabase(1);
}

void thread_two(void* args){
    auto test_engine = GetTestEngine();
#ifdef SLEEP_FOR_RACE
	sleep(2);
#endif
	RacyPointer<webrtc::SSRCDatabase> ssrcdb(webrtc::SSRCDatabase::GetSSRCDatabase(2));
	printf("thread 2: ssrcdb = %p\n", ssrcdb);
#ifdef SLEEP_FOR_RACE
	sleep(6);
#endif
	accessMap(ssrcdb);
	printf("thread 2: use ssrcdb = %p\n", ssrcdb);
}


void run_iteration()
{
#ifdef TEST_TIME
    static double run_time_begin;
    static double run_time_end;
    static double run_time_total;
    run_time_begin = clock();
#endif

    void* args = NULL;
	webrtc::SSRCDatabase::GetSSRCDatabase(0);
    ControlledTask<void> t2([&args] { try { thread_two(args); } catch (std::exception& e) { std::cout << e.what() << std::endl; } });
    ControlledTask<void> t1([&args] { try { thread_one(args); } catch (std::exception& e) { std::cout << e.what() << std::endl; } });

    t2.start();
    t1.start();

	t2.wait();
    t1.wait();

    printf("\nprogram-successful-exit\n");
#ifdef TEST_TIME
    run_time_end = clock();
    run_time_total = run_time_end - run_time_begin;
    printf("test-the-total-time: %.3lf\n", (double)(run_time_total/CLOCKS_PER_SEC)*1000);
#endif
}

int main()
{
    std::cout << "[test] started." << std::endl;
    auto start_time = std::chrono::steady_clock::now();

    try
    {
        auto settings = CreateDefaultSettings();
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
