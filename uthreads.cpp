#include <iostream>
#include "uthreads.h"
#include <deque>
#include <vector>
#include "Thread.h"
#include <algorithm>
#include <sys/time.h>
#include <signal.h>
#include <setjmp.h>
#include <set>
#include <map>

sigjmp_buf env[MAX_THREAD_NUM];

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
            "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
		"rol    $0x9,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#endif

// ## Global Library Variables ##
static std::vector<Thread*> thread_list(MAX_THREAD_NUM, nullptr);
static std::deque<unsigned int> ready_threads_id;
static unsigned int active_threads_num = 0;
static unsigned int running_thread = 0;
static unsigned int quantums_num = 0;
static __suseconds_t quantum_usec;
static struct itimerval timer;
static sigset_t set;
static std::map<unsigned int, std::set<unsigned int>> dependencies_dict;


// ## Constants ##

#define FAIL_MSG_PREFIX "thread library error: "
#define SYSTEM_ERR_PREFIX "system error: "


// ## Private Helper Methods ##

/**
 * Releases allocated memory
 * @return
 */
int _terminate_all()
{
    for (int id = 0; id < MAX_THREAD_NUM; ++id) {
        delete thread_list[id];
        thread_list[id] = nullptr;
    }
}


/**
 * creates the mask set for signal un\blocking of signal SIGVTALRM
 */
void _init_block_mask()
{
    if (sigemptyset(&set) == -EXIT_FAILURE)
    {
        std::cerr << SYSTEM_ERR_PREFIX << "sigemptyset failed" << std::endl;
        _terminate_all();
        exit(EXIT_FAILURE);
    }
    if (sigaddset(&set, SIGVTALRM) == -EXIT_FAILURE)
    {
        std::cerr << SYSTEM_ERR_PREFIX << "sigaddset failed" << std::endl;
        _terminate_all();
        exit(EXIT_FAILURE);
    }
}


/**
 * blocking mask defined by _init_block_mask
 */
void _block()
{
    if (sigprocmask(SIG_BLOCK, &set, nullptr) == -EXIT_FAILURE)
    {
        std::cerr << SYSTEM_ERR_PREFIX << "sigprocmask failed" << std::endl;
        _terminate_all();
        exit(EXIT_FAILURE);
    }
}


/**
 * unblocking mask defined by _init_block_mask
 */
void _unblock()
{
    if (sigprocmask(SIG_UNBLOCK, &set, nullptr) == -EXIT_FAILURE)
    {
        std::cerr << SYSTEM_ERR_PREFIX << "sigprocmask failed" << std::endl;
        _terminate_all();
        exit(EXIT_FAILURE);
    }
}


/**
 * Starts a timer of quantum_usec microseconds intervals. exits with code 1 on failure
 */
void _start_timer(){
    // Configure the timer to expire after quantum_usecs sec... */
    timer.it_value.tv_sec = 0;		// first time interval, seconds part
    timer.it_value.tv_usec = quantum_usec;		// first time interval, microseconds part

    // configure the timer to expire every quantum_usecs sec after that.
    timer.it_interval.tv_sec = 0;	// following time intervals, seconds part
    timer.it_interval.tv_usec = quantum_usec;	// following time intervals, microseconds part

    if (setitimer (ITIMER_VIRTUAL, &timer, nullptr) == -EXIT_FAILURE)
    {
        std::cerr << SYSTEM_ERR_PREFIX << "setitimer failed" << std::endl;
        _terminate_all();
        exit(EXIT_FAILURE);
    }
}


/**
 * Switching between threads. the running thread is handled based on the given argument and the next thread in ready
 * queue will run with a restarted timer.
 * @param next_state_for_running_thread the new state of the running state. either "READY", "BLOCKED" or "TERMINATED"
 */
void _switch_threads(const std::string &next_state_for_running_thread = "READY") {
    // Saving environment of current thread
    int ret_val = sigsetjmp(env[running_thread], 1);
    if (ret_val == 1) {
        return;
    }
    // Switching
    if (next_state_for_running_thread == "READY")
    {
        ready_threads_id.push_back(running_thread);
        thread_list[running_thread]->setState("READY");
    }
    else if (next_state_for_running_thread == "BLOCKED")
    {
        thread_list[running_thread]->setState("BLOCKED");
    }
    else if (next_state_for_running_thread == "TERMINATED"){}
    running_thread = ready_threads_id.front();
    ready_threads_id.pop_front();
    thread_list[running_thread]->setState("RUNNING");
    thread_list[running_thread]->raise_quantums();
    quantums_num++;
    _start_timer();
    siglongjmp(env[running_thread], 1);
}


/**
 * Handler for SIGVTALRM action
 */
void _timer_handler(int sig)
{
    _block();
    _switch_threads();
    _unblock();
}


/**
 * checks if id is valid
 * @param tid thread id to validate
 * @return if valid returns true, else false
 */
bool _is_id_valid(int tid)
{
    if (tid > (MAX_THREAD_NUM - 1)  || tid < 0 || thread_list[tid] == nullptr)
    {
        std::cerr << FAIL_MSG_PREFIX << "Thread ID not found" << std::endl;
        return false;
    }
    return true;
}


// ## Library's API Implementation##

