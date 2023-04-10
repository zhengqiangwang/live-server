#include "app_st.h"
#include "log.h"
#include "protocol_log.h"

ICoroutineHandler::ICoroutineHandler()
{

}

ICoroutineHandler::~ICoroutineHandler()
{

}

IStartable::IStartable()
{

}

IStartable::~IStartable()
{

}

Coroutine::Coroutine()
{

}

Coroutine::~Coroutine()
{

}

DummyCoroutine::DummyCoroutine()
{

}

DummyCoroutine::~DummyCoroutine()
{

}

error DummyCoroutine::Start()
{
    return ERRORNEW(ERROR_THREAD_DUMMY, "dummy coroutine");
}

void DummyCoroutine::Stop()
{

}

void DummyCoroutine::Interrupt()
{

}

error DummyCoroutine::Pull()
{
    return ERRORNEW(ERROR_THREAD_DUMMY, "dummy pull");
}

const ContextId &DummyCoroutine::Cid()
{
    return m_cid;
}

void DummyCoroutine::SetCid(const ContextId &cid)
{
    m_cid = cid;
}

STCoroutine::STCoroutine(std::string name, ICoroutineHandler *handler)
{
    m_impl = new FastCoroutine(name, handler);
}

STCoroutine::STCoroutine(std::string name, ICoroutineHandler *handler, ContextId cid)
{
    m_impl = new FastCoroutine(name, handler, cid);
}

STCoroutine::~STCoroutine()
{
    Freep(m_impl);
}

void STCoroutine::SetStackSize(int size)
{
    m_impl->SetStackSize(size);
}

error STCoroutine::Start()
{
    return m_impl->Start();
}

void STCoroutine::Stop()
{
    m_impl->Stop();
}

void STCoroutine::Interrupt()
{
    m_impl->Interrupt();
}

error STCoroutine::Pull()
{
    return m_impl->Pull();
}

const ContextId &STCoroutine::Cid()
{
    return m_impl->Cid();
}

void STCoroutine::SetCid(const ContextId &cid)
{
    m_impl->SetCid(cid);
}

FastCoroutine::FastCoroutine(std::string name, ICoroutineHandler *handler)
{
    m_name = name;
    m_handler = handler;
    m_trd = nullptr;
    m_err = SUCCESS;
    m_started = m_interrupted = m_disposed = m_cycleDone = false;
    m_stopping = false;

    //0 use default, default is 64K
    m_stackSize = 0;
}

FastCoroutine::FastCoroutine(std::string name, ICoroutineHandler *handler, ContextId cid)
{
    m_name = name;
    m_handler = handler;
    m_cid = cid;
    m_trd = nullptr;
    m_err = SUCCESS;
    m_started = m_interrupted = m_disposed = m_cycleDone = false;
    m_stopping = false;

    //0 use default, default is 64K
    m_stackSize = 0;
}

FastCoroutine::~FastCoroutine()
{
    Stop();

    //todo: fixme: we must assert the cycle is done

    Freep(m_err);
}

void FastCoroutine::SetStackSize(int v)
{
    m_stackSize = v;
}

error FastCoroutine::Start()
{
    error err = SUCCESS;

    if(m_started || m_disposed){
        if(m_disposed){
            err = ERRORNEW(ERROR_THREAD_DISPOSED, "disposed");
        }else{
            err = ERRORNEW(ERROR_THREAD_STARTED, "started");
        }

        if(m_err == SUCCESS)
        {
            m_err = ERRORCOPY(m_err);
        }

        return err;
    }

    if((m_trd = (st_thread_t)st_thread_create(pfn, this, 1, m_stackSize)) == nullptr){
        err = ERRORNEW(ERROR_ST_CREATE_CYCLE_THREAD, "create failed");

        Freep(m_err);
        m_err = ERRORCOPY(err);

        return err;
    }

    m_started = true;

    return err;
}

void FastCoroutine::Stop()
{
    if(m_disposed){
        if(m_stopping){
            ERROR("thread is stopping by %s", m_stoppingCid.Cstr());
            Assert(!m_stopping);
        }
        return;
    }
    m_disposed = true;
    m_stopping = true;

    Interrupt();

    //when not started, the trd is NULL
    if(m_trd){
        void* res = nullptr;
        int r0 = st_thread_join(m_trd, &res);
        if(r0){
            //By st_thread_join
            if(errno == EINVAL) Assert(!r0);
            if(errno == EDEADLK) Assert(!r0);
            //By st_cond_timewait
            if(errno == EINTR) Assert(!r0);
            if(errno == ETIME) Assert(!r0);
            //others
            Assert(!r0);
        }

        error err_res = (error)res;
        if(err_res != SUCCESS){
            //when worker cycle done, the error has already been overrided,
            //so the m_err should be equal to err_res
            Assert(m_err == err_res);
        }
    }

    //If there's no error occur from worker, try to set to terminated error
    if(m_err == SUCCESS && !m_cycleDone){
        m_err = ERRORNEW(ERROR_THREAD_TERMINATED, "terminated");
    }

    //now ,we'are stopped
    m_stopping = false;

    return;
}

void FastCoroutine::Interrupt()
{
    if(!m_started || m_interrupted || m_cycleDone){
        return;
    }
    m_interrupted = true;

    if(m_err == SUCCESS){
        m_err = ERRORNEW(ERROR_THREAD_INTERRUPED, "interrupted");
    }

    //note that if another thread is stopping thread and waiting in st_thread_join,
    //the interrupt will make the st_thread_join fail
    st_thread_interrupt(m_trd);
}

const ContextId &FastCoroutine::Cid()
{
    return m_cid;
}

void FastCoroutine::SetCid(const ContextId &cid)
{
    m_cid = cid;
    ContextSetCidOf(m_trd, cid);

}

error FastCoroutine::Cycle()
{
    if(Context){
        if(m_cid.Empty()){
            m_cid = Context->GenerateId();
        }
        Context->SetId(m_cid);
    }

    error err = m_handler->Cycle();
    if(err != SUCCESS){
        return ERRORWRAP(err, "coroutine cycle");
    }

    //set cycle done, no need to interrupt it.
    m_cycleDone = true;

    return err;
}

void *FastCoroutine::pfn(void *arg)
{
    FastCoroutine* p = (FastCoroutine*)arg;

    error err = p->Cycle();

    //set the err for function pull to fetch it
    if(err != SUCCESS){
        Freep(p->m_err);
        //it's ok to directly use it, because it's returned by st_thread_join
        p->m_err = err;
    }

    return (void*)err;
}

WaitGroup::WaitGroup()
{
    m_nn = 0;
    m_done = st_cond_new();
}

WaitGroup::~WaitGroup()
{
    Wait();
    st_cond_destroy(m_done);
}

void WaitGroup::Add(int n)
{
    m_nn += n;
}

void WaitGroup::Done()
{
    m_nn--;
    if(m_nn <= 0)
    {
        st_cond_signal(m_done);
    }
}

void WaitGroup::Wait()
{
    if(m_nn > 0){
        st_cond_wait(m_done);
    }
}
