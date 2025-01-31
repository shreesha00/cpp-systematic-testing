#include <stdio.h>
//#include <pthread.h>
#include <malloc.h>
#include <time.h>

#include "test.h"
#include "controlled_task.h"
#include "systematic_testing_resources.h"
#include "racy_variable.h" 
#include "malloc_wrapper.h"
#include "null_safe_ptr.h"
// #define TEST_TIME

typedef __signed__ int __s32;
typedef unsigned int __u32;

typedef		__s32		int32_t;
typedef		__u32		uint32_t;

typedef int32_t key_serial_t;
typedef uint32_t key_perm_t;
typedef struct __key_reference_with_attributes *key_ref_t;

#define KEY_SPEC_USER_SESSION_KEYRING	-5
#define KEY_LOOKUP_FOR_UNLINK	0x04
#define KEY_LOOKUP_PARTIAL	0x02
#define KEY_POS_ALL	0x3f000000
#define KEY_POS_SETATTR	0x20000000
#define KEY_USR_ALL	0x003f0000
#define KEY_FLAG_INSTANTIATED	0

#define	ENOKEY		132
#define	EIO		 5
#define ERESTARTSYS	512

typedef struct {
	int counter;
} atomic_t;

struct key {
	atomic_t		usage;
	unsigned long		flags;
};

struct user_struct {
	struct key *uid_keyring;
	struct key *session_keyring;
};

struct cred {
	RacyPointer<struct user_struct> user;
};

struct pthread_args{
	key_serial_t id;
	unsigned long lflags;
	key_perm_t perm;
};

Resources::SynchronizedResource* key_user_keyring_mutex;
struct cred cred_test;
struct user_struct user_test;

static inline void * ERR_PTR(long error)
{
	return (void *) error;
}

static inline key_ref_t make_key_ref(const struct key *key,
				     unsigned long possession)
{
	return (key_ref_t) ((unsigned long) key | possession);
}

static inline int
test_bit(int nr, const volatile void * addr)
{
	return (1UL & (((const int *) addr)[nr >> 5] >> (nr & 31))) != 0UL;
}

static struct cred * current_cred(){
	return &cred_test;
}

static struct cred * get_current_cred(){
	return &cred_test;
}

void mutex_lock(Resources::SynchronizedResource* lock){
	lock->acquire();
}

void mutex_unlock(Resources::SynchronizedResource* lock){
	lock->release();
}

static void atomic_inc(atomic_t * v){
	(v->counter)++;
}

int wait_for_key_construction(struct key *key, bool intr){return 0;}
int key_validate(const struct key *key){return 0;}
int key_task_permission(const key_ref_t key_ref, const struct cred *cred, key_perm_t perm){return 0;}
static inline void put_cred(const struct cred *_cred){}
static inline void key_ref_put(key_ref_t key_ref){}
void key_put(struct key *key){}

int install_user_keyrings(int thread_num)
{
	RacyPointer<struct user_struct> user;
	const struct cred *cred;
	struct key *uid_keyring, *session_keyring;
	key_perm_t user_keyring_perm;
	int ret;

	user_keyring_perm = (KEY_POS_ALL & ~KEY_POS_SETATTR) | KEY_USR_ALL;
	cred = current_cred();
	user = cred->user;

	if (user->uid_keyring) {
		printf("thread %d: has uid_keyring\n", thread_num);
		return 0;
	}

	mutex_lock(key_user_keyring_mutex);
	ret = 0;

	if (!user->uid_keyring) {
		printf("thread %d: ready to malloc\n", thread_num);

		uid_keyring = (key *)malloc(sizeof (struct key));
		session_keyring = (key *)malloc(sizeof (struct key));

		printf("thread %d: malloc\n", thread_num);

		user->uid_keyring = uid_keyring;
		user->session_keyring = session_keyring;
	}

	mutex_unlock(key_user_keyring_mutex);
	printf("thread %d: install_user_keyrings done\n", thread_num);
	return 0;

error_release_both:
	printf("thread %d: enter label error_release_both\n", thread_num);
	key_put(session_keyring);
error_release:
	printf("thread %d: enter label error_release\n", thread_num);
	key_put(uid_keyring);
error:
	printf("thread %d: enter label error\n", thread_num);
	mutex_unlock(key_user_keyring_mutex);
	return ret;
}

