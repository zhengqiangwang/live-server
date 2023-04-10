#ifndef APP_TENCETCLOUD_H
#define APP_TENCETCLOUD_H


#include "buffer.h"
#include "log.h"
#include <cstdint>
#include <vector>

class SrsClsSugar : public IEncoder
{
private:
    ClsLog* log_;
    ClsLogGroup* log_group_;
    ClsLogGroupList* log_groups_;
public:
    SrsClsSugar();
    virtual ~SrsClsSugar();
public:
    virtual uint64_t nb_bytes();
    error encode(Buffer* b);
public:
    bool empty();
    SrsClsSugar* kv(std::string k, std::string v);
};

class ClsSugars : public IEncoder
{
private:
    std::vector<SrsClsSugar*> sugars;
public:
    ClsSugars();
    virtual ~ClsSugars();
public:
    virtual uint64_t nb_bytes();
    error encode(Buffer* b);
public:
    ClsSugar* create();
    ClsSugars* slice(int max_size);
    bool empty();
    int size();
};
#endif // APP_TENCETCLOUD_H
