#include "rtmpprotocol.h"
#include "thread.h"
#include <iostream>

RtmpProtocol::RtmpProtocol(st_netfd_t fd): m_fd{fd}
{
    m_trd = new Thread(this);
}

int RtmpProtocol::cycle()
{
    int result = 0;

    std::cout<<"init cycle"<<std::endl;

    return result;
}

int RtmpProtocol::start()
{
    int result = 0;
    result = m_trd->start();
    return result;
}
