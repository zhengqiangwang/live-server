#ifndef PROTOCOL_RTMP_MSG_ARRAY_H
#define PROTOCOL_RTMP_MSG_ARRAY_H


class SharedPtrMessage;

// The class to auto free the shared ptr message array.
// When need to get some messages, for instance, from Consumer queue,
// create a message array, whose msgs can used to accept the msgs,
// then send each message and set to NULL.
//
// @remark: user must free all msgs in array, for the SRS2.0 protocol stack
//       provides an api to send messages, @see send_and_free_messages
class MessageArray
{
public:
    // When user already send all msgs, please set to NULL,
    // for instance, msg= msgs.msgs[i], msgs.msgs[i]=NULL, send(msg),
    // where send(msg) will always send and free it.
    SharedPtrMessage** m_msgs;
    int m_max;
public:
    // Create msg array, initialize array to NULL ptrs.
    MessageArray(int max_msgs);
    // Free the msgs not sent out(not NULL).
    virtual ~MessageArray();
public:
    // Free specified count of messages.
    virtual void Free(int count);
private:
    // Zero initialize the message array.
    virtual void Zero(int count);
};

#endif // PROTOCOL_RTMP_MSG_ARRAY_H
