#include "protocol_rtmp_msg_array.h"
#include "error.h"
#include "flv.h"


MessageArray::MessageArray(int max_msgs)
{
    Assert(max_msgs > 0);

    m_msgs = new SharedPtrMessage*[max_msgs];
    m_max = max_msgs;

    Zero(max_msgs);
}

MessageArray::~MessageArray()
{
    // we just free the msgs itself,
    // both delete and delete[] is ok,
    // for all msgs is already freed by send_and_free_messages.
    Freepa(m_msgs);
}

void MessageArray::Free(int count)
{
    // initialize
    for (int i = 0; i < count; i++) {
        SharedPtrMessage* msg = m_msgs[i];
        Freep(msg);

        m_msgs[i] = NULL;
    }
}

void MessageArray::Zero(int count)
{
    // initialize
    for (int i = 0; i < count; i++) {
        m_msgs[i] = NULL;
    }
}
