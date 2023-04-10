#include "app_refer.h"
#include "app_config.h"
#include "error.h"

Refer::Refer()
{

}

Refer::~Refer()
{

}

error Refer::Check(std::string page_url, ConfDirective *refer)
{
    error err = SUCCESS;

    if (!refer) {
        return err;
    }

    for (int i = 0; i < (int)refer->m_args.size(); i++) {
        if ((err = CheckSingleRefer(page_url, refer->m_args.at(i))) == SUCCESS) {
            return SUCCESS;
        }

        ERRORRESET(err);
    }

    return ERRORNEW(ERROR_RTMP_ACCESS_DENIED, "access denied");
}

error Refer::CheckSingleRefer(std::string page_url, std::string refer)
{
    error err = SUCCESS;

    size_t pos = std::string::npos;

    std::string domain_name = page_url;
    if ((pos = domain_name.find("://")) != std::string::npos) {
        domain_name = domain_name.substr(pos + 3);
    }

    if ((pos = domain_name.find("/")) != std::string::npos) {
        domain_name = domain_name.substr(0, pos);
    }

    if ((pos = domain_name.find(":")) != std::string::npos) {
        domain_name = domain_name.substr(0, pos);
    }

    pos = domain_name.find(refer);
    if (pos == std::string::npos) {
        return ERRORNEW(ERROR_RTMP_ACCESS_DENIED, "access denied");
    }
    // match primary domain.
    if (pos != domain_name.length() - refer.length()) {
        return ERRORNEW(ERROR_RTMP_ACCESS_DENIED, "access denied");
    }

    return err;
}
