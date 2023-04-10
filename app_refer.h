#ifndef APP_REFER_H
#define APP_REFER_H


#include "log.h"
#include <string>

class ConfDirective;

class Refer
{
public:
    Refer();
    virtual ~Refer();
public:
    // Check the refer.
    // @param page_url the client page url.
    // @param refer the refer in config.
    virtual error Check(std::string page_url, ConfDirective* refer);
private:
    virtual error CheckSingleRefer(std::string page_url, std::string refer);
};

#endif // APP_REFER_H
