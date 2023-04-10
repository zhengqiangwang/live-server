#ifndef APP_ST_H
#define APP_ST_H


#include "core.h"
#include "error.h"
#include "st/st.h"

class FastCoroutine;

class ICoroutineHandler
{
public:
    ICoroutineHandler();
    virtual ~ICoroutineHandler();

public:
    //Do the work. the st-coroutine will terminated normally if it returned
    virtual error Cycle() = 0;
};

//start the object, generally a croutine
class IStartable
{
public:
    IStartable();
    virtual ~IStartable();

public:
    virtual error Start() = 0;
};

//the corotine object
class Coroutine : public IStartable
{
public:
    Coroutine();
    virtual ~Coroutine();
public:
    virtual void Stop() = 0;
    virtual void Interrupt() = 0;
    //@return a copy of error, which should be freed by user.
    virtual error Pull() = 0;
    //get and set the context id of coroutine
    virtual const ContextId &Cid() = 0;
    virtual void SetCid(const ContextId &cid) = 0;
};

//an empty coroutine, user can default to this object before create any real coroutine
class DummyCoroutine : public Coroutine
{
private:
    ContextId m_cid;
public:
    DummyCoroutine();
    virtual ~DummyCoroutine();
public:
    virtual error Start();
    virtual void Stop();
    virtual void Interrupt();
    virtual error Pull();
    virtual const ContextId &Cid();
    virtual void SetCid(const ContextId &cid);
};

//a st-coroutine is a lightweight thread, just like the goroutine
class STCoroutine : public Coroutine
{
private:
    FastCoroutine *m_impl;
public:
    STCoroutine(std::string name, ICoroutineHandler *handler);
    STCoroutine(std::string name, ICoroutineHandler *handler, ContextId cid);
    virtual ~STCoroutine();
public:
    //set the stack size of coroutine, default to 0(64kB)
    void SetStackSize(int size);
public:
    //start the thread
    virtual error Start();
    //interrupt the thread then wait to terminated
    virtual void Stop();
    //interrupt the thread and notify it to terminate, it will be wakeup if it's blocked
    virtual void Interrupt();
    //check the thread is terminated normally of error
    virtual error Pull();
    //Get and set the context id of thread
    virtual const ContextId &Cid();
    virtual void SetCid(const ContextId &cid);
};

//high performance coroutine
class FastCoroutine
{
private:
    std::string m_name;
    int m_stackSize;
    ICoroutineHandler *m_handler;
private:
    st_thread_t m_trd;
    ContextId m_cid;
    error m_err;
private:
    bool m_started = false;
    bool m_interrupted = false;
    bool m_disposed = false;
    //cycle done, no need to interrupt it
    bool m_cycleDone = false;
private:
    //sub state in disposed, we need to wait for thread to quit
    bool m_stopping;
    ContextId m_stoppingCid;
public:
    FastCoroutine(std::string name, ICoroutineHandler *handler);
    FastCoroutine(std::string name, ICoroutineHandler *handler, ContextId cid);
    virtual ~FastCoroutine();
public:
    void SetStackSize(int v);
public:
    error Start();
    void Stop();
    void Interrupt();
    inline error Pull()
    {
        if(m_err == SUCCESS)
        {
            return SUCCESS;
        }
        return ERRORCOPY(m_err);
    }

    const ContextId& Cid();
    virtual void SetCid(const ContextId& cid);
private:
    error Cycle();
    static void* pfn(void* arg);
};

// like goroytine sync.waitgroup
class WaitGroup
{
private:
    int m_nn;
    st_cond_t m_done;
public:
    WaitGroup();
    virtual ~WaitGroup();
public:
    //when start for n coroutines
    void Add(int n);
    //when coroutine is done
    void Done();
    //wait for all corotine to be done
    void Wait();
};

#endif // APP_ST_H
