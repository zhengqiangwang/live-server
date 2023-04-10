#ifndef APP_THREAD_H
#define APP_THREAD_H

#include <pthread.h>
#include "app_hourglass.h"
#include "core.h"


class ThreadPool;
class ProcSelfStat;

//protect server in high load
class CircuitBreaker : public IFastTimer
{
private:
    //the config for high/critical water level.
    bool m_enabled;
    int m_highThreshold;
    int m_highPulse;
    int m_criticalThreshold;
    int m_criticalPulse;
    int m_dyingThreshold;
    int m_dyingPulse;
private:
    //reset the water-level when cpu is low for n times.
    int m_hybridHighWaterLevel;
    int m_hybridCriticalWaterLevel;
    int m_hybridDyingWaterLevel;
public:
    CircuitBreaker();
    virtual ~CircuitBreaker();
public:
    error Initialize();
public:
    //whether hybrid server water-level is high
    bool HybridHighWaterLevel();
    bool HybridCriticalWaterLevel();
    bool HybridDyingWaterLevel();
//interface IFastTimer
private:
    error OnTimer(utime_t interval);
};

extern CircuitBreaker* circuit_breaker;

//Initialize global shared variables cross all threads.
extern error GlobalInitialize();

//the thread mutex wrapper, without error.
class ThreadMutex
{
private:
    pthread_mutex_t m_lock;
    pthread_mutexattr_t m_attr;
public:
    ThreadMutex();
    virtual ~ThreadMutex();
public:
    void Lock();
    void UnLock();
};

#define ThreadLocker(instance) \
    impl_ThreadLocker free_##instance(instance)

class impl_ThreadLocker
{
private:
    ThreadMutex* m_lock;
public:
    impl_ThreadLocker(ThreadMutex* l)
    {
        m_lock = l;
        m_lock->Lock();
    }
    virtual ~impl_ThreadLocker(){
        m_lock->UnLock();
    }
};

//the information for a thread
class ThreadEntry
{
public:
    ThreadPool* m_pool;
    std::string m_label;
    std::string m_name;
    error (*m_start)(void* arg);
    void* m_arg;
    int m_num;
    pid_t m_tid;
public:
    //the thread object
    pthread_t m_trd;
    //the exit error of thread
    error m_err;
public:
    ThreadEntry();
    virtual ~ThreadEntry();
};

//allocate a(or almost) fixed thread poll to execute tasks,
//so that we can take the advantage of multiple cupus
class ThreadPool
{
private:
    ThreadEntry* m_entry;
    utime_t m_interval;
private:
    ThreadMutex* m_lock;
    std::vector<ThreadEntry*> m_threads;
private:
    // The hybrid server entry, the cpu percent used for circuit breaker.
    ThreadEntry* m_hybrid;
    std::vector<ThreadEntry*> m_hybrids;
private:
    // The pid file fd, lock the file write when server is running.
    // @remark the init.d script should cleanup the pid file, when stop service,
    //       for the server never delete the file; when system startup, the pid in pid file
    //       maybe valid but the process is not SRS, the init.d script will never start server.
    int m_pidFd;
public:
    ThreadPool();
    virtual ~ThreadPool();
public:
    //setup the thread-local variables.
    static error SetupThreadLocals();
    //initialize the thread pool
    error Initialize();
private:
    //require the PID file for the whole process
    virtual error AcquirePidFile();
public:
    //execute start function with label in thread
    error Execute(std::string label, error (*start)(void* arg), void* arg);
    //run in the primordial thread, util stop or quit
    error Run();
    //stop the thread pool and quit the primordial thread
    void Stop();
public:
    ThreadEntry* Self();
    ThreadEntry* Hybrid();
    std::vector<ThreadEntry*> Hybrids();
private:
    static void* Start(void* arg);
};

//it must be thread-safe, global and shared object
extern ThreadPool* thread_pool;

#endif // APP_THREAD_H
