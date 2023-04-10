#ifndef RTMPPROTOCOL_H
#define RTMPPROTOCOL_H
#include "st/st.h"
class Thread;

class RtmpProtocol
{
public:
    RtmpProtocol(st_netfd_t fd);
    int cycle();
    int start();

private:
    st_netfd_t m_fd = nullptr;
    Thread *m_trd = nullptr;
};

#endif // RTMPPROTOCOL_H
