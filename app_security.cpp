#include "app_security.h"
#include "app_config.h"
#include "protocol_utility.h"

Security::Security()
{

}

Security::~Security()
{

}

error Security::Check(RtmpConnType type, std::string ip, Request *req)
{
    error err = SUCCESS;

    // allow all if security disabled.
    if (!config->GetSecurityEnabled(req->m_vhost)) {
        return err; // OK
    }

    // rules to apply
    ConfDirective* rules = config->GetSecurityRules(req->m_vhost);
    return DoCheck(rules, type, ip, req);
}

error Security::DoCheck(ConfDirective *rules, RtmpConnType type, std::string ip, Request *req)
{
    error err = SUCCESS;

    if (!rules) {
        return ERRORNEW(ERROR_SYSTEM_SECURITY, "default deny for %s", ip.c_str());
    }

    // deny if matches deny strategy.
    if ((err = DenyCheck(rules, type, ip)) != SUCCESS) {
        return ERRORWRAP(err, "for %s", ip.c_str());
    }

    // allow if matches allow strategy.
    if ((err = AllowCheck(rules, type, ip)) != SUCCESS) {
        return ERRORWRAP(err, "for %s", ip.c_str());
    }

    return err;
}

error Security::AllowCheck(ConfDirective *rules, RtmpConnType type, std::string ip)
{
    int allow_rules = 0;
    int deny_rules = 0;

    for (int i = 0; i < (int)rules->m_directives.size(); i++) {
        ConfDirective* rule = rules->At(i);

        if (rule->m_name != "allow") {
            if (rule->m_name == "deny") {
                deny_rules++;
            }
            continue;
        }
        allow_rules++;

        std::string cidr_ipv4 = GetCidrIpv4(rule->Arg1());
        std::string cidr_mask = GetCidrMask(rule->Arg1());

        switch (type) {
            case RtmpConnPlay:
            //case SrsRtcConnPlay:
                if (rule->Arg0() != "play") {
                    break;
                }
                if (rule->Arg1() == "all" || rule->Arg1() == ip) {
                    return SUCCESS; // OK
                }
                if (IsIpv4(cidr_ipv4) && cidr_mask != "" && Ipv4WithinMask(ip, cidr_ipv4, cidr_mask)) {
                    return SUCCESS; // OK
                }
                break;
            case RtmpConnFMLEPublish:
            case RtmpConnFlashPublish:
            case RtmpConnHaivisionPublish:
            //case SrsRtcConnPublish:
                if (rule->Arg0() != "publish") {
                    break;
                }
                if (rule->Arg1() == "all" || rule->Arg1() == ip) {
                    return SUCCESS; // OK
                }
                if (IsIpv4(cidr_ipv4) && cidr_mask != "" && Ipv4WithinMask(ip, cidr_ipv4, cidr_mask)) {
                    return SUCCESS; // OK
                }
                break;
            case RtmpConnUnknown:
            default:
                break;
        }
    }

    if (allow_rules > 0 || (deny_rules + allow_rules) == 0) {
        return ERRORNEW(ERROR_SYSTEM_SECURITY_ALLOW, "not allowed by any of %d/%d rules", allow_rules, deny_rules);
    }
    return SUCCESS; // OK
}

error Security::DenyCheck(ConfDirective *rules, RtmpConnType type, std::string ip)
{
    for (int i = 0; i < (int)rules->m_directives.size(); i++) {
        ConfDirective* rule = rules->At(i);

        if (rule->m_name != "deny") {
            continue;
        }

        std::string cidr_ipv4 = GetCidrIpv4(rule->Arg1());
        std::string cidr_mask = GetCidrMask(rule->Arg1());

        switch (type) {
            case RtmpConnPlay:
            //case SrsRtcConnPlay:
                if (rule->Arg0() != "play") {
                    break;
                }
                if (rule->Arg1() == "all" || rule->Arg1() == ip) {
                    return ERRORNEW(ERROR_SYSTEM_SECURITY_DENY, "deny by rule<%s>", rule->Arg1().c_str());
                }
                if (IsIpv4(cidr_ipv4) && cidr_mask != "" && Ipv4WithinMask(ip, cidr_ipv4, cidr_mask)) {
                    return ERRORNEW(ERROR_SYSTEM_SECURITY_DENY, "deny by rule<%s>", rule->Arg1().c_str());
                }
                break;
            case RtmpConnFMLEPublish:
            case RtmpConnFlashPublish:
            case RtmpConnHaivisionPublish:
            //case SrsRtcConnPublish:
                if (rule->Arg0() != "publish") {
                    break;
                }
                if (rule->Arg1() == "all" || rule->Arg1() == ip) {
                    return ERRORNEW(ERROR_SYSTEM_SECURITY_DENY, "deny by rule<%s>", rule->Arg1().c_str());
                }
                if (IsIpv4(cidr_ipv4) && cidr_mask != "" && Ipv4WithinMask(ip, cidr_ipv4, cidr_mask)) {
                    return ERRORNEW(ERROR_SYSTEM_SECURITY_DENY, "deny by rule<%s>", rule->Arg1().c_str());
                }
                break;
            case RtmpConnUnknown:
            default:
                break;
        }
    }

    return SUCCESS; // OK
}
