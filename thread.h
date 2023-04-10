#ifndef THREAD_H
#define THREAD_H

class RtmpProtocol;

class Thread
{
public:
    Thread(RtmpProtocol *handle);
    int start();
    int cycle();

private:
    RtmpProtocol *m_handle;
    static void* pfn(void* arg);
};

#endif // THREAD_H
