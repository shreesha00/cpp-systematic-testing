#include <stdio.h>
//#include <pthread.h>
//#include <unistd.h>
#include <time.h>

#include "test.h"
#include "controlled_task.h"
#include "systematic_testing_resources.h"
#include "racy_variable.h"
#include "null_safe_ptr.h"
// #define TEST_TIME

#define POISON_POINTER_DELTA 0
#define GOLDEN_RATIO_PRIME_32 0x9e370001UL
#define MMF_VM_MERGEABLE	16
#define LIST_POISON1  ((void *) (0x00100100 + POISON_POINTER_DELTA))
#define LIST_POISON2  ((void *) (0x00200200 + POISON_POINTER_DELTA))

typedef Resources::SynchronizedResource* spinlock_t;
typedef unsigned int u32;
typedef __signed__ int __s32;

struct list_head {
	RacyPointer<list_head> next, prev;
};

struct hlist_node {
	struct hlist_node *next, **pprev;
};

struct hlist_head {
	struct hlist_node *first;
};

struct rmap_item {
	struct rmap_item *rmap_list;
};

struct rw_semaphore {
	__s32			activity;
	spinlock_t		wait_lock;
	struct list_head	wait_list;
};

struct mm_struct {
	struct rw_semaphore mmap_sem;
	unsigned long flags;
};

struct mm_slot {
	struct hlist_node link;
	struct list_head mm_list;
	struct rmap_item *rmap_list;
	struct mm_struct *mm;
};

struct ksm_scan {
	struct mm_slot *mm_slot;
	unsigned long address;
	struct rmap_item **rmap_list;
	unsigned long seqnr;
};

spinlock_t ksm_mmlist_lock;

#define MM_SLOTS_HASH_SHIFT 10
#define MM_SLOTS_HASH_HEADS (1 << MM_SLOTS_HASH_SHIFT)
static struct hlist_head mm_slots_hash[MM_SLOTS_HASH_HEADS];

static struct mm_slot ksm_mm_head;

#define LIST_HEAD_INIT(name) { RacyPointer<list_head>(&(name)), RacyPointer<list_head>(&(name)) }
// static struct mm_slot ksm_mm_head = {
	// .mm_list = LIST_HEAD_INIT(ksm_mm_head.mm_list)
// };

static struct ksm_scan ksm_scan = {
	.mm_slot = &ksm_mm_head
};

#define spin_lock(lock) lock->acquire();

#define spin_unlock(lock) lock->release();

#define typeof(a) __typeof__(a)

static inline int list_empty(const struct list_head *head)
{
	return head->next == head;
}

#define list_entry(link, type, member) \
	((type *)((char *)(link)-(unsigned long)(&((type *)0)->member)))

static inline void __list_add(struct list_head *neww,
			      struct list_head *prev, struct list_head *next)
{
	next->prev = neww;
	neww->next = next;
	neww->prev = prev;
	prev->next = neww;
}

static inline void __list_del(struct list_head *prev, struct list_head *next)
{
	next->prev = prev;
	prev->next = next;
}

static inline void list_add(struct list_head *neww, struct list_head *head)
{
	__list_add(neww, head, head->next);
}

static inline void list_del(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	entry->next = (list_head *)LIST_POISON1;
	entry->prev = (list_head *)LIST_POISON2;
}

static inline void list_move(struct list_head *list, struct list_head *head)
{
	__list_del(list->prev, list->next);
	list_add(list, head);
}

static inline void hlist_add_head(struct hlist_node *n, struct hlist_head *h)
{
	struct hlist_node *first = h->first;
	n->next = first;
	if (first)
		first->pprev = &n->next;
	h->first = n;
	n->pprev = &h->first;
}

static inline void __hlist_del(struct hlist_node *n)
{
	struct hlist_node *next = n->next;
	struct hlist_node **pprev = n->pprev;
	*pprev = next;
	if (next)
		next->pprev = pprev;
}

static inline void hlist_del(struct hlist_node *n)
{
	__hlist_del(n);
	n->next = (hlist_node *)LIST_POISON1;
	n->pprev = (hlist_node **)LIST_POISON2;
}

#define container_of(ptr, type, member) \
    (type *)((char *)(ptr) - (char *) &((type *)0)->member)

#define hlist_entry(ptr, type, member) container_of(ptr,type,member)

#define hlist_for_each_entry(tpos, pos, head, member)			 \
	for (pos = (head)->first;					 \
	     pos &&							 \
		({ tpos = hlist_entry(pos, typeof(*tpos), member); 1;}); \
	     pos = pos->next)

#define hash_long(val, bits) hash_32(val, bits)

static inline u32 hash_32(u32 val, unsigned int bits)
{
	u32 hash = val * GOLDEN_RATIO_PRIME_32;
	return hash >> (32 - bits);
}

static inline unsigned long hash_ptr(void *ptr, unsigned int bits)
{
	return hash_long((unsigned long)ptr, bits);
}

void down_read(struct rw_semaphore *sem){}
void down_write(struct rw_semaphore *sem){}
void up_read(struct rw_semaphore *sem){}
void up_write(struct rw_semaphore *sem){}
static inline void mmdrop(struct mm_struct * mm){}
static inline void clear_bit(unsigned long nr, volatile void * addr){}
static inline void free_mm_slot(struct mm_slot *mm_slot){}

static void insert_to_mm_slots_hash(struct mm_struct *mm,
				    struct mm_slot *mm_slot)
{
	struct hlist_head *bucket;

	bucket = &mm_slots_hash[hash_ptr(mm, MM_SLOTS_HASH_SHIFT)];
	mm_slot->mm = mm;
	hlist_add_head(&mm_slot->link, bucket);
}