int uthread_init(int quantum_usecs) {
     if (quantum_usecs <= 0) {
         std::cerr << FAIL_MSG_PREFIX << "Quantum time can't be non-positive" << std::endl;
         return(-EXIT_FAILURE);
    }
    quantum_usec = quantum_usecs;
    Thread* main_thread = new Thread("RUNNING", 0, nullptr);
    thread_list[0] = main_thread;
    active_threads_num++;
    thread_list[running_thread]->raise_quantums();
    quantums_num++;


    // Install _timer_handler as the signal handler for SIGVTALRM.
    struct sigaction sa;
    sa.sa_handler = &_timer_handler;
    if (sigaction(SIGVTALRM, &sa, nullptr) < 0) {
        std::cerr << SYSTEM_ERR_PREFIX << "sigaction failed" << std::endl;
        _terminate_all();
        exit(EXIT_FAILURE);
    }
    _start_timer();
    _init_block_mask();

    return EXIT_SUCCESS;
}

int uthread_spawn(void (*f)(void)) {
    _block();
    if(active_threads_num >= MAX_THREAD_NUM)
    {
        std::cerr << FAIL_MSG_PREFIX << "Threads number has reached it's maximum" << std::endl;
        _unblock();
        return (-EXIT_FAILURE);
    }

    // Creating a new thread at the minimal index available
    unsigned int id;
    for (id = 1 ; id < MAX_THREAD_NUM ; id++)
    {
        if (thread_list[id] == nullptr)
        {
            Thread* new_thread = new Thread("READY", id, f);
            thread_list[id] = new_thread;
            ready_threads_id.push_back(id);
            break;
        }
    }
    active_threads_num++;

    // Saving environment of the newly thread
    address_t sp, pc;
    sp = (address_t)thread_list[id]->getAllocated_mem() + STACK_SIZE - sizeof(address_t);
    pc = (address_t)f;
    sigsetjmp(env[id], 1);
    (env[id]->__jmpbuf)[JB_SP] = translate_address(sp);
    (env[id]->__jmpbuf)[JB_PC] = translate_address(pc);
    if (sigemptyset(&env[id]->__saved_mask) == -EXIT_FAILURE)
    {
        std::cerr << SYSTEM_ERR_PREFIX << "sigemptyset failed" << std::endl;
        _unblock();
        _terminate_all();
        exit(EXIT_FAILURE);
    }
    _unblock();

    return id;
}

int uthread_terminate(int tid) {
    _block();
    if (!_is_id_valid(tid))
    {
        _unblock();
        return (-EXIT_FAILURE);
    }

    //terminated thread is the main thread
    if (tid == 0)
    {
        _terminate_all();
        _unblock();
        exit(0);
    }
    //terminated thread is on 'READY' state
    if (thread_list[tid]->getState() == "READY")
        ready_threads_id.erase(std::remove(ready_threads_id.begin(), ready_threads_id.end(), tid), ready_threads_id.end());

    //terminated thread is on 'RUNNING' state
    else if (thread_list[tid]->getState() == "RUNNING")
    {
        _switch_threads("TERMINATED");
    }

    if(dependencies_dict.find((unsigned int)tid) != dependencies_dict.end())
    {
        for (auto thread_id : dependencies_dict[tid])
        {
            thread_list[thread_id]->switch_sync_state();
            uthread_resume(thread_id);
        }
        dependencies_dict.erase((unsigned int)tid);
    }
    delete thread_list[tid];
    thread_list[tid] = nullptr;
    active_threads_num--;
    _unblock();
    return EXIT_SUCCESS;
}

int uthread_block(int tid) {
    _block();
    if (!_is_id_valid(tid))
    {
        _unblock();
        return (-EXIT_FAILURE);
    }
    if (tid == 0)
    {
        std::cerr << FAIL_MSG_PREFIX << "Can't block main thread (ID == 0)" << std::endl;
        _unblock();
        return (-EXIT_FAILURE);
    }

    if (thread_list[tid]->getState() == "BLOCKED")
    {
        _unblock();
        return EXIT_SUCCESS;
    }

    if (thread_list[tid]->getState() == "RUNNING")
    {
        _switch_threads("BLOCKED");
    }

    else if (thread_list[tid]->getState() == "READY")
    {
        ready_threads_id.erase(std::remove(ready_threads_id.begin(), ready_threads_id.end(), tid), ready_threads_id.end());
        thread_list[tid]->setState("BLOCKED");
    }
    _unblock();
    return EXIT_SUCCESS;
}

int uthread_resume(int tid) {
    _block();
    if (!_is_id_valid(tid))
    {
        _unblock();
        return (-EXIT_FAILURE);
    }
    if (thread_list[tid]->getState() == "BLOCKED" && !thread_list[tid]->get_is_synced())
    {
        thread_list[tid]->setState("READY");
        ready_threads_id.push_back((unsigned int)tid);
    }
    _unblock();
    return EXIT_SUCCESS;
}

int uthread_sync(int tid) {
    _block();
    if (!_is_id_valid(tid) || tid == running_thread || tid == 0)
    {
        _unblock();
        return (-EXIT_FAILURE);
    }
    unsigned int current_thread = running_thread;
    _switch_threads("BLOCKED");
    dependencies_dict[tid].insert(current_thread);
    thread_list[current_thread]->switch_sync_state();
    _unblock();
    return EXIT_SUCCESS;
}

int uthread_get_tid() {
    return (int)running_thread;
}

int uthread_get_total_quantums() {
    return (int)quantums_num;
}

int uthread_get_quantums(int tid) {
    if (!_is_id_valid(tid))
        return (-EXIT_FAILURE);
    return (int)thread_list[tid]->get_quantums_num();
}