key_ref_t lookup_user_key(key_serial_t id, unsigned long lflags,
			  key_perm_t perm)
{
	const struct cred *cred;
	NullSafePtr<struct key> key;
	key_ref_t key_ref, skey_ref;
	int ret;

try_again:
	printf("thread 1: enter label try_again\n");
	cred = get_current_cred();
	key_ref = (key_ref_t)ERR_PTR(-ENOKEY);

	switch (id) {

	case KEY_SPEC_USER_SESSION_KEYRING:
		if (!cred->user->session_keyring) {
			printf("thread 1: session_keyring not exist\n");
			ret = install_user_keyrings(1);
			if (ret < 0)
				goto error;
		}

		key = cred->user->session_keyring;
		atomic_inc(&key->usage);
		printf("thread 1: dereference key\n");
		key_ref = make_key_ref(key, 1);
		printf("thread 1: case finish\n");
		break;

	default:
		printf("thread 1: enter default\n");
		break;
	}

	if (lflags & KEY_LOOKUP_FOR_UNLINK) {
		printf("thread 1: enter if 1\n");
		ret = 0;
		goto error;
	}

	if (!(lflags & KEY_LOOKUP_PARTIAL)) {
		printf("thread 1: enter if 2\n");
		ret = wait_for_key_construction(key, true);
		switch (ret) {
		case -ERESTARTSYS:
			printf("thread 1: enter case -ERESTARTSYS\n");
			goto invalid_key;
		default:
			printf("thread 1: enter case default\n");
			if (perm){
				printf("thread 1: perm = 1\n");
				goto invalid_key;
			}
		case 0:
			printf("thread 1: enter case 0\n");
			break;
		}
	} else if (perm) {
		printf("thread 1: enter if 3\n");
		ret = key_validate(key);
		if (ret < 0)
			goto invalid_key;
	}

	ret = -EIO;
	if (!(lflags & KEY_LOOKUP_PARTIAL) &&
	    !test_bit(KEY_FLAG_INSTANTIATED, &key->flags)){
		printf("thread 1: enter if 4\n");
		goto invalid_key;
	}

	ret = key_task_permission(key_ref, cred, perm);
	if (ret < 0)
		goto invalid_key;

error:
	printf("thread 1: enter label error\n");
	put_cred(cred);
	return key_ref;

invalid_key:
	printf("thread 1: enter label invalid_key\n");
	key_ref_put(key_ref);
	key_ref = (key_ref_t)ERR_PTR(ret);
	goto error;

reget_creds:
	printf("thread 1: enter label reget_creds\n");
	put_cred(cred);
	goto try_again;
}

void thread_one(void* args){
	key_serial_t id = ((pthread_args *)args)->id;
	unsigned long lflags = ((pthread_args *)args)->lflags;
	key_perm_t perm = ((pthread_args *)args)->perm;

	lookup_user_key(id, lflags, perm);
}

void thread_two(void* args){
	install_user_keyrings(2);
}


void run_iteration()
{
#ifdef TEST_TIME
    static double run_time_begin;
    static double run_time_end;
    static double run_time_total;
    run_time_begin = clock();
#endif
	cred_test.user = &user_test;
	key_user_keyring_mutex = new Resources::SynchronizedResource();


	struct pthread_args arg_one;
	arg_one.id = KEY_SPEC_USER_SESSION_KEYRING;
	arg_one.lflags = 4;
	arg_one.perm = 1;

    user_test.uid_keyring = NULL;
    user_test.session_keyring = NULL;
    struct pthread_args* arg_two = NULL;
    ControlledTask<void> t1([&arg_one] { try { thread_one(&arg_one); } catch (std::exception& e) { std::cout << e.what() << std::endl; }});
    ControlledTask<void> t2([&arg_two] { try { thread_two(arg_two); } catch (std::exception& e) { std::cout << e.what() << std::endl; }});
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

int main(){

    /*
	std::cout << "[test] started." << std::endl;
    auto start_time = std::chrono::steady_clock::now();

    try
    {
        auto settings = CreateDefaultSettings();
        //settings.with_resource_race_checking_enabled(true);
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