static struct mm_slot *get_mm_slot(struct mm_struct *mm)
{
	struct mm_slot *mm_slot;
	struct hlist_head *bucket;
	struct hlist_node *node;

	bucket = &mm_slots_hash[hash_ptr(mm, MM_SLOTS_HASH_SHIFT)];
	hlist_for_each_entry(mm_slot, node, bucket, link) {
		if (mm == mm_slot->mm)
			return mm_slot;
	}
	return NULL;
}

static struct rmap_item *scan_get_next_rmap_item()
{
	NullSafePtr<struct mm_struct> mm;
	struct mm_slot *slot;

	if (list_empty(&ksm_mm_head.mm_list))
		return NULL;

	slot = ksm_scan.mm_slot;
	if (slot == &ksm_mm_head) {
		spin_lock(ksm_mmlist_lock);
		slot = list_entry(slot->mm_list.next.read(), struct mm_slot, mm_list);
		ksm_scan.mm_slot = slot;
		spin_unlock(ksm_mmlist_lock);
next_mm:
		ksm_scan.address = 0;
		ksm_scan.rmap_list = &slot->rmap_list;
	}

	mm = slot->mm;
	printf("thread 1, slot->mm = %p\n", mm);
	down_read(&mm->mmap_sem);
	printf("thread 1, mm->mmap_sem = %p\n", &mm->mmap_sem);

	spin_lock(ksm_mmlist_lock);
	ksm_scan.mm_slot = list_entry(slot->mm_list.next.read(),
						struct mm_slot, mm_list);

	if (ksm_scan.address == 0) {
		printf("thread 1, ksm_scan.address = 0\n");

		// hlist_del(&slot->link);
		// list_del(&slot->mm_list);

		spin_unlock(ksm_mmlist_lock);

		free_mm_slot(slot);
		clear_bit(MMF_VM_MERGEABLE, &mm->flags);
		up_read(&mm->mmap_sem);
		mmdrop(mm);
	} else {
		spin_unlock(ksm_mmlist_lock);
		up_read(&mm->mmap_sem);
	}

	slot = ksm_scan.mm_slot;
	if (slot != &ksm_mm_head)
		goto next_mm;

	ksm_scan.seqnr++;
	return NULL;
}

void __ksm_exit(struct mm_struct *mm)
{
	struct mm_slot *mm_slot;
	int easy_to_free = 0;

	spin_lock(ksm_mmlist_lock);
	mm_slot = get_mm_slot(mm);
	printf("thread 2, mm_slot = %p, mm_slot.mm_list = %p, ksm_scan.mm_slot = %p\n", mm_slot, &mm_slot->mm_list, ksm_scan.mm_slot);
	if (mm_slot && ksm_scan.mm_slot != mm_slot) {
		if (!mm_slot->rmap_list) {
			hlist_del(&mm_slot->link);
			list_del(&mm_slot->mm_list);
			easy_to_free = 1;
			printf("thread 2, delete\n");
		} else {
			printf("thread 2, else\n");
			list_move(&mm_slot->mm_list,
				  &ksm_scan.mm_slot->mm_list);
		}
	}
	spin_unlock(ksm_mmlist_lock);

	if (easy_to_free) {
		printf("thread 2, if\n");
		free_mm_slot(mm_slot);
		clear_bit(MMF_VM_MERGEABLE, &mm->flags);
		mmdrop(mm);
	} else if (mm_slot) {
		printf("thread 2, else if\n");
		down_write(&mm->mmap_sem);
		up_write(&mm->mmap_sem);
	}
}

void * thread_one(void *args){
	scan_get_next_rmap_item();
}

void * thread_two(void *args){
	__ksm_exit((mm_struct *)args);
}

void run_iteration()
{
#ifdef TEST_TIME
    static double run_time_begin;
    static double run_time_end;
    static double run_time_total;
    run_time_begin = clock();
#endif
	ksm_mm_head.mm_list = LIST_HEAD_INIT(ksm_mm_head.mm_list);
	printf("thread main, ksm_mm_head.mm_list = %p, ksm_mm_head.mm_list.prev = %p, ksm_mm_head.mm_list.next = %p\n", &ksm_mm_head.mm_list, ksm_mm_head.mm_list.prev, ksm_mm_head.mm_list.next);

	struct mm_struct mm_test;
	struct mm_slot slot_test;
	slot_test.rmap_list = NULL;
	insert_to_mm_slots_hash(&mm_test, &slot_test);
	list_add(&slot_test.mm_list, &ksm_scan.mm_slot->mm_list);
	printf("thread main, ksm_mm_head.mm_list = %p, ksm_mm_head.mm_list.prev = %p, ksm_mm_head.mm_list.next = %p\n", &ksm_mm_head.mm_list, ksm_mm_head.mm_list.prev, ksm_mm_head.mm_list.next);
	printf("thread main, slot_test.mm = %p, mm_test = %p\n", slot_test.mm, &mm_test);

	ksm_mmlist_lock = new Resources::SynchronizedResource();

	void* args1 = NULL;
	ControlledTask<void> t1([&args1] { try { thread_one(args1); } catch (std::exception& e) { std::cout << e.what() << std::endl; } });
	ControlledTask<void> t2([&mm_test] { try { thread_two(&mm_test); } catch (std::exception& e) { std::cout << e.what() << std::endl; }});
	t1.start();
	t2.start();

	t1.wait();
	t2.wait();

	printf("thread main, ksm_mm_head.mm_list = %p, ksm_mm_head.mm_list.prev = %p, ksm_mm_head.mm_list.next = %p\n", &ksm_mm_head.mm_list, ksm_mm_head.mm_list.prev, ksm_mm_head.mm_list.next);

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
        //settings.with_resource_race_checking_enabled(true);
        settings.with_prioritization_strategy();
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
	return 0;
}
