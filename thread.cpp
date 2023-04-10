#include "thread.h"
#include "rtmpprotocol.h"

Thread::Thread(RtmpProtocol *handle): m_handle{handle}
{

}

int Thread::start()
{
    int result = 0;
    if(st_thread_create(pfn, this, 0, 0) == nullptr)
    {
        result = -1;
    }
    return result;
}

int Thread::cycle()
{
    int result = 0;
    result = m_handle->cycle();
    return result;
}

void *Thread::pfn(void *arg)
{
    Thread *p = (Thread *)arg;
    p->cycle();
    return nullptr;
}
