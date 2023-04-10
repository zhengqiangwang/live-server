#ifndef APP_ASYNC_CALL_H
#define APP_ASYNC_CALL_H


#include "app_st.h"
#include "core.h"
#include "protocol_st.h"
#include <string>
#include <vector>

class IAsyncCallTask
{
public:
    IAsyncCallTask();
    virtual ~IAsyncCallTask();
public:
    // Execute the task async.
    // This method is the actual execute method of task,
    // for example, to notify callback server.
    virtual error Call() = 0;
    // Convert task to string to describe it.
    // It's used for logger.
    virtual std::string ToString() = 0;
};

// The async callback for dvr, callback and other async worker.
// When worker call with the task, the worker will do it in isolate thread.
// That is, the task is execute/call in async mode.
class AsyncCallWorker : public ICoroutineHandler
{
private:
    Coroutine* m_trd;
protected:
    std::vector<IAsyncCallTask*> m_tasks;
    cond_t m_wait;
    mutex_t m_lock;
public:
    AsyncCallWorker();
    virtual ~AsyncCallWorker();
public:
    virtual error Execute(IAsyncCallTask* t);
    virtual int Count();
public:
    virtual error Start();
    virtual void Stop();
// Interface ISrsReusableThreadHandler
public:
    virtual error Cycle();
private:
    virtual void FlushTasks();
};

#endif // APP_ASYNC_CALL_H
