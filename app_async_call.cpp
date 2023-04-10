#include "app_async_call.h"
#include <sys/wait.h>

IAsyncCallTask::IAsyncCallTask()
{

}

IAsyncCallTask::~IAsyncCallTask()
{

}

AsyncCallWorker::AsyncCallWorker()
{
    m_trd = new DummyCoroutine();
    m_wait = CondNew();
    m_lock = MutexNew();
}

AsyncCallWorker::~AsyncCallWorker()
{
    Freep(m_trd);

    std::vector<IAsyncCallTask*>::iterator it;
    for (it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        IAsyncCallTask* task = *it;
        Freep(task);
    }
    m_tasks.clear();

    CondDestroy(m_wait);
    MutexDestroy(m_lock);
}

error AsyncCallWorker::Execute(IAsyncCallTask *t)
{
    error err = SUCCESS;

    m_tasks.push_back(t);
    CondSignal(m_wait);

    return err;
}

int AsyncCallWorker::Count()
{
    return (int)m_tasks.size();
}

error AsyncCallWorker::Start()
{
    error err = SUCCESS;

    Freep(m_trd);
    m_trd = new STCoroutine("async", this, Context->GetId());

    if ((err = m_trd->Start()) != SUCCESS) {
        return ERRORWRAP(err, "coroutine");
    }

    return err;
}

void AsyncCallWorker::Stop()
{
    FlushTasks();
    CondSignal(m_wait);
    m_trd->Stop();
}

error AsyncCallWorker::Cycle()
{
    error err = SUCCESS;

    while (true) {
        if ((err = m_trd->Pull()) != SUCCESS) {
            return ERRORWRAP(err, "async call worker");
        }

        if (m_tasks.empty()) {
            CondWait(m_wait);
        }

        FlushTasks();
    }

    return err;
}

void AsyncCallWorker::FlushTasks()
{
    error err = SUCCESS;

    // Avoid the async call blocking other coroutines.
    std::vector<IAsyncCallTask*> copy;
    if (true) {
        Locker(m_lock);

        if (m_tasks.empty()) {
            return;
        }

        copy = m_tasks;
        m_tasks.clear();
    }

    std::vector<IAsyncCallTask*>::iterator it;
    for (it = copy.begin(); it != copy.end(); ++it) {
        IAsyncCallTask* task = *it;

        if ((err = task->Call()) != SUCCESS) {
            warn("ignore task failed %s", ERRORDESC(err).c_str());
            Freep(err);
        }
        Freep(task);
    }
}
