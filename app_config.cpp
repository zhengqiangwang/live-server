#include "app_config.h"
#include "app_utility.h"
#include "consts.h"
#include "file.h"
#include "error.h"
#include "protocol_rtmp_stack.h"
#include "protocol_json.h"
#include "protocol_utility.h"
#include "utility.h"
#include "core_autofree.h"
#include "app_source.h"
#include "log.h"


#include <cstring>
#include <string>
#include <unistd.h>
#include <inttypes.h>

#ifdef __linux__
#include <linux/version.h>
#include <sys/utsname.h>
#endif

using namespace internal;

// @global the version to identify the core.
const char* _version = "XCORE-" RTMP_SIG_SERVER;

// when user config an invalid value, macros to perfer true or false.
#define CONF_PERFER_FALSE(conf_arg) conf_arg == "on"
#define CONF_PERFER_TRUE(conf_arg) conf_arg != "off"

#define DEFAULT_CONFIG  "/conf.conf"
// default config file.
#define CONF_DEFAULT_COFNIG_FILE DEFAULT_CONFIG

// '\n'
#define _LF (char)CONSTS_LF

// '\r'
#define _CR (char)CONSTS_CR

// Overwrite the config by env.
#define OVERWRITE_BY_ENV_STRING(key) if (!Getenv(key).empty()) return Getenv(key)
#define OVERWRITE_BY_ENV_BOOL(key) if (!Getenv(key).empty()) return CONF_PERFER_FALSE(Getenv(key))
#define OVERWRITE_BY_ENV_BOOL2(key) if (!Getenv(key).empty()) return CONF_PERFER_TRUE(Getenv(key))
#define OVERWRITE_BY_ENV_INT(key) if (!Getenv(key).empty()) return ::atoi(Getenv(key).c_str())
#define OVERWRITE_BY_ENV_FLOAT(key) if (!Getenv(key).empty()) return ::atof(Getenv(key).c_str())
#define OVERWRITE_BY_ENV_SECONDS(key) if (!Getenv(key).empty()) return utime_t(::atoi(Getenv(key).c_str()) * UTIME_SECONDS)
#define OVERWRITE_BY_ENV_MILLISECONDS(key) if (!Getenv(key).empty()) return (utime_t)(::atoi(Getenv(key).c_str()) * UTIME_MILLISECONDS)
#define OVERWRITE_BY_ENV_FLOAT_SECONDS(key) if (!Getenv(key).empty()) return utime_t(::atof(Getenv(key).c_str()) * UTIME_SECONDS)
#define OVERWRITE_BY_ENV_FLOAT_MILLISECONDS(key) if (!Getenv(key).empty()) return utime_t(::atof(Getenv(key).c_str()) * UTIME_MILLISECONDS)

/**
 * dumps the ingest/transcode-engine in @param dir to amf0 object @param engine.
 * @param dir the transcode or ingest config directive.
 * @param engine the amf0 object to dumps to.
 */
error ConfigDumpsEngine(ConfDirective* dir, JsonObject* engine);

/**
 * whether the ch is common space.
 */
bool IsCommonSpace(char ch)
{
    return (ch == ' ' || ch == '\t' || ch == _CR || ch == _LF);
}

namespace internal{

    internal::ConfigBuffer::ConfigBuffer()
    {
        m_line = 1;

        m_pos = m_last = m_start = nullptr;
        m_end = m_start;
    }

    ConfigBuffer::~ConfigBuffer()
    {
        Freepa(m_start);
    }

    error ConfigBuffer::Fullfill(const char *filename)
    {
        error err = SUCCESS;

        FileReader reader;

        // open file reader.
        if ((err = reader.Open(filename)) != SUCCESS) {
            return ERRORWRAP(err, "open file=%s", filename);
        }

        // read all.
        int filesize = (int)reader.Filesize();

        // create buffer
        Freepa(m_start);
        m_pos = m_last = m_start = new char[filesize];
        m_end = m_start + filesize;

        // read total content from file.
        ssize_t nread = 0;
        if ((err = reader.Read(m_start, filesize, &nread)) != SUCCESS) {
            return ERRORWRAP(err, "read %d only %d bytes", filesize, (int)nread);
        }

        return err;
    }

    bool ConfigBuffer::Empty()
    {
    return m_pos >= m_end;
    }
}

bool DirectiveEqualsSelf(ConfDirective* a, ConfDirective* b)
{
    // both NULL, equal.
    if (!a && !b) {
        return true;
    }

    if (!a || !b) {
        return false;
    }

    if (a->m_name != b->m_name) {
        return false;
    }

    if (a->m_args.size() != b->m_args.size()) {
        return false;
    }

    for (int i = 0; i < (int)a->m_args.size(); i++) {
        if (a->m_args.at(i) != b->m_args.at(i)) {
            return false;
        }
    }

    if (a->m_directives.size() != b->m_directives.size()) {
        return false;
    }

    return true;
}

bool DirectiveEquals(ConfDirective *a, ConfDirective *b)
{
    // both NULL, equal.
    if (!a && !b) {
        return true;
    }

    if (!DirectiveEqualsSelf(a, b)) {
        return false;
    }

    for (int i = 0; i < (int)a->m_directives.size(); i++) {
        ConfDirective* a0 = a->At(i);
        ConfDirective* b0 = b->At(i);

        if (!DirectiveEquals(a0, b0)) {
            return false;
        }
    }

    return true;
}

bool DirectiveEquals(ConfDirective *a, ConfDirective *b, std::string except)
{
    // both NULL, equal.
    if (!a && !b) {
        return true;
    }

    if (!DirectiveEqualsSelf(a, b)) {
        return false;
    }

    for (int i = 0; i < (int)a->m_directives.size(); i++) {
        ConfDirective* a0 = a->At(i);
        ConfDirective* b0 = b->At(i);

        // donot compare the except child directive.
        if (a0->m_name == except) {
            continue;
        }

        if (!DirectiveEquals(a0, b0)) {
            return false;
        }
    }

    return true;
}

void SetConfigDirective(ConfDirective* parent, std::string dir, std::string value)
{
    ConfDirective* d = parent->GetOrCreate(dir);
    d->m_name = dir;
    d->m_args.clear();
    d->m_args.push_back(value);
}

bool ConfigHlsIsOnErrorIgnore(std::string strategy)
{
    return strategy == "ignore";
}

bool ConfigHlsIsOnErrorContinue(std::string strategy)
{
    return strategy == "continue";
}

bool ConfigIngestIsFile(std::string type)
{
    return type == "file";
}

bool ConfigIngestIsStream(std::string type)
{
    return type == "stream";
}

bool ConfigDvrIsPlanSegment(std::string plan)
{
    return plan == "segment";
}

bool ConfigDvrIsPlanSession(std::string plan)
{
    return plan == "session";
}

bool StreamCasterIsUdp(std::string caster)
{
    return caster == "mpegts_over_udp";
}

bool StreamCasterIsFlv(std::string caster)
{
    return caster == "flv";
}

bool StreamCasterIsGb28181(std::string caster)
{
    return caster == "gb28181";
}

bool ConfigApplyFilter(ConfDirective *dvr_apply, Request *req)
{
    static bool DEFAULT = true;

    if (!dvr_apply || dvr_apply->m_args.empty()) {
        return DEFAULT;
    }

    std::vector<std::string>& args = dvr_apply->m_args;
    if (args.size() == 1 && dvr_apply->Arg0() == "all") {
        return true;
    }

    std::string id = req->m_app + "/" + req->m_stream;
    if (std::find(args.begin(), args.end(), id) != args.end()) {
        return true;
    }

    return false;
}

std::string ConfigBool2switch(std::string sbool)
{
    return sbool == "true"? "on":"off";
}

error ConfigTransformVhost(ConfDirective *root)
{
    error err = SUCCESS;

    for (int i = 0; i < (int)root->m_directives.size(); i++) {
        ConfDirective* dir = root->m_directives.at(i);

        // SRS2.0, rename global http_stream to http_server.
        //  SRS1:
        //      http_stream {}
        //  SRS2+:
        //      http_server {}
        if (dir->m_name == "http_stream") {
            dir->m_name = "http_server";
            continue;
        }

        // SRS4.0, removed the support of configs:
        //      rtc_server { perf_stat; queue_length; }
        if (dir->m_name == "rtc_server") {
            std::vector<ConfDirective*>::iterator it;
            for (it = dir->m_directives.begin(); it != dir->m_directives.end();) {
                ConfDirective* conf = *it;

                if (conf->m_name == "perf_stat" || conf->m_name == "queue_length") {
                    it = dir->m_directives.erase(it);
                    Freep(conf);
                    continue;
                }

                ++it;
            }
        }

        // SRS5.0, GB28181 allows unused config.
        //      stream_caster {
        //          caster gb28181; tcp_enable; rtp_port_min; rtp_port_max; wait_keyframe; rtp_idle_timeout;
        //          audio_enable; auto_create_channel;
        //          sip {
        //              serial; realm; ack_timeout; keepalive_timeout; invite_port_fixed; query_catalog_interval; auto_play;
        //          }
        //      }
        if (dir->m_name == "stream_caster") {
            for (std::vector<ConfDirective*>::iterator it = dir->m_directives.begin(); it != dir->m_directives.end();) {
                ConfDirective* conf = *it;
                if (conf->m_name == "tcp_enable" || conf->m_name == "rtp_port_min" || conf->m_name == "rtp_port_max"
                    || conf->m_name == "wait_keyframe" || conf->m_name == "rtp_idle_timeout" || conf->m_name == "audio_enable"
                    || conf->m_name == "auto_create_channel"
                ) {
                    warn("transform: Config %s for GB is not used", conf->m_name.c_str());
                    it = dir->m_directives.erase(it); Freep(conf); continue;
                }
                ++it;
            }

            ConfDirective* sip = dir->Get("sip");
            if (sip) {
                for (std::vector<ConfDirective*>::iterator it = sip->m_directives.begin(); it != sip->m_directives.end();) {
                    ConfDirective* conf = *it;
                    if (conf->m_name == "serial" || conf->m_name == "realm" || conf->m_name == "ack_timeout"
                        || conf->m_name == "keepalive_timeout" || conf->m_name == "invite_port_fixed"
                        || conf->m_name == "query_catalog_interval" || conf->m_name == "auto_play"
                    ) {
                        warn("transform: Config sip.%s for GB is not used", conf->m_name.c_str());
                        it = sip->m_directives.erase(it); Freep(conf); continue;
                    }
                    ++it;
                }
            }
        }

        // SRS 5.0, GB28181 moves config from:
        //      stream_caster { caster gb28181; host * }
        // to:
        //      stream_caster { caster gb28181; sip { candidate *; } }
        if (dir->m_name == "stream_caster") {
            for (std::vector<ConfDirective*>::iterator it = dir->m_directives.begin(); it != dir->m_directives.end();) {
                ConfDirective* conf = *it;
                if (conf->m_name == "host") {
                    warn("transform: Config move host to sip.candidate for GB");
                    conf->m_name = "candidate"; dir->GetOrCreate("sip")->m_directives.push_back(conf->Copy());
                    it = dir->m_directives.erase(it); Freep(conf); continue;
                }
                ++it;
            }
        }

        // The bellow is vhost scope configurations.
        if (!dir->IsVhost()) {
            continue;
        }

        // for each directive of vhost.
        std::vector<ConfDirective*>::iterator it;
        for (it = dir->m_directives.begin(); it != dir->m_directives.end();) {
            ConfDirective* conf = *it;
            std::string n = conf->m_name;

            // SRS2.0, rename vhost http to http_static
            //  SRS1:
            //      vhost { http {} }
            //  SRS2+:
            //      vhost { http_static {} }
            if (n == "http") {
                conf->m_name = "http_static";
                warn("transform: vhost.http => vhost.http_static for %s", dir->m_name.c_str());
                ++it;
                continue;
            }

            // SRS3.0, ignore hstrs, always on.
            // SRS1/2:
            //      vhost { http_remux { hstrs; } }
            if (n == "http_remux") {
                ConfDirective* hstrs = conf->Get("hstrs");
                conf->Remove(hstrs);
                Freep(hstrs);
            }

            // SRS3.0, change the refer style
            //  SRS1/2:
            //      vhost { refer; refer_play; refer_publish; }
            //  SRS3+:
            //      vhost { refer { enabled; all; play; publish; } }
            if ((n == "refer" && conf->m_directives.empty()) || n == "refer_play" || n == "refer_publish") {
                // remove the old one first, for name duplicated.
                it = dir->m_directives.erase(it);

                ConfDirective* refer = dir->GetOrCreate("refer");
                refer->GetOrCreate("enabled", "on");
                if (n == "refer") {
                    ConfDirective* all = refer->GetOrCreate("all");
                    all->m_args = conf->m_args;
                    warn("transform: vhost.refer to vhost.refer.all for %s", dir->m_name.c_str());
                } else if (n == "refer_play") {
                    ConfDirective* play = refer->GetOrCreate("play");
                    play->m_args = conf->m_args;
                    warn("transform: vhost.refer_play to vhost.refer.play for %s", dir->m_name.c_str());
                } else if (n == "refer_publish") {
                    ConfDirective* publish = refer->GetOrCreate("publish");
                    publish->m_args = conf->m_args;
                    warn("transform: vhost.refer_publish to vhost.refer.publish for %s", dir->m_name.c_str());
                }

                // remove the old directive.
                Freep(conf);
                continue;
            }

            // SRS3.0, change the mr style
            //  SRS2:
            //      vhost { mr { enabled; latency; } }
            //  SRS3+:
            //      vhost { publish { mr; mr_latency; } }
            if (n == "mr") {
                it = dir->m_directives.erase(it);

                ConfDirective* publish = dir->GetOrCreate("publish");

                ConfDirective* enabled = conf->Get("enabled");
                if (enabled) {
                    ConfDirective* mr = publish->GetOrCreate("mr");
                    mr->m_args = enabled->m_args;
                    warn("transform: vhost.mr.enabled to vhost.publish.mr for %s", dir->m_name.c_str());
                }

                ConfDirective* latency = conf->Get("latency");
                if (latency) {
                    ConfDirective* mr_latency = publish->GetOrCreate("mr_latency");
                    mr_latency->m_args = latency->m_args;
                    warn("transform: vhost.mr.latency to vhost.publish.mr_latency for %s", dir->m_name.c_str());
                }

                Freep(conf);
                continue;
            }

            // SRS3.0, change the publish_1stpkt_timeout
            //  SRS2:
            //      vhost { publish_1stpkt_timeout; }
            //  SRS3+:
            //      vhost { publish { firstpkt_timeout; } }
            if (n == "publish_1stpkt_timeout") {
                it = dir->m_directives.erase(it);

                ConfDirective* publish = dir->GetOrCreate("publish");

                ConfDirective* firstpkt_timeout = publish->GetOrCreate("firstpkt_timeout");
                firstpkt_timeout->m_args = conf->m_args;
                warn("transform: vhost.publish_1stpkt_timeout to vhost.publish.firstpkt_timeout for %s", dir->m_name.c_str());

                Freep(conf);
                continue;
            }

            // SRS3.0, change the publish_normal_timeout
            //  SRS2:
            //      vhost { publish_normal_timeout; }
            //  SRS3+:
            //      vhost { publish { normal_timeout; } }
            if (n == "publish_normal_timeout") {
                it = dir->m_directives.erase(it);

                ConfDirective* publish = dir->GetOrCreate("publish");

                ConfDirective* normal_timeout = publish->GetOrCreate("normal_timeout");
                normal_timeout->m_args = conf->m_args;
                warn("transform: vhost.publish_normal_timeout to vhost.publish.normal_timeout for %s", dir->m_name.c_str());

                Freep(conf);
                continue;
            }

            // SRS3.0, change the bellow like a shadow:
            //      time_jitter, mix_correct, atc, atc_auto, mw_latency, gop_cache, queue_length
            //  SRS1/2:
            //      vhost { shadow; }
            //  SRS3+:
            //      vhost { play { shadow; } }
            if (n == "time_jitter" || n == "mix_correct" || n == "atc" || n == "atc_auto"
                || n == "mw_latency" || n == "gop_cache" || n == "queue_length" || n == "send_min_interval"
                || n == "reduce_sequence_header") {
                it = dir->m_directives.erase(it);

                ConfDirective* play = dir->GetOrCreate("play");
                ConfDirective* shadow = play->GetOrCreate(conf->m_name);
                shadow->m_args = conf->m_args;
                warn("transform: vhost.%s to vhost.play.%s of %s", n.c_str(), n.c_str(), dir->m_name.c_str());

                Freep(conf);
                continue;
            }

            // SRS3.0, change the forward.
            //  SRS1/2:
            //      vhost { forward target; }
            //  SRS3+:
            //      vhost { forward { enabled; destination target; } }
            if (n == "forward" && conf->m_directives.empty() && !conf->m_args.empty()) {
                conf->GetOrCreate("enabled")->SetArg0("on");

                ConfDirective* destination = conf->GetOrCreate("destination");
                destination->m_args = conf->m_args;
                conf->m_args.clear();
                warn("transform: vhost.forward to vhost.forward.destination for %s", dir->m_name.c_str());

                ++it;
                continue;
            }

            // SRS3.0, change the bellow like a shadow:
            //      mode, origin, token_traverse, vhost, debug_srs_upnode
            //  SRS1/2:
            //      vhost { shadow; }
            //  SRS3+:
            //      vhost { cluster { shadow; } }
            if (n == "mode" || n == "origin" || n == "token_traverse" || n == "vhost" || n == "debug_srs_upnode") {
                it = dir->m_directives.erase(it);

                ConfDirective* cluster = dir->GetOrCreate("cluster");
                ConfDirective* shadow = cluster->GetOrCreate(conf->m_name);
                shadow->m_args = conf->m_args;
                warn("transform: vhost.%s to vhost.cluster.%s of %s", n.c_str(), n.c_str(), dir->m_name.c_str());

                Freep(conf);
                continue;
            }

            // SRS4.0, move nack/twcc to rtc:
            //      vhost { nack {enabled; no_copy;} twcc {enabled} }
            // as:
            //      vhost { rtc { nack on; nack_no_copy on; twcc on; } }
            if (n == "nack" || n == "twcc") {
                it = dir->m_directives.erase(it);

                ConfDirective* rtc = dir->GetOrCreate("rtc");
                if (n == "nack") {
                    if (conf->Get("enabled")) {
                        rtc->GetOrCreate("nack")->m_args = conf->Get("enabled")->m_args;
                    }

                    if (conf->Get("no_copy")) {
                        rtc->GetOrCreate("nack_no_copy")->m_args = conf->Get("no_copy")->m_args;
                    }
                } else if (n == "twcc") {
                    if (conf->Get("enabled")) {
                        rtc->GetOrCreate("twcc")->m_args = conf->Get("enabled")->m_args;
                    }
                }
                warn("transform: vhost.%s to vhost.rtc.%s of %s", n.c_str(), n.c_str(), dir->m_name.c_str());

                Freep(conf);
                continue;
            }

            // SRS3.0, change the forward.
            //  SRS1/2:
            //      vhost { rtc { aac; } }
            //  SRS3+:
            //      vhost { rtc { rtmp_to_rtc; } }
            if (n == "rtc") {
                ConfDirective* aac = conf->Get("aac");
                if (aac) {
                    std::string v = aac->Arg0() == "transcode" ? "on" : "off";
                    conf->GetOrCreate("rtmp_to_rtc")->SetArg0(v);
                    conf->Remove(aac); Freep(aac);
                    warn("transform: vhost.rtc.aac to vhost.rtc.rtmp_to_rtc %s", v.c_str());
                }

                ConfDirective* bframe = conf->Get("bframe");
                if (bframe) {
                    std::string v = bframe->Arg0() == "keep" ? "on" : "off";
                    conf->GetOrCreate("keep_bframe")->SetArg0(v);
                    conf->Remove(bframe); Freep(bframe);
                    warn("transform: vhost.rtc.bframe to vhost.rtc.keep_bframe %s", v.c_str());
                }

                ++it;
                continue;
            }

            ++it;
        }
    }

    return err;
}

// LCOV_EXCL_START
error ConfigDumpsEngine(ConfDirective* dir, JsonObject* engine)
{
    error err = SUCCESS;

    ConfDirective* conf = NULL;

    engine->Set("id", dir->DumpsArg0ToStr());
    engine->Set("enabled", JsonAny::Boolean(config->GetEngineEnabled(dir)));

    if ((conf = dir->Get("iformat")) != NULL) {
        engine->Set("iformat", conf->DumpsArg0ToStr());
    }

    if ((conf = dir->Get("vfilter")) != NULL) {
        JsonObject* vfilter = JsonAny::Object();
        engine->Set("vfilter", vfilter);

        for (int i = 0; i < (int)conf->m_directives.size(); i++) {
            ConfDirective* sdir = conf->m_directives.at(i);
            vfilter->Set(sdir->m_name, sdir->DumpsArg0ToStr());
        }
    }

    if ((conf = dir->Get("vcodec")) != NULL) {
        engine->Set("vcodec", conf->DumpsArg0ToStr());
    }

    if ((conf = dir->Get("vbitrate")) != NULL) {
        engine->Set("vbitrate", conf->DumpsArg0ToInteger());
    }

    if ((conf = dir->Get("vfps")) != NULL) {
        engine->Set("vfps", conf->DumpsArg0ToNumber());
    }

    if ((conf = dir->Get("vwidth")) != NULL) {
        engine->Set("vwidth", conf->DumpsArg0ToInteger());
    }

    if ((conf = dir->Get("vheight")) != NULL) {
        engine->Set("vheight", conf->DumpsArg0ToInteger());
    }

    if ((conf = dir->Get("vthreads")) != NULL) {
        engine->Set("vthreads", conf->DumpsArg0ToInteger());
    }

    if ((conf = dir->Get("vprofile")) != NULL) {
        engine->Set("vprofile", conf->DumpsArg0ToStr());
    }

    if ((conf = dir->Get("vpreset")) != NULL) {
        engine->Set("vpreset", conf->DumpsArg0ToStr());
    }

    if ((conf = dir->Get("vparams")) != NULL) {
        JsonObject* vparams = JsonAny::Object();
        engine->Set("vparams", vparams);

        for (int i = 0; i < (int)conf->m_directives.size(); i++) {
            ConfDirective* sdir = conf->m_directives.at(i);
            vparams->Set(sdir->m_name, sdir->DumpsArg0ToStr());
        }
    }

    if ((conf = dir->Get("acodec")) != NULL) {
        engine->Set("acodec", conf->DumpsArg0ToStr());
    }

    if ((conf = dir->Get("abitrate")) != NULL) {
        engine->Set("abitrate", conf->DumpsArg0ToInteger());
    }

    if ((conf = dir->Get("asample_rate")) != NULL) {
        engine->Set("asample_rate", conf->DumpsArg0ToInteger());
    }

    if ((conf = dir->Get("achannels")) != NULL) {
        engine->Set("achannels", conf->DumpsArg0ToInteger());
    }

    if ((conf = dir->Get("aparams")) != NULL) {
        JsonObject* aparams = JsonAny::Object();
        engine->Set("aparams", aparams);

        for (int i = 0; i < (int)conf->m_directives.size(); i++) {
            ConfDirective* sdir = conf->m_directives.at(i);
            aparams->Set(sdir->m_name, sdir->DumpsArg0ToStr());
        }
    }

    if ((conf = dir->Get("oformat")) != NULL) {
        engine->Set("oformat", conf->DumpsArg0ToStr());
    }

    if ((conf = dir->Get("output")) != NULL) {
        engine->Set("output", conf->DumpsArg0ToStr());
    }

    return err;
}
// LCOV_EXCL_STOP

ConfDirective::ConfDirective()
{
    m_confLine = 0;
}

ConfDirective::~ConfDirective()
{
    std::vector<ConfDirective*>::iterator it;
    for (it = m_directives.begin(); it != m_directives.end(); ++it) {
        ConfDirective* directive = *it;
        Freep(directive);
    }
    m_directives.clear();
}

ConfDirective *ConfDirective::Copy()
{
    return Copy("");
}

ConfDirective *ConfDirective::Copy(std::string except)
{
    ConfDirective* cp = new ConfDirective();

    cp->m_confLine = m_confLine;
    cp->m_name = m_name;
    cp->m_args = m_args;

    for (int i = 0; i < (int)m_directives.size(); i++) {
        ConfDirective* directive = m_directives.at(i);
        if (!except.empty() && directive->m_name == except) {
            continue;
        }
        cp->m_directives.push_back(directive->Copy(except));
    }

    return cp;
}

std::string ConfDirective::Arg0()
{
    if (m_args.size() > 0) {
        return m_args.at(0);
    }

    return "";
}

std::string ConfDirective::Arg1()
{
    if (m_args.size() > 1) {
        return m_args.at(1);
    }

    return "";
}

std::string ConfDirective::Arg2()
{
    if (m_args.size() > 2) {
        return m_args.at(2);
    }

    return "";
}

std::string ConfDirective::Arg3()
{
    if (m_args.size() > 3) {
        return m_args.at(3);
    }

    return "";
}

ConfDirective *ConfDirective::At(int index)
{
    Assert(index < (int)m_directives.size());
    return m_directives.at(index);
}

ConfDirective *ConfDirective::Get(std::string _name)
{
    std::vector<ConfDirective*>::iterator it;
    for (it = m_directives.begin(); it != m_directives.end(); ++it) {
        ConfDirective* directive = *it;
        if (directive->m_name == _name) {
            return directive;
        }
    }

    return NULL;
}

ConfDirective *ConfDirective::Get(std::string _name, std::string _arg0)
{
    std::vector<ConfDirective*>::iterator it;
    for (it = m_directives.begin(); it != m_directives.end(); ++it) {
        ConfDirective* directive = *it;
        if (directive->m_name == _name && directive->Arg0() == _arg0) {
            return directive;
        }
    }

    return NULL;
}

ConfDirective *ConfDirective::GetOrCreate(std::string n)
{
    ConfDirective* conf = Get(n);

    if (!conf) {
        conf = new ConfDirective();
        conf->m_name = n;
        m_directives.push_back(conf);
    }

    return conf;
}

ConfDirective *ConfDirective::GetOrCreate(std::string n, std::string a0)
{
    ConfDirective* conf = Get(n, a0);

    if (!conf) {
        conf = new ConfDirective();
        conf->m_name = n;
        conf->m_args.push_back(a0);
        m_directives.push_back(conf);
    }

    return conf;
}

ConfDirective *ConfDirective::GetOrCreate(std::string n, std::string a0, std::string a1)
{
    ConfDirective* conf = Get(n, a0);

    if (!conf) {
        conf = new ConfDirective();
        conf->m_name = n;
        conf->m_args.push_back(a0);
        conf->m_args.push_back(a1);
        m_directives.push_back(conf);
    }

    return conf;
}

ConfDirective *ConfDirective::SetArg0(std::string a0)
{
    if (Arg0() == a0) {
        return this;
    }

    // update a0.
    if (!m_args.empty()) {
        m_args.erase(m_args.begin());
    }

    m_args.insert(m_args.begin(), a0);

    return this;
}

void ConfDirective::Remove(ConfDirective *v)
{
    std::vector<ConfDirective*>::iterator it;
    if ((it = std::find(m_directives.begin(), m_directives.end(), v)) != m_directives.end()) {
        it = m_directives.erase(it);
    }
}

bool ConfDirective::IsVhost()
{
    return m_name == "vhost";
}

bool ConfDirective::IsStreamCaster()
{
    return m_name == "stream_caster";
}

error ConfDirective::Parse(internal::ConfigBuffer *buffer, Config *conf)
{
    return ParseConf(buffer, DirectiveContextFile, conf);
}

error ConfDirective::Persistence(FileWriter *writer, int level)
{
    error err = SUCCESS;

    static char SPACE = CONSTS_SP;
    static char SEMICOLON = CONSTS_SE;
    static char LF = CONSTS_LF;
    static char LB = CONSTS_LB;
    static char RB = CONSTS_RB;
    static const char* INDENT = "    ";

    // for level0 directive, only contains sub directives.
    if (level > 0) {
        // indent by (level - 1) * 4 space.
        for (int i = 0; i < level - 1; i++) {
            if ((err = writer->Write((char*)INDENT, 4, NULL)) != SUCCESS) {
                return ERRORWRAP(err, "write indent");
            }
        }

        // directive m_name.
        if ((err = writer->Write((char*)m_name.c_str(), (int)m_name.length(), NULL)) != SUCCESS) {
            return ERRORWRAP(err, "write m_name");
        }
        if (!m_args.empty() && (err = writer->Write((char*)&SPACE, 1, NULL)) != SUCCESS) {
            return ERRORWRAP(err, "write m_name space");
        }

        // directive args.
        for (int i = 0; i < (int)m_args.size(); i++) {
            std::string& arg = m_args.at(i);
            if ((err = writer->Write((char*)arg.c_str(), (int)arg.length(), NULL)) != SUCCESS) {
                return ERRORWRAP(err, "write arg");
            }
            if (i < (int)m_args.size() - 1 && (err = writer->Write((char*)&SPACE, 1, NULL)) != SUCCESS) {
                return ERRORWRAP(err, "write arg space");
            }
        }

        // native directive, without sub directives.
        if (m_directives.empty()) {
            if ((err = writer->Write((char*)&SEMICOLON, 1, NULL)) != SUCCESS) {
                return ERRORWRAP(err, "write arg semicolon");
            }
        }
    }

    // persistence all sub directives.
    if (level > 0) {
        if (!m_directives.empty()) {
            if ((err = writer->Write((char*)&SPACE, 1, NULL)) != SUCCESS) {
                return ERRORWRAP(err, "write sub-dir space");
            }
            if ((err = writer->Write((char*)&LB, 1, NULL)) != SUCCESS) {
                return ERRORWRAP(err, "write sub-dir left-brace");
            }
        }

        if ((err = writer->Write((char*)&LF, 1, NULL)) != SUCCESS) {
            return ERRORWRAP(err, "write sub-dir linefeed");
        }
    }

    for (int i = 0; i < (int)m_directives.size(); i++) {
        ConfDirective* dir = m_directives.at(i);
        if ((err = dir->Persistence(writer, level + 1)) != SUCCESS) {
            return ERRORWRAP(err, "sub-dir %s", dir->m_name.c_str());
        }
    }

    if (level > 0 && !m_directives.empty()) {
        // indent by (level - 1) * 4 space.
        for (int i = 0; i < level - 1; i++) {
            if ((err = writer->Write((char*)INDENT, 4, NULL)) != SUCCESS) {
                return ERRORWRAP(err, "write sub-dir indent");
            }
        }

        if ((err = writer->Write((char*)&RB, 1, NULL)) != SUCCESS) {
            return ERRORWRAP(err, "write sub-dir right-brace");
        }

        if ((err = writer->Write((char*)&LF, 1, NULL)) != SUCCESS) {
            return ERRORWRAP(err, "write sub-dir linefeed");
        }
    }


    return err;
}

JsonArray *ConfDirective::DumpsArgs()
{
    JsonArray* arr = JsonAny::Array();
    for (int i = 0; i < (int)m_args.size(); i++) {
        std::string arg = m_args.at(i);
        arr->Append(JsonAny::Str(arg.c_str()));
    }
    return arr;
}

JsonAny *ConfDirective::DumpsArg0ToStr()
{
    return JsonAny::Str(Arg0().c_str());
}

JsonAny *ConfDirective::DumpsArg0ToInteger()
{
    return JsonAny::Integer(::atoll(Arg0().c_str()));
}

JsonAny *ConfDirective::DumpsArg0ToNumber()
{
    return JsonAny::Number(::atof(Arg0().c_str()));
}

JsonAny *ConfDirective::DumpsArg0ToBoolean()
{
    return JsonAny::Boolean(Arg0() == "on");
}

error ConfDirective::ParseConf(internal::ConfigBuffer *buffer, DirectiveContext ctx, Config *conf)
{
    error err = SUCCESS;

    while (true) {
        std::vector<std::string> args;
        int line_start = 0;
        DirectiveState state = DirectiveStateInit;
        if ((err = ReadToken(buffer, args, line_start, state)) != SUCCESS) {
            return ERRORWRAP(err, "read token, line=%d, state=%d", line_start, state);
        }

        if (state == DirectiveStateBlockEnd) {
            return ctx == DirectiveContextBlock ? SUCCESS : ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "line %d: unexpected \"}\"", buffer->m_line);
        }
        if (state == DirectiveStateEOF) {
            return ctx != DirectiveContextBlock ? SUCCESS : ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "line %d: unexpected end of file, expecting \"}\"", m_confLine);
        }
        if (args.empty()) {
            return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "line %d: empty directive", m_confLine);
        }

        // Build normal directive which is not "include".
        if (args.at(0) != "include") {
            ConfDirective* directive = new ConfDirective();

            directive->m_confLine = line_start;
            directive->m_name = args[0];
            args.erase(args.begin());
            directive->m_args.swap(args);

            m_directives.push_back(directive);

            if (state == DirectiveStateBlockStart) {
                if ((err = directive->ParseConf(buffer, DirectiveContextBlock, conf)) != SUCCESS) {
                    return ERRORWRAP(err, "parse dir");
                }
            }
            continue;
        }

        // Parse including, allow multiple files.
        std::vector<std::string> files(args.begin() + 1, args.end());
        if (files.empty()) {
            return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "line %d: include is empty directive", buffer->m_line);
        }
        if (!conf) {
            return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "line %d: no config", buffer->m_line);
        }

        for (int i = 0; i < (int)files.size(); i++) {
            std::string file = files.at(i);
            Assert(!file.empty());
            trace("config parse include %s", file.c_str());

            ConfigBuffer* include_file_buffer = NULL;
            AutoFree(ConfigBuffer, include_file_buffer);
            if ((err = conf->BuildBuffer(file, &include_file_buffer)) != SUCCESS) {
                return ERRORWRAP(err, "buffer fullfill %s", file.c_str());
            }

            if ((err = ParseConf(include_file_buffer, DirectiveContextFile, conf)) != SUCCESS) {
                return ERRORWRAP(err, "parse include buffer");
            }
        }
    }

    return err;
}

error ConfDirective::ReadToken(internal::ConfigBuffer *buffer, std::vector<std::string> &args, int &line_start, DirectiveState &state)
{
    error err = SUCCESS;

    char* pstart = buffer->m_pos;

    bool sharp_comment = false;

    bool d_quoted = false;
    bool s_quoted = false;

    bool need_space = false;
    bool last_space = true;

    while (true) {
        if (buffer->Empty()) {
            if (!args.empty() || !last_space) {
                return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID,
                    "line %d: unexpected end of file, expecting ; or \"}\"",
                    buffer->m_line);
            }
            trace("config parse complete");

            state = DirectiveStateEOF;
            return err;
        }

        char ch = *buffer->m_pos++;

        if (ch == _LF) {
            buffer->m_line++;
            sharp_comment = false;
        }

        if (sharp_comment) {
            continue;
        }

        if (need_space) {
            if (IsCommonSpace(ch)) {
                last_space = true;
                need_space = false;
                continue;
            }
            if (ch == ';') {
                state = DirectiveStateEntire;
                return err;
            }
            if (ch == '{') {
                state = DirectiveStateBlockStart;
                return err;
            }
            return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "line %d: unexpected '%c'", buffer->m_line, ch);
        }

        // last charecter is space.
        if (last_space) {
            if (IsCommonSpace(ch)) {
                continue;
            }
            pstart = buffer->m_pos - 1;
            switch (ch) {
                case ';':
                    if (args.size() == 0) {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "line %d: unexpected ';'", buffer->m_line);
                    }
                    state = DirectiveStateEntire;
                    return err;
                case '{':
                    if (args.size() == 0) {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "line %d: unexpected '{'", buffer->m_line);
                    }
                    state = DirectiveStateBlockStart;
                    return err;
                case '}':
                    if (args.size() != 0) {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "line %d: unexpected '}'", buffer->m_line);
                    }
                    state = DirectiveStateBlockEnd;
                    return err;
                case '#':
                    sharp_comment = 1;
                    continue;
                case '"':
                    pstart++;
                    d_quoted = true;
                    last_space = 0;
                    continue;
                case '\'':
                    pstart++;
                    s_quoted = true;
                    last_space = 0;
                    continue;
                default:
                    last_space = 0;
                    continue;
            }
        } else {
            // last charecter is not space
            if (line_start == 0) {
                line_start = buffer->m_line;
            }

            bool found = false;
            if (d_quoted) {
                if (ch == '"') {
                    d_quoted = false;
                    need_space = true;
                    found = true;
                }
            } else if (s_quoted) {
                if (ch == '\'') {
                    s_quoted = false;
                    need_space = true;
                    found = true;
                }
            } else if (IsCommonSpace(ch) || ch == ';' || ch == '{') {
                last_space = true;
                found = 1;
            }

            if (found) {
                int len = (int)(buffer->m_pos - pstart);
                char* aword = new char[len];
                memcpy(aword, pstart, len);
                aword[len - 1] = 0;

                std::string word_str = aword;
                if (!word_str.empty()) {
                    args.push_back(word_str);
                }
                Freepa(aword);

                if (ch == ';') {
                    state = DirectiveStateEntire;
                    return err;
                }
                if (ch == '{') {
                    state = DirectiveStateBlockStart;
                    return err;
                }
            }
        }
    }

    return err;
}

Config::Config()
{
    m_envOnly = false;

    m_showHelp = false;
    m_showVersion = false;
    m_testConf = false;
    m_showSignature = false;

    m_root = new ConfDirective();
    m_root->m_confLine = 0;
    m_root->m_name = "root";
}

Config::~Config()
{
    Freep(m_root);
}

void Config::Subscribe(IReloadHandler *handler)
{
    std::vector<IReloadHandler*>::iterator it;

    it = std::find(m_subscribes.begin(), m_subscribes.end(), handler);
    if (it != m_subscribes.end()) {
        return;
    }

    m_subscribes.push_back(handler);
}

void Config::Unsubscribe(IReloadHandler *handler)
{
    std::vector<IReloadHandler*>::iterator it;

    it = std::find(m_subscribes.begin(), m_subscribes.end(), handler);
    if (it == m_subscribes.end()) {
        return;
    }

    it = m_subscribes.erase(it);
}

error Config::Reload()
{
    error err = SUCCESS;

    Config conf;

    if ((err = conf.ParseFile(m_configFile.c_str())) != SUCCESS) {
        return ERRORWRAP(err, "parse file");
    }
    info("config reloader parse file success.");

    // transform config to compatible with previous style of config.
    if ((err = ConfigTransformVhost(conf.m_root)) != SUCCESS) {
        return ERRORWRAP(err, "transform config");
    }

    if ((err = conf.CheckConfig()) != SUCCESS) {
        return ERRORWRAP(err, "check config");
    }

    if ((err = ReloadConf(&conf)) != SUCCESS) {
        return ERRORWRAP(err, "reload config");
    }

    return err;
}

error Config::ReloadVhost(ConfDirective *old_root)
{
    error err = SUCCESS;

    // merge config.
    std::vector<IReloadHandler*>::iterator it;

    // following directly support reload.
    //      origin, token_traverse, vhost, debug_srs_upnode

    // state graph
    //      old_vhost       new_vhost
    //      DISABLED    =>  ENABLED
    //      ENABLED     =>  DISABLED
    //      ENABLED     =>  ENABLED (modified)

    // collect all vhost m_names
    std::vector<std::string> vhosts;
    for (int i = 0; i < (int)m_root->m_directives.size(); i++) {
        ConfDirective* vhost = m_root->At(i);
        if (vhost->m_name != "vhost") {
            continue;
        }
        vhosts.push_back(vhost->Arg0());
    }
    for (int i = 0; i < (int)old_root->m_directives.size(); i++) {
        ConfDirective* vhost = old_root->At(i);
        if (vhost->m_name != "vhost") {
            continue;
        }
        if (m_root->Get("vhost", vhost->Arg0())) {
            continue;
        }
        vhosts.push_back(vhost->Arg0());
    }

    // process each vhost
    for (int i = 0; i < (int)vhosts.size(); i++) {
        std::string vhost = vhosts.at(i);

        ConfDirective* old_vhost = old_root->Get("vhost", vhost);
        ConfDirective* new_vhost = m_root->Get("vhost", vhost);

        //      DISABLED    =>  ENABLED
        if (!GetVhostEnabled(old_vhost) && GetVhostEnabled(new_vhost)) {
            if ((err = DoReloadVhostAdded(vhost)) != SUCCESS) {
                return ERRORWRAP(err, "reload vhost added");
            }
            continue;
        }

        //      ENABLED     =>  DISABLED
        if (GetVhostEnabled(old_vhost) && !GetVhostEnabled(new_vhost)) {
            if ((err = DoReloadVhostRemoved(vhost)) != SUCCESS) {
                return ERRORWRAP(err, "reload vhost removed");
            }
            continue;
        }

        // cluster.mode, never supports reload.
        // first, for the origin and edge role change is too complex.
        // second, the vhosts in origin device group normally are all origin,
        //      they never change to edge sometimes.
        // third, the origin or upnode device can always be restart,
        //      edge will retry and the users connected to edge are ok.
        // it's ok to add or remove edge/origin vhost.
        if (GetVhostIsEdge(old_vhost) != GetVhostIsEdge(new_vhost)) {
            return ERRORNEW(ERROR_RTMP_EDGE_RELOAD, "vhost mode changed");
        }

        // the auto reload configs:
        //      publish.parse_sps

        //      ENABLED     =>  ENABLED (modified)
        if (GetVhostEnabled(new_vhost) && GetVhostEnabled(old_vhost)) {
            trace("vhost %s maybe modified, reload its detail.", vhost.c_str());
            // chunk_size, only one per vhost.
            if (!DirectiveEquals(new_vhost->Get("chunk_size"), old_vhost->Get("chunk_size"))) {
                for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
                    IReloadHandler* subscribe = *it;
                    if ((err = subscribe->OnReloadVhostChunkSize(vhost)) != SUCCESS) {
                        return ERRORWRAP(err, "vhost %s notify subscribes chunk_size failed", vhost.c_str());
                    }
                }
                trace("vhost %s reload chunk_size success.", vhost.c_str());
            }

            // tcp_nodelay, only one per vhost
            if (!DirectiveEquals(new_vhost->Get("tcp_nodelay"), old_vhost->Get("tcp_nodelay"))) {
                for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
                    IReloadHandler* subscribe = *it;
                    if ((err = subscribe->OnReloadVhostTcpNodelay(vhost)) != SUCCESS) {
                        return ERRORWRAP(err, "vhost %s notify subscribes tcp_nodelay failed", vhost.c_str());
                    }
                }
                trace("vhost %s reload tcp_nodelay success.", vhost.c_str());
            }

            // min_latency, only one per vhost
            if (!DirectiveEquals(new_vhost->Get("min_latency"), old_vhost->Get("min_latency"))) {
                for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
                    IReloadHandler* subscribe = *it;
                    if ((err = subscribe->OnReloadVhostRealtime(vhost)) != SUCCESS) {
                        return ERRORWRAP(err, "vhost %s notify subscribes min_latency failed", vhost.c_str());
                    }
                }
                trace("vhost %s reload min_latency success.", vhost.c_str());
            }

            // play, only one per vhost
            if (!DirectiveEquals(new_vhost->Get("play"), old_vhost->Get("play"))) {
                for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
                    IReloadHandler* subscribe = *it;
                    if ((err = subscribe->OnReloadVhostPlay(vhost)) != SUCCESS) {
                        return ERRORWRAP(err, "vhost %s notify subscribes play failed", vhost.c_str());
                    }
                }
                trace("vhost %s reload play success.", vhost.c_str());
            }

            // forward, only one per vhost
            if (!DirectiveEquals(new_vhost->Get("forward"), old_vhost->Get("forward"))) {
                for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
                    IReloadHandler* subscribe = *it;
                    if ((err = subscribe->OnReloadVhostForward(vhost)) != SUCCESS) {
                        return ERRORWRAP(err, "vhost %s notify subscribes forward failed", vhost.c_str());
                    }
                }
                trace("vhost %s reload forward success.", vhost.c_str());
            }

            // To reload DASH.
            if (!DirectiveEquals(new_vhost->Get("dash"), old_vhost->Get("dash"))) {
                for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
                    IReloadHandler* subscribe = *it;
                    if ((err = subscribe->OnReloadVhostDash(vhost)) != SUCCESS) {
                        return ERRORWRAP(err, "Reload vhost %s dash failed", vhost.c_str());
                    }
                }
                trace("Reload vhost %s dash ok.", vhost.c_str());
            }

            // hls, only one per vhost
            // @remark, the hls_on_error directly support reload.
            if (!DirectiveEquals(new_vhost->Get("hls"), old_vhost->Get("hls"))) {
                for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
                    IReloadHandler* subscribe = *it;
                    if ((err = subscribe->OnReloadVhostHls(vhost)) != SUCCESS) {
                        return ERRORWRAP(err, "vhost %s notify subscribes hls failed", vhost.c_str());
                    }
                }
                trace("vhost %s reload hls success.", vhost.c_str());
            }

            // hds reload
            if (!DirectiveEquals(new_vhost->Get("hds"), old_vhost->Get("hds"))) {
                for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
                    IReloadHandler* subscribe = *it;
                    if ((err = subscribe->OnReloadVhostHds(vhost)) != SUCCESS) {
                        return ERRORWRAP(err, "vhost %s notify subscribes hds failed", vhost.c_str());
                    }
                }
                trace("vhost %s reload hds success.", vhost.c_str());
            }

            // dvr, only one per vhost, except the dvr_apply
            if (!DirectiveEquals(new_vhost->Get("dvr"), old_vhost->Get("dvr"), "dvr_apply")) {
                for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
                    IReloadHandler* subscribe = *it;
                    if ((err = subscribe->OnReloadVhostDvr(vhost)) != SUCCESS) {
                        return ERRORWRAP(err, "vhost %s notify subscribes dvr failed", vhost.c_str());
                    }
                }
                trace("vhost %s reload dvr success.", vhost.c_str());
            }

            // exec, only one per vhost
            if (!DirectiveEquals(new_vhost->Get("exec"), old_vhost->Get("exec"))) {
                for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
                    IReloadHandler* subscribe = *it;
                    if ((err = subscribe->OnReloadVhostExec(vhost)) != SUCCESS) {
                        return ERRORWRAP(err, "vhost %s notify subscribes exec failed", vhost.c_str());
                    }
                }
                trace("vhost %s reload exec success.", vhost.c_str());
            }

            // publish, only one per vhost
            if (!DirectiveEquals(new_vhost->Get("publish"), old_vhost->Get("publish"))) {
                for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
                    IReloadHandler* subscribe = *it;
                    if ((err = subscribe->OnReloadVhostPublish(vhost)) != SUCCESS) {
                        return ERRORWRAP(err, "vhost %s notify subscribes publish failed", vhost.c_str());
                    }
                }
                trace("vhost %s reload publish success.", vhost.c_str());
            }

            // transcode, many per vhost.
            if ((err = ReloadTranscode(new_vhost, old_vhost)) != SUCCESS) {
                return ERRORWRAP(err, "reload transcode");
            }

            // ingest, many per vhost.
            if ((err = ReloadIngest(new_vhost, old_vhost)) != SUCCESS) {
                return ERRORWRAP(err, "reload ingest");
            }
            continue;
        }
        trace("ignore reload vhost, enabled old: %d, new: %d",
                  GetVhostEnabled(old_vhost), GetVhostEnabled(new_vhost));
    }

    return err;
}

error Config::ReloadConf(Config *conf)
{
    error err = SUCCESS;

    ConfDirective* old_root = m_root;
    AutoFree(ConfDirective, old_root);

    m_root = conf->m_root;
    conf->m_root = NULL;

    // never support reload:
    //      daemon
    //
    // always support reload without additional code:
    //      chunk_size, ff_log_dir,
    //      http_hooks, heartbeat,
    //      security

    // merge config: listen
    if (!DirectiveEquals(m_root->Get("listen"), old_root->Get("listen"))) {
        if ((err = DoReloadListen()) != SUCCESS) {
            return ERRORWRAP(err, "listen");
        }
    }

    // merge config: max_connections
    if (!DirectiveEquals(m_root->Get("max_connections"), old_root->Get("max_connections"))) {
        if ((err = DoReloadMaxConnections()) != SUCCESS) {
            return ERRORWRAP(err, "max connections");;
        }
    }

    // merge config: pithy_print_ms
    if (!DirectiveEquals(m_root->Get("pithy_print_ms"), old_root->Get("pithy_print_ms"))) {
        if ((err = DoReloadPithyPrintMs()) != SUCCESS) {
            return ERRORWRAP(err, "pithy print ms");;
        }
    }

    // Merge config: rtc_server
    if ((err = ReloadRtcServer(old_root)) != SUCCESS) {
        return ERRORWRAP(err, "http steram");;
    }

    // TODO: FIXME: support reload stream_caster.

    // merge config: vhost
    if ((err = ReloadVhost(old_root)) != SUCCESS) {
        return ERRORWRAP(err, "vhost");;
    }

    return err;
}

error Config::ReloadRtcServer(ConfDirective *old_root)
{
    error err = SUCCESS;

    // merge config.
    std::vector<IReloadHandler*>::iterator it;

    // state graph
    //      old_rtc_server     new_rtc_server
    //      ENABLED     =>      ENABLED (modified)

    ConfDirective* new_rtc_server = m_root->Get("rtc_server");
    ConfDirective* old_rtc_server = old_root->Get("rtc_server");

    // TODO: FIXME: Support disable or enable reloading.

    //      ENABLED     =>  ENABLED (modified)
    if (GetRtcServerEnabled(old_rtc_server) && GetRtcServerEnabled(new_rtc_server)
        && !DirectiveEquals(old_rtc_server, new_rtc_server)
        ) {
        for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
            IReloadHandler* subscribe = *it;
            if ((err = subscribe->OnReloadRtcServer()) != SUCCESS) {
                return ERRORWRAP(err, "rtc server enabled");
            }
        }
        trace("reload rtc server success.");
        return err;
    }

    trace("reload rtc server success, nothing changed.");
    return err;
}

error Config::ReloadTranscode(ConfDirective *new_vhost, ConfDirective *old_vhost)
{
    error err = SUCCESS;

    std::vector<ConfDirective*> old_transcoders;
    for (int i = 0; i < (int)old_vhost->m_directives.size(); i++) {
        ConfDirective* conf = old_vhost->At(i);
        if (conf->m_name == "transcode") {
            old_transcoders.push_back(conf);
        }
    }

    std::vector<ConfDirective*> new_transcoders;
    for (int i = 0; i < (int)new_vhost->m_directives.size(); i++) {
        ConfDirective* conf = new_vhost->At(i);
        if (conf->m_name == "transcode") {
            new_transcoders.push_back(conf);
        }
    }

    std::vector<IReloadHandler*>::iterator it;

    std::string vhost = new_vhost->Arg0();

    // to be simple:
    // whatever, once tiny changed of transcode,
    // restart all ffmpeg of vhost.
    bool changed = false;

    // discovery the removed ffmpeg.
    for (int i = 0; !changed && i < (int)old_transcoders.size(); i++) {
        ConfDirective* old_transcoder = old_transcoders.at(i);
        std::string transcoder_id = old_transcoder->Arg0();

        // if transcoder exists in new vhost, not removed, ignore.
        if (new_vhost->Get("transcode", transcoder_id)) {
            continue;
        }

        changed = true;
    }

    // discovery the added ffmpeg.
    for (int i = 0; !changed && i < (int)new_transcoders.size(); i++) {
        ConfDirective* new_transcoder = new_transcoders.at(i);
        std::string transcoder_id = new_transcoder->Arg0();

        // if transcoder exists in old vhost, not added, ignore.
        if (old_vhost->Get("transcode", transcoder_id)) {
            continue;
        }

        changed = true;
    }

    // for updated transcoders, restart them.
    for (int i = 0; !changed && i < (int)new_transcoders.size(); i++) {
        ConfDirective* new_transcoder = new_transcoders.at(i);
        std::string transcoder_id = new_transcoder->Arg0();
        ConfDirective* old_transcoder = old_vhost->Get("transcode", transcoder_id);
        Assert(old_transcoder);

        if (DirectiveEquals(new_transcoder, old_transcoder)) {
            continue;
        }

        changed = true;
    }

    // transcode, many per vhost
    if (changed) {
        for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
            IReloadHandler* subscribe = *it;
            if ((err = subscribe->OnReloadVhostTranscode(vhost)) != SUCCESS) {
                return ERRORWRAP(err, "vhost %s notify subscribes transcode failed", vhost.c_str());
            }
        }
        trace("vhost %s reload transcode success.", vhost.c_str());
    }

    return err;
}

error Config::ReloadIngest(ConfDirective *new_vhost, ConfDirective *old_vhost)
{
    error err = SUCCESS;

    std::vector<ConfDirective*> old_ingesters;
    for (int i = 0; i < (int)old_vhost->m_directives.size(); i++) {
        ConfDirective* conf = old_vhost->At(i);
        if (conf->m_name == "ingest") {
            old_ingesters.push_back(conf);
        }
    }

    std::vector<ConfDirective*> new_ingesters;
    for (int i = 0; i < (int)new_vhost->m_directives.size(); i++) {
        ConfDirective* conf = new_vhost->At(i);
        if (conf->m_name == "ingest") {
            new_ingesters.push_back(conf);
        }
    }

    std::vector<IReloadHandler*>::iterator it;

    std::string vhost = new_vhost->Arg0();

    // for removed ingesters, stop them.
    for (int i = 0; i < (int)old_ingesters.size(); i++) {
        ConfDirective* old_ingester = old_ingesters.at(i);
        std::string ingest_id = old_ingester->Arg0();
        ConfDirective* new_ingester = new_vhost->Get("ingest", ingest_id);

        // ENABLED => DISABLED
        if (GetIngestEnabled(old_ingester) && !GetIngestEnabled(new_ingester)) {
            // notice handler ingester removed.
            for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
                IReloadHandler* subscribe = *it;
                if ((err = subscribe->OnReloadIngestRemoved(vhost, ingest_id)) != SUCCESS) {
                    return ERRORWRAP(err, "vhost %s notify subscribes ingest=%s removed failed", vhost.c_str(), ingest_id.c_str());
                }
            }
            trace("vhost %s reload ingest=%s removed success.", vhost.c_str(), ingest_id.c_str());
        }
    }

    // for added ingesters, start them.
    for (int i = 0; i < (int)new_ingesters.size(); i++) {
        ConfDirective* new_ingester = new_ingesters.at(i);
        std::string ingest_id = new_ingester->Arg0();
        ConfDirective* old_ingester = old_vhost->Get("ingest", ingest_id);

        // DISABLED => ENABLED
        if (!GetIngestEnabled(old_ingester) && GetIngestEnabled(new_ingester)) {
            for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
                IReloadHandler* subscribe = *it;
                if ((err = subscribe->OnReloadIngestAdded(vhost, ingest_id)) != SUCCESS) {
                    return ERRORWRAP(err, "vhost %s notify subscribes ingest=%s added failed", vhost.c_str(), ingest_id.c_str());
                }
            }
            trace("vhost %s reload ingest=%s added success.", vhost.c_str(), ingest_id.c_str());
        }
    }

    // for updated ingesters, restart them.
    for (int i = 0; i < (int)new_ingesters.size(); i++) {
        ConfDirective* new_ingester = new_ingesters.at(i);
        std::string ingest_id = new_ingester->Arg0();
        ConfDirective* old_ingester = old_vhost->Get("ingest", ingest_id);

        // ENABLED => ENABLED
        if (GetIngestEnabled(old_ingester) && GetIngestEnabled(new_ingester)) {
            if (DirectiveEquals(new_ingester, old_ingester)) {
                continue;
            }

            // notice handler ingester removed.
            for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
                IReloadHandler* subscribe = *it;
                if ((err = subscribe->OnReloadIngestUpdated(vhost, ingest_id)) != SUCCESS) {
                    return ERRORWRAP(err, "vhost %s notify subscribes ingest=%s updated failed", vhost.c_str(), ingest_id.c_str());
                }
            }
            trace("vhost %s reload ingest=%s updated success.", vhost.c_str(), ingest_id.c_str());
        }
    }

    trace("ingest nothing changed for vhost=%s", vhost.c_str());

    return err;
}

error Config::ParseOptions(int argc, char **argv)
{
    error err = SUCCESS;

    // argv
    for (int i = 0; i < argc; i++) {
        m_argv.append(argv[i]);

        if (i < argc - 1) {
            m_argv.append(" ");
        }
    }

    // Show help if has any argv
    m_showHelp = argc > 1;
    for (int i = 1; i < argc; i++) {
        if ((err = ParseArgv(i, argv)) != SUCCESS) {
            return ERRORWRAP(err, "parse argv");
        }
    }

    if (m_showHelp) {
        PrintHelp(argv);
        exit(0);
    }

    if (m_showVersion) {
        fprintf(stderr, "%s\n", RTMP_SIG_VERSION);
        exit(0);
    }
    if (m_showSignature) {
        fprintf(stderr, "%s\n", RTMP_SIG_SERVER);
        exit(0);
    }

    // first hello message.
    trace(_version);

    // Config the env_only_ by env.
    if (getenv("ENV_ONLY")) m_envOnly = true;

    // Try config files as bellow:
    //      User specified config(not empty), like user/docker.conf
    //      If user specified *docker.conf, try *srs.conf, like user/srs.conf
    //      Try the default srs config, defined as SRS_CONF_DEFAULT_COFNIG_FILE, like conf/srs.conf
    //      Try config for FHS, like /etc/srs/srs.conf @see https://github.com/ossrs/srs/pull/2711
    if (!m_envOnly) {
        std::vector<std::string> try_config_files;
        if (!m_configFile.empty()) {
            try_config_files.push_back(m_configFile);
        }
        if (StringEndsWith(m_configFile, "docker.conf")) {
            try_config_files.push_back(StringReplace(m_configFile, "docker.conf", "srs.conf"));
        }
        try_config_files.push_back(CONF_DEFAULT_COFNIG_FILE);
        try_config_files.push_back("/etc/srs/srs.conf");

        // Match the first exists file.
        std::string exists_config_file;
        for (int i = 0; i < (int) try_config_files.size(); i++) {
            std::string try_config_file = try_config_files.at(i);
            if (PathExists(try_config_file)) {
                exists_config_file = try_config_file;
                break;
            }
        }

        if (exists_config_file.empty()) {
            return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "no config file at %s", JoinVectorString(try_config_files, ", ").c_str());
        }

        if (m_configFile != exists_config_file) {
            warn("user config %s does not exists, use %s instead", m_configFile.c_str(), exists_config_file.c_str());
            m_configFile = exists_config_file;
        }
    }

    // Overwrite the config by env SRS_CONFIG_FILE.
    if (!Getenv("srs.config.file").empty()) {
        std::string ov = m_configFile;
        m_configFile = Getenv("srs.config.file");
        trace("ENV: Overwrite config %s to %s", ov.c_str(), m_configFile.c_str());
    }

    // Parse the matched config file.
    if (!m_envOnly) {
        err = ParseFile(m_configFile.c_str());
    }

    if (m_testConf) {
        // the parse_file never check the config,
        // we check it when user requires check config file.
        if (err == SUCCESS && (err = ConfigTransformVhost(m_root)) == SUCCESS) {
            if ((err = CheckConfig()) == SUCCESS) {
                trace("the config file %s syntax is ok", m_configFile.c_str());
                trace("config file %s test is successful", m_configFile.c_str());
                exit(0);
            }
        }

        trace("invalid config%s in %s", ERRORSUMMARY(err).c_str(), m_configFile.c_str());
        trace("config file %s test is failed", m_configFile.c_str());
        exit(ERRORCODE(err));
    }

    if (err != SUCCESS) {
        return ERRORWRAP(err, "invalid config");
    }

    // transform config to compatible with previous style of config.
    if ((err = ConfigTransformVhost(m_root)) != SUCCESS) {
        return ERRORWRAP(err, "transform");
    }

    // If use env only, we set change to daemon(off) and console log.
    if (m_envOnly) {
        if (!getenv("SRS_DAEMON")) setenv("SRS_DAEMON", "off", 1);
        if (!getenv("SRS_LOG_TANK")) setenv("SRS_LOG_TANK", "console", 1);
        if (m_root->m_directives.empty()) m_root->GetOrCreate("vhost", "__defaultVhost__");
    }

    ////////////////////////////////////////////////////////////////////////
    // check log m_name and level
    ////////////////////////////////////////////////////////////////////////
    if (true) {
        std::string log_filename = this->GetLogFile();
        if (GetLogTankFile() && log_filename.empty()) {
            return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "no log file");
        }
        if (GetLogTankFile()) {
            trace("you can check log by: tail -n 30 -f %s", log_filename.c_str());
            trace("please check SRS by: ./etc/init.d/srs status");
        } else {
            trace("write log to console");
        }
    }

    return err;
}

error Config::InitializeCwd()
{
    // cwd
    char cwd[256];
    getcwd(cwd, sizeof(cwd));
    m_cwd = cwd;

    return SUCCESS;
}

error Config::Persistence()
{
    error err = SUCCESS;

    // write to a tmp file, then mv to the config.
    std::string path = m_configFile + ".tmp";

    // open the tmp file for persistence
    FileWriter fw;
    if ((err = fw.Open(path)) != SUCCESS) {
        return ERRORWRAP(err, "open file");
    }

    // do persistence to writer.
    if ((err = DoPersistence(&fw)) != SUCCESS) {
        ::unlink(path.c_str());
        return ERRORWRAP(err, "persistence");
    }

    // rename the config file.
    if (::rename(path.c_str(), m_configFile.c_str()) < 0) {
        ::unlink(path.c_str());
        return ERRORNEW(ERROR_SYSTEM_CONFIG_PERSISTENCE, "rename %s=>%s", path.c_str(), m_configFile.c_str());
    }

    return err;
}

error Config::DoPersistence(FileWriter *fw)
{
    error err = SUCCESS;

    // persistence root directive to writer.
    if ((err = m_root->Persistence(fw, 0)) != SUCCESS) {
        return ERRORWRAP(err, "root persistence");
    }

    return err;
}

error Config::RawToJson(JsonObject *obj)
{
    error err = SUCCESS;

    JsonObject* sobj = JsonAny::Object();
    obj->Set("http_api", sobj);

    sobj->Set("enabled", JsonAny::Boolean(GetHttpApiEnabled()));
    sobj->Set("listen", JsonAny::Str(GetHttpApiListen().c_str()));
    sobj->Set("crossdomain", JsonAny::Boolean(GetHttpApiCrossdomain()));

    JsonObject* ssobj = JsonAny::Object();
    sobj->Set("raw_api", ssobj);

    ssobj->Set("enabled", JsonAny::Boolean(GetRawApi()));
    ssobj->Set("allow_reload", JsonAny::Boolean(GetRawApiAllowReload()));
    ssobj->Set("allow_query", JsonAny::Boolean(GetRawApiAllowQuery()));
    ssobj->Set("allow_update", JsonAny::Boolean(GetRawApiAllowUpdate()));

    return err;
}

error Config::DoReloadListen()
{
    error err = SUCCESS;

    std::vector<IReloadHandler*>::iterator it;
    for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
        IReloadHandler* subscribe = *it;
        if ((err = subscribe->OnReloadListen()) != SUCCESS) {
            return ERRORWRAP(err, "notify subscribes reload listen failed");
        }
    }
    trace("reload listen success.");

    return err;
}

error Config::DoReloadMaxConnections()
{
    error err = SUCCESS;

    std::vector<IReloadHandler*>::iterator it;
    for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
        IReloadHandler* subscribe = *it;
        if ((err = subscribe->OnReloadMaxConns()) != SUCCESS) {
            return ERRORWRAP(err, "notify subscribes reload max_connections failed");
        }
    }
    trace("reload max_connections success.");

    return err;
}

error Config::DoReloadPithyPrintMs()
{
    error err = SUCCESS;

    std::vector<IReloadHandler*>::iterator it;
    for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
        IReloadHandler* subscribe = *it;
        if ((err = subscribe->OnReloadPithyPrint()) != SUCCESS) {
            return ERRORWRAP(err, "notify subscribes pithy_print_ms failed");
        }
    }
    trace("reload pithy_print_ms success.");

    return err;
}

error Config::DoReloadVhostAdded(std::string vhost)
{
    error err = SUCCESS;

    trace("vhost %s added, reload it.", vhost.c_str());

    std::vector<IReloadHandler*>::iterator it;
    for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
        IReloadHandler* subscribe = *it;
        if ((err = subscribe->OnReloadVhostAdded(vhost)) != SUCCESS) {
            return ERRORWRAP(err, "notify subscribes added vhost %s failed", vhost.c_str());
        }
    }

    trace("reload new vhost %s success.", vhost.c_str());

    return err;
}

error Config::DoReloadVhostRemoved(std::string vhost)
{
    error err = SUCCESS;

    trace("vhost %s removed, reload it.", vhost.c_str());

    std::vector<IReloadHandler*>::iterator it;
    for (it = m_subscribes.begin(); it != m_subscribes.end(); ++it) {
        IReloadHandler* subscribe = *it;
        if ((err = subscribe->OnReloadVhostRemoved(vhost)) != SUCCESS) {
            return ERRORWRAP(err, "notify subscribes removed vhost %s failed", vhost.c_str());
        }
    }
    trace("reload removed vhost %s success.", vhost.c_str());

    return err;
}

std::string Config::ConfigPath()
{
    return m_configFile;
}

error Config::ParseArgv(int &i, char **argv)
{
    error err = SUCCESS;

    char* p = argv[i];

    if (*p++ != '-') {
        m_showHelp = true;
        return err;
    }

    while (*p) {
        switch (*p++) {
            case '?':
            case 'h':
                m_showHelp = true;
                break;
            case 't':
                m_showHelp = false;
                m_testConf = true;
                break;
            case 'e':
                m_showHelp = false;
                m_envOnly = true;
                break;
            case 'v':
            case 'V':
                m_showHelp = false;
                m_showVersion = true;
                break;
            case 'g':
            case 'G':
                m_showHelp = false;
                m_showSignature = true;
                break;
            case 'c':
                m_showHelp = false;
                if (*p) {
                    m_configFile = p;
                    continue;
                }
                if (argv[++i]) {
                    m_configFile = argv[i];
                    continue;
                }
                return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "-c requires params");
            default:
                return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "invalid option: \"%c\", read help: %s -h",
                    *(p - 1), argv[0]);
        }
    }

    return err;
}

void Config::PrintHelp(char **argv)
{
    printf(
           "%s, %s, %s, created by %sand %s\n\n"
           "Usage: %s <-h?vVgGe>|<[-t] -c filename>\n"
           "Options:\n"
           "   -?, -h              : Show this help and exit 0.\n"
           "   -v, -V              : Show version and exit 0.\n"
           "   -g, -G              : Show server signature and exit 0.\n"
           "   -e                  : Use environment variable only, ignore config file.\n"
           "   -t                  : Test configuration file, exit with error code(0 for success).\n"
           "   -c filename         : Use config file to start server.\n"
           "For example:\n"
           "   %s -v\n"
           "   %s -t -c %s\n"
           "   %s -c %s\n",
           RTMP_SIG_SERVER, RTMP_SIG_URL, RTMP_SIG_LICENSE,
           RTMP_SIG_AUTHORS, CONSTRIBUTORS,
           argv[0], argv[0], argv[0], CONF_DEFAULT_COFNIG_FILE,
           argv[0], CONF_DEFAULT_COFNIG_FILE);
}

error Config::ParseFile(const char *filename)
{
    error err = SUCCESS;

    m_configFile = filename;

    if (m_configFile.empty()) {
        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "empty config");
    }

    ConfigBuffer* buffer = NULL;
    AutoFree(ConfigBuffer, buffer);
    if ((err = BuildBuffer(m_configFile, &buffer)) != SUCCESS) {
        return ERRORWRAP(err, "buffer fullfill %s", m_configFile.c_str());
    }

    if ((err = ParseBuffer(buffer)) != SUCCESS) {
        return ERRORWRAP(err, "parse buffer");
    }

    return err;
}

error Config::BuildBuffer(std::string src, internal::ConfigBuffer **pbuffer)
{
    error err = SUCCESS;

    ConfigBuffer* buffer = new ConfigBuffer();

    if ((err = buffer->Fullfill(src.c_str())) != SUCCESS) {
        Freep(buffer);
        return ERRORWRAP(err, "read from src %s", src.c_str());
    }

    *pbuffer = buffer;
    return err;
}

error Config::CheckConfig()
{
    error err = SUCCESS;

    if ((err = CheckNormalConfig()) != SUCCESS) {
        return ERRORWRAP(err, "check normal");
    }

    if ((err = CheckNumberConnections()) != SUCCESS) {
        return ERRORWRAP(err, "check connections");
    }

    // If use the full.conf, fail.
    if (IsFullConfig()) {
        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID,
            "never use full.conf(%s)", m_configFile.c_str());
    }

    return err;
}

error Config::CheckNormalConfig()
{
    error err = SUCCESS;

    trace("srs checking config...");

    ////////////////////////////////////////////////////////////////////////
    // check empty
    ////////////////////////////////////////////////////////////////////////
    if (!m_envOnly && m_root->m_directives.size() == 0) {
        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "conf is empty");
    }

    ////////////////////////////////////////////////////////////////////////
    // check root m_directives.
    ////////////////////////////////////////////////////////////////////////
    for (int i = 0; i < (int)m_root->m_directives.size(); i++) {
        ConfDirective* conf = m_root->At(i);
        std::string n = conf->m_name;
        if (n != "listen" && n != "pid" && n != "chunk_size" && n != "ff_log_dir"
            && n != "log_tank" && n != "log_level" && n != "log_level_v2" && n != "log_file"
            && n != "max_connections" && n != "daemon" && n != "heartbeat" && n != "tencentcloud_apm"
            && n != "http_api" && n != "stats" && n != "vhost" && n != "pithy_print_ms"
            && n != "http_server" && n != "stream_caster" && n != "rtc_server" && n != "srt_server"
            && n != "utc_time" && n != "work_dir" && n != "asprocess" && n != "server_id"
            && n != "ff_log_level" && n != "grace_final_wait" && n != "force_grace_quit"
            && n != "grace_start_wait" && n != "empty_ip_ok" && n != "disable_daemon_for_docker"
            && n != "inotify_auto_reload" && n != "auto_reload_for_docker" && n != "tcmalloc_release_rate"
            && n != "query_latest_version" && n != "first_wait_for_qlv" && n != "threads"
            && n != "circuit_breaker" && n != "is_full" && n != "in_docker" && n != "tencentcloud_cls"
            && n != "exporter" && n != "service_server"
            ) {
            return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal directive %s", n.c_str());
        }
    }
    if (true) {
        ConfDirective* conf = m_root->Get("http_api");
        for (int i = 0; conf && i < (int)conf->m_directives.size(); i++) {
            ConfDirective* obj = conf->At(i);
            std::string n = obj->m_name;
            if (n != "enabled" && n != "listen" && n != "crossdomain" && n != "raw_api" && n != "https") {
                return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal http_api.%s", n.c_str());
            }

            if (n == "raw_api") {
                for (int j = 0; j < (int)obj->m_directives.size(); j++) {
                    std::string m = obj->At(j)->m_name;
                    if (m != "enabled" && m != "allow_reload" && m != "allow_query" && m != "allow_update") {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal http_api.raw_api.%s", m.c_str());
                    }
                }
            }
        }
    }
    if (true) {
        ConfDirective* conf = m_root->Get("http_server");
        for (int i = 0; conf && i < (int)conf->m_directives.size(); i++) {
            std::string n = conf->At(i)->m_name;
            if (n != "enabled" && n != "listen" && n != "dir" && n != "crossdomain" && n != "https") {
                return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal http_stream.%s", n.c_str());
            }
        }
    }
    if (true) {
        ConfDirective* conf = m_root->Get("srt_server");
        for (int i = 0; conf && i < (int)conf->m_directives.size(); i++) {
            std::string n = conf->At(i)->m_name;
            if (n != "enabled" && n != "listen" && n != "maxbw"
                && n != "mss" && n != "latency" && n != "recvlatency"
                && n != "peerlatency" && n != "tlpkdrop" && n != "connect_timeout"
                && n != "sendbuf" && n != "recvbuf" && n != "payloadsize"
                && n != "default_app" && n != "sei_filter" && n != "mix_correct"
                && n != "tlpktdrop" && n != "tsbpdmode" && n != "passphrase" && n != "pbkeylen") {
                return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal srt_server.%s", n.c_str());
            }
        }
    }
    if (true) {
        ConfDirective* conf = GetHeartbeart();
        for (int i = 0; conf && i < (int)conf->m_directives.size(); i++) {
            std::string n = conf->At(i)->m_name;
            if (n != "enabled" && n != "interval" && n != "url"
                && n != "device_id" && n != "summaries") {
                return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal heartbeat.%s", n.c_str());
            }
        }
    }
    if (true) {
        ConfDirective* conf = GetStats();
        for (int i = 0; conf && i < (int)conf->m_directives.size(); i++) {
            std::string n = conf->At(i)->m_name;
            if (n != "enabled" && n != "network" && n != "disk") {
                return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal stats.%s", n.c_str());
            }
        }
    }
    if (true) {
        ConfDirective* conf = m_root->Get("rtc_server");
        for (int i = 0; conf && i < (int)conf->m_directives.size(); i++) {
            std::string n = conf->At(i)->m_name;
            if (n != "enabled" && n != "listen" && n != "dir" && n != "candidate" && n != "ecdsa" && n != "tcp"
                && n != "encrypt" && n != "reuseport" && n != "merge_nalus" && n != "black_hole" && n != "protocol"
                && n != "ip_family" && n != "api_as_candidates" && n != "resolve_api_domain"
                && n != "keep_api_domain" && n != "use_auto_detect_network_ip") {
                return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal rtc_server.%s", n.c_str());
            }
        }
    }
    if (true) {
        ConfDirective* conf = m_root->Get("exporter");
        for (int i = 0; conf && i < (int)conf->m_directives.size(); i++) {
            std::string n = conf->At(i)->m_name;
            if (n != "enabled" && n != "listen" && n != "label" && n != "tag") {
                return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal exporter.%s", n.c_str());
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////
    // check listen for rtmp.
    ////////////////////////////////////////////////////////////////////////
    if (true) {
        std::vector<std::string> listens = GetListens();
        if (!m_envOnly && listens.size() <= 0) {
            return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "listen requires params");
        }
        for (int i = 0; i < (int)listens.size(); i++) {
            int port; std::string ip;
            ParseEndpoint(listens[i], ip, port);

            // check ip
            if (!CheckIpAddrValid(ip)) {
                return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "listen.ip=%s is invalid", ip.c_str());
            }

            // check port
            if (port <= 0) {
                return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "listen.port=%d is invalid", port);
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////
    // check heartbeat
    ////////////////////////////////////////////////////////////////////////
    if (GetHeartbeatInterval() <= 0) {
        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "invalid heartbeat.interval=%" PRId64,
            GetHeartbeatInterval());
    }

    ////////////////////////////////////////////////////////////////////////
    // check stats
    ////////////////////////////////////////////////////////////////////////
    if (GetStatsNetwork() < 0) {
        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "invalid stats.network=%d", GetStatsNetwork());
    }
    if (true) {
        std::vector<IPAddress*> ips = GetLocalIps();
        int index = GetStatsNetwork();
        if (index >= (int)ips.size()) {
            return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "invalid stats.network=%d of %d",
                index, (int)ips.size());
        }

        IPAddress* addr = ips.at(index);
        warn("stats network use index=%d, ip=%s, ifname=%s", index, addr->m_ip.c_str(), addr->m_ifname.c_str());
    }
    if (true) {
        ConfDirective* conf = GetStatsDiskDevice();
        if (conf == NULL || (int)conf->m_args.size() <= 0) {
            warn("stats disk not configed, disk iops disabled.");
        } else {
            std::string disks;
            for (int i = 0; i < (int)conf->m_args.size(); i++) {
                disks += conf->m_args.at(i);
                disks += " ";
            }
            warn("stats disk list: %s", disks.c_str());
        }
    }

    ////////////////////////////////////////////////////////////////////////
    // Check HTTP API and server.
    ////////////////////////////////////////////////////////////////////////
    if (true) {
        std::string api = GetHttpApiListen();
        std::string server = GetHttpStreamListen();
        if (api.empty()) {
            return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "http_api.listen requires params");
        }
        if (server.empty()) {
            return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "http_server.listen requires params");
        }

        std::string apis = GetHttpsApiListen();
        std::string servers = GetHttpsStreamListen();
        if (api == server && apis != servers) {
            return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "for same http, https api(%s) != server(%s)", apis.c_str(), servers.c_str());
        }
        if (apis == servers && api != server) {
            return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "for same https, http api(%s) != server(%s)", api.c_str(), server.c_str());
        }

        if (GetHttpsApiEnabled() && !GetHttpApiEnabled()) {
            return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "https api depends on http");
        }
        if (GetHttpsStreamEnabled() && !GetHttpStreamEnabled()) {
            return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "https stream depends on http");
        }
    }

    ////////////////////////////////////////////////////////////////////////
    // check log name and level
    ////////////////////////////////////////////////////////////////////////
    if (true) {
        std::string log_filename = this->GetLogFile();
        if (GetLogTankFile() && log_filename.empty()) {
            return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "log file is empty");
        }
        if (GetLogTankFile()) {
            trace("you can check log by: tail -n 30 -f %s", log_filename.c_str());
            trace("please check SRS by: ./etc/init.d/srs status");
        } else {
            trace("write log to console");
        }
    }

    ////////////////////////////////////////////////////////////////////////
    // check features
    ////////////////////////////////////////////////////////////////////////
    std::vector<ConfDirective*> stream_casters = GetStreamCasters();
    for (int n = 0; n < (int)stream_casters.size(); n++) {
        ConfDirective* stream_caster = stream_casters[n];
        for (int i = 0; stream_caster && i < (int)stream_caster->m_directives.size(); i++) {
            ConfDirective* conf = stream_caster->At(i);
            std::string n = conf->m_name;
            if (n != "enabled" && n != "caster" && n != "output" && n != "listen" && n != "sip") {
                return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal stream_caster.%s", n.c_str());
            }

            if (n == "sip") {
                for (int j = 0; j < (int)conf->m_directives.size(); j++) {
                    std::string m = conf->At(j)->m_name;
                    if (m != "enabled"  && m != "listen" && m != "timeout" && m != "reinvite" && m != "candidate") {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal stream_caster.sip.%s", m.c_str());
                    }
                }
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////
    // check vhosts.
    ////////////////////////////////////////////////////////////////////////
    std::vector<ConfDirective*> vhosts;
    GetVhosts(vhosts);
    for (int n = 0; n < (int)vhosts.size(); n++) {
        ConfDirective* vhost = vhosts[n];

        for (int i = 0; vhost && i < (int)vhost->m_directives.size(); i++) {
            ConfDirective* conf = vhost->At(i);
            std::string n = conf->m_name;
            if (n != "enabled" && n != "chunk_size" && n != "min_latency" && n != "tcp_nodelay"
                && n != "dvr" && n != "ingest" && n != "hls" && n != "http_hooks"
                && n != "refer" && n != "forward" && n != "transcode" && n != "bandcheck"
                && n != "play" && n != "publish" && n != "cluster"
                && n != "security" && n != "http_remux" && n != "dash"
                && n != "http_static" && n != "hds" && n != "exec"
                && n != "in_ack_size" && n != "out_ack_size" && n != "rtc" && n != "srt") {
                return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.%s", n.c_str());
            }
            // for each sub m_directives of vhost.
            if (n == "dvr") {
                for (int j = 0; j < (int)conf->m_directives.size(); j++) {
                    std::string m = conf->At(j)->m_name;
                    if (m != "enabled"  && m != "dvr_apply" && m != "dvr_path" && m != "dvr_plan"
                        && m != "dvr_duration" && m != "dvr_wait_keyframe" && m != "time_jitter") {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.dvr.%s of %s", m.c_str(), vhost->Arg0().c_str());
                    }
                }
            } else if (n == "refer") {
                for (int j = 0; j < (int)conf->m_directives.size(); j++) {
                    std::string m = conf->At(j)->m_name;
                    if (m != "enabled" && m != "all" && m != "publish" && m != "play") {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.refer.%s of %s", m.c_str(), vhost->Arg0().c_str());
                    }
                }
            } else if (n == "exec") {
                for (int j = 0; j < (int)conf->m_directives.size(); j++) {
                    std::string m = conf->At(j)->m_name;
                    if (m != "enabled" && m != "publish") {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.exec.%s of %s", m.c_str(), vhost->Arg0().c_str());
                    }
                }
            } else if (n == "play") {
                for (int j = 0; j < (int)conf->m_directives.size(); j++) {
                    std::string m = conf->At(j)->m_name;
                    if (m != "time_jitter" && m != "mix_correct" && m != "atc" && m != "atc_auto" && m != "mw_latency"
                        && m != "gop_cache" && m != "queue_length" && m != "send_min_interval" && m != "reduce_sequence_header"
                        && m != "mw_msgs") {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.play.%s of %s", m.c_str(), vhost->Arg0().c_str());
                    }
                }
            } else if (n == "cluster") {
                for (int j = 0; j < (int)conf->m_directives.size(); j++) {
                    std::string m = conf->At(j)->m_name;
                    if (m != "mode" && m != "origin" && m != "token_traverse" && m != "vhost" && m != "debug_srs_upnode" && m != "coworkers"
                        && m != "origin_cluster" && m != "protocol" && m != "follow_client") {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.cluster.%s of %s", m.c_str(), vhost->Arg0().c_str());
                    }
                }
            } else if (n == "publish") {
                for (int j = 0; j < (int)conf->m_directives.size(); j++) {
                    std::string m = conf->At(j)->m_name;
                    if (m != "mr" && m != "mr_latency" && m != "firstpkt_timeout" && m != "normal_timeout"
                        && m != "parse_sps" && m != "try_annexb_first") {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.publish.%s of %s", m.c_str(), vhost->Arg0().c_str());
                    }
                }
            } else if (n == "ingest") {
                for (int j = 0; j < (int)conf->m_directives.size(); j++) {
                    std::string m = conf->At(j)->m_name;
                    if (m != "enabled" && m != "input" && m != "ffmpeg" && m != "engine") {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.ingest.%s of %s", m.c_str(), vhost->Arg0().c_str());
                    }
                }
            } else if (n == "http_static") {
                for (int j = 0; j < (int)conf->m_directives.size(); j++) {
                    std::string m = conf->At(j)->m_name;
                    if (m != "enabled" && m != "mount" && m != "dir") {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.http_static.%s of %s", m.c_str(), vhost->Arg0().c_str());
                    }
                }
            } else if (n == "http_remux") {
                for (int j = 0; j < (int)conf->m_directives.size(); j++) {
                    std::string m = conf->At(j)->m_name;
                    if (m != "enabled" && m != "mount" && m != "fast_cache") {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.http_remux.%s of %s", m.c_str(), vhost->Arg0().c_str());
                    }
                }
            } else if (n == "dash") {
                for (int j = 0; j < (int)conf->m_directives.size(); j++) {
                    std::string m = conf->At(j)->m_name;
                    if (m != "enabled" && m != "dash_fragment" && m != "dash_update_period" && m != "dash_timeshift" && m != "dash_path"
                        && m != "dash_mpd_file") {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.dash.%s of %s", m.c_str(), vhost->Arg0().c_str());
                    }
                }
            } else if (n == "hls") {
                for (int j = 0; j < (int)conf->m_directives.size(); j++) {
                    std::string m = conf->At(j)->m_name;
                    if (m != "enabled" && m != "hls_entry_prefix" && m != "hls_path" && m != "hls_fragment" && m != "hls_window" && m != "hls_on_error"
                        && m != "hls_storage" && m != "hls_mount" && m != "hls_td_ratio" && m != "hls_aof_ratio" && m != "hls_acodec" && m != "hls_vcodec"
                        && m != "hls_m3u8_file" && m != "hls_ts_file" && m != "hls_ts_floor" && m != "hls_cleanup" && m != "hls_nb_notify"
                        && m != "hls_wait_keyframe" && m != "hls_dispose" && m != "hls_keys" && m != "hls_fragments_per_key" && m != "hls_key_file"
                        && m != "hls_key_file_path" && m != "hls_key_url" && m != "hls_dts_directly" && m != "hls_ctx" && m != "hls_ts_ctx") {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.hls.%s of %s", m.c_str(), vhost->Arg0().c_str());
                    }

                    // TODO: FIXME: remove it in future.
                    if (m == "hls_storage" || m == "hls_mount") {
                        warn("HLS RAM is removed in SRS3+, read https://github.com/ossrs/srs/issues/513.");
                    }
                }
            } else if (n == "http_hooks") {
                for (int j = 0; j < (int)conf->m_directives.size(); j++) {
                    std::string m = conf->At(j)->m_name;
                    if (m != "enabled" && m != "on_connect" && m != "on_close" && m != "on_publish"
                        && m != "on_unpublish" && m != "on_play" && m != "on_stop"
                        && m != "on_dvr" && m != "on_hls" && m != "on_hls_notify") {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.http_hooks.%s of %s", m.c_str(), vhost->Arg0().c_str());
                    }
                }
            } else if (n == "forward") {
                for (int j = 0; j < (int)conf->m_directives.size(); j++) {
                    std::string m = conf->At(j)->m_name;
                    if (m != "enabled" && m != "destination" && m != "backend") {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.forward.%s of %s", m.c_str(), vhost->Arg0().c_str());
                    }
                }
            } else if (n == "security") {
                for (int j = 0; j < (int)conf->m_directives.size(); j++) {
                    ConfDirective* security = conf->At(j);
                    std::string m = security->m_name.c_str();
                    if (m != "enabled" && m != "deny" && m != "allow") {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.security.%s of %s", m.c_str(), vhost->Arg0().c_str());
                    }
                }
            } else if (n == "transcode") {
                for (int j = 0; j < (int)conf->m_directives.size(); j++) {
                    ConfDirective* trans = conf->At(j);
                    std::string m = trans->m_name.c_str();
                    if (m != "enabled" && m != "ffmpeg" && m != "engine") {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.transcode.%s of %s", m.c_str(), vhost->Arg0().c_str());
                    }
                    if (m == "engine") {
                        for (int k = 0; k < (int)trans->m_directives.size(); k++) {
                            std::string e = trans->At(k)->m_name;
                            if (e != "enabled" && e != "vfilter" && e != "vcodec"
                                && e != "vbitrate" && e != "vfps" && e != "vwidth" && e != "vheight"
                                && e != "vthreads" && e != "vprofile" && e != "vpreset" && e != "vparams"
                                && e != "acodec" && e != "abitrate" && e != "asample_rate" && e != "achannels"
                                && e != "aparams" && e != "output" && e != "perfile"
                                && e != "iformat" && e != "oformat") {
                                return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.transcode.engine.%s of %s", e.c_str(), vhost->Arg0().c_str());
                            }
                        }
                    }
                }
            } else if (n == "rtc") {
                for (int j = 0; j < (int)conf->m_directives.size(); j++) {
                    std::string m = conf->At(j)->m_name;
                    if (m != "enabled" && m != "nack" && m != "twcc" && m != "nack_no_copy"
                        && m != "bframe" && m != "aac" && m != "stun_timeout" && m != "stun_strict_check"
                        && m != "dtls_role" && m != "dtls_version" && m != "drop_for_pt" && m != "rtc_to_rtmp"
                        && m != "pli_for_rtmp" && m != "rtmp_to_rtc" && m != "keep_bframe") {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.rtc.%s of %s", m.c_str(), vhost->Arg0().c_str());
                    }
                }
            } else if (n == "srt") {
                for (int j = 0; j < (int)conf->m_directives.size(); j++) {
                    std::string m = conf->At(j)->m_name;
                    if (m != "enabled" && m != "srt_to_rtmp") {
                        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "illegal vhost.srt.%s of %s", m.c_str(), vhost->Arg0().c_str());
                    }
                }
            }
        }
    }
    // check ingest id unique.
    for (int i = 0; i < (int)vhosts.size(); i++) {
        ConfDirective* vhost = vhosts[i];
        std::vector<std::string> ids;

        for (int j = 0; j < (int)vhost->m_directives.size(); j++) {
            ConfDirective* conf = vhost->At(j);
            if (conf->m_name != "ingest") {
                continue;
            }

            std::string id = conf->Arg0();
            for (int k = 0; k < (int)ids.size(); k++) {
                if (id == ids.at(k)) {
                    return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "ingest id=%s exists for %s",
                        id.c_str(), vhost->Arg0().c_str());
                }
            }
            ids.push_back(id);
        }
    }

    ////////////////////////////////////////////////////////////////////////
    // check chunk size
    ////////////////////////////////////////////////////////////////////////
    if (GetGlobalChunkSize() < CONSTS_RTMP_MIN_CHUNK_SIZE
        || GetGlobalChunkSize() > CONSTS_RTMP_MAX_CHUNK_SIZE) {
        warn("chunk_size=%s should be in [%d, %d]", GetGlobalChunkSize(),
            CONSTS_RTMP_MIN_CHUNK_SIZE, CONSTS_RTMP_MAX_CHUNK_SIZE);
    }
    for (int i = 0; i < (int)vhosts.size(); i++) {
        ConfDirective* vhost = vhosts[i];
        if (GetChunkSize(vhost->Arg0()) < CONSTS_RTMP_MIN_CHUNK_SIZE
            || GetChunkSize(vhost->Arg0()) >CONSTS_RTMP_MAX_CHUNK_SIZE) {
            warn("chunk_size=%s of %s should be in [%d, %d]", GetGlobalChunkSize(), vhost->Arg0().c_str(),
                CONSTS_RTMP_MIN_CHUNK_SIZE, CONSTS_RTMP_MAX_CHUNK_SIZE);
        }
    }

    // asprocess conflict with daemon
    if (GetAsprocess() && GetDaemon()) {
        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "daemon conflicts with asprocess");
    }

    return err;
}

error Config::CheckNumberConnections()
{
    error err = SUCCESS;

    ////////////////////////////////////////////////////////////////////////
    // check max connections
    ////////////////////////////////////////////////////////////////////////
    int nb_connections = GetMaxConnections();
    if (nb_connections <= 0) {
        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "max_connections=%d is invalid", nb_connections);
    }

    // check max connections of system limits
    int nb_total = nb_connections + 128; // Simple reserved some fds.
    int max_open_files = (int)sysconf(_SC_OPEN_MAX);
    if (nb_total >= max_open_files) {
        ERROR("max_connections=%d, system limit to %d, please run: ulimit -HSn %d", nb_connections, max_open_files, MAX(10000, nb_connections * 10));
        return ERRORNEW(ERROR_SYSTEM_CONFIG_INVALID, "%d exceed max open files=%d", nb_total, max_open_files);
    }

    return err;
}

error Config::ParseBuffer(internal::ConfigBuffer *buffer)
{
    error err = SUCCESS;

    // We use a new root to parse buffer, to allow parse multiple times.
    Freep(m_root);
    m_root = new ConfDirective();

    // Parse root tree from buffer.
    if ((err = m_root->Parse(buffer, this)) != SUCCESS) {
        return ERRORWRAP(err, "root parse");
    }

    return err;
}

std::string Config::Cwd()
{
    return m_cwd;
}

std::string Config::Argv()
{
    return m_argv;
}

ConfDirective *Config::GetRoot()
{
    return m_root;
}

bool Config::GetDaemon()
{
    OVERWRITE_BY_ENV_BOOL2("srs.daemon");

    ConfDirective* conf = m_root->Get("daemon");
    if (!conf || conf->Arg0().empty()) {
        return true;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::GetInDocker()
{
    OVERWRITE_BY_ENV_BOOL("srs.in_docker");

    static bool DEFAULT = false;

    ConfDirective* conf = m_root->Get("in_docker");
    if (!conf) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

bool Config::IsFullConfig()
{
    static bool DEFAULT = false;

    ConfDirective* conf = m_root->Get("is_full");
    if (!conf) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

std::string ServerIdPath(std::string pid_file)
{
    std::string path = StringReplace(pid_file, ".pid", ".id");
    if (!StringEndsWith(path, ".id")) {
        path += ".id";
    }
    return path;
}

std::string TryReadFile(std::string path) {
    error err = SUCCESS;

    FileReader r;
    if ((err = r.Open(path)) != SUCCESS) {
        Freep(err);
        return "";
    }

    static char buf[1024];
    ssize_t nn = 0;
    if ((err = r.Read(buf, sizeof(buf), &nn)) != SUCCESS) {
        Freep(err);
        return "";
    }

    if (nn > 0) {
        return std::string(buf, nn);
    }
    return "";
}

void TryWriteFile(std::string path, std::string content) {
    error err = SUCCESS;

    FileWriter w;
    if ((err = w.Open(path)) != SUCCESS) {
        Freep(err);
        return;
    }

    if ((err = w.Write((void*)content.data(), content.length(), NULL)) != SUCCESS) {
        Freep(err);
        return;
    }
}

std::string Config::GetServerId()
{
    static std::string DEFAULT = "";

    // Try to read DEFAULT from server id file.
    if (DEFAULT.empty()) {
        DEFAULT = TryReadFile(ServerIdPath(GetPidFile()));
    }

    // Generate a random one if empty.
    if (DEFAULT.empty()) {
        DEFAULT = "vid-" + RandomStr(7);;
    }

    // Get the server id from env, config or DEFAULT.
    std::string server_id;

    if (!Getenv("srs.server_id").empty()) {
        server_id = Getenv("srs.server_id");
    } else {
        ConfDirective* conf = m_root->Get("server_id");
        if (conf) {
            server_id = conf->Arg0();
        }
    }

    if (server_id.empty()) {
        server_id = DEFAULT;
    }

    // Write server id to tmp file.
    TryWriteFile(ServerIdPath(GetPidFile()), server_id);

    return server_id;
}

int Config::GetMaxConnections()
{
    OVERWRITE_BY_ENV_INT("srs.max_connections");

    static int DEFAULT = 1000;

    ConfDirective* conf = m_root->Get("max_connections");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

std::vector<std::string> Config::GetListens()
{
    std::vector<std::string> ports;

    if (!Getenv("srs.listen").empty()) {
        ports.push_back(Getenv("srs.listen"));
        return ports;
    }

    ConfDirective* conf = m_root->Get("listen");
    if (!conf) {
        return ports;
    }

    for (int i = 0; i < (int)conf->m_args.size(); i++) {
        ports.push_back(conf->m_args.at(i));
    }

    return ports;
}

std::string Config::GetPidFile()
{
    OVERWRITE_BY_ENV_STRING("srs.pid");

    static std::string DEFAULT = "./objs/srs.pid";

    ConfDirective* conf = m_root->Get("pid");

    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

utime_t Config::GetPithyPrint()
{
    OVERWRITE_BY_ENV_MILLISECONDS("srs.pithy_print_ms");

    static utime_t DEFAULT = 10 * UTIME_SECONDS;

    ConfDirective* conf = m_root->Get("pithy_print_ms");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return (utime_t)(::atoi(conf->Arg0().c_str()) * UTIME_MILLISECONDS);
}

bool Config::GetUtcTime()
{
    OVERWRITE_BY_ENV_BOOL("srs.utc_time");

    static bool DEFAULT = false;

    ConfDirective* conf = m_root->Get("utc_time");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

std::string Config::GetWorkDir()
{
    OVERWRITE_BY_ENV_STRING("srs.work_dir");

    static std::string DEFAULT = "./";

    ConfDirective* conf = m_root->Get("work_dir");
    if( !conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

bool Config::GetAsprocess()
{
    OVERWRITE_BY_ENV_BOOL("srs.asprocess");

    static bool DEFAULT = false;

    ConfDirective* conf = m_root->Get("asprocess");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

bool Config::WhetherQueryLatestVersion()
{
    OVERWRITE_BY_ENV_BOOL2("srs.query_latest_version");

    static bool DEFAULT = true;

    ConfDirective* conf = m_root->Get("query_latest_version");
    if (!conf) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

utime_t Config::FirstWaitForQlv()
{
    OVERWRITE_BY_ENV_SECONDS("srs.first_wait_for_qlv");

    static utime_t DEFAULT = 5 * 60 * UTIME_SECONDS;

    ConfDirective* conf = m_root->Get("first_wait_for_qlv");
    if (!conf) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str()) * UTIME_SECONDS;
}

bool Config::EmptyIpOk()
{
    OVERWRITE_BY_ENV_BOOL2("srs.empty_ip_ok");

    static bool DEFAULT = true;

    ConfDirective* conf = m_root->Get("empty_ip_ok");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

utime_t Config::GetGraceStartWait()
{
    OVERWRITE_BY_ENV_MILLISECONDS("srs.grace_start_wait");

    static utime_t DEFAULT = 2300 * UTIME_MILLISECONDS;

    ConfDirective* conf = m_root->Get("grace_start_wait");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return (utime_t)(::atol(conf->Arg0().c_str()) * UTIME_MILLISECONDS);
}

utime_t Config::GetGraceFinalWait()
{
    OVERWRITE_BY_ENV_MILLISECONDS("srs.grace_final_wait");

    static utime_t DEFAULT = 3200 * UTIME_MILLISECONDS;

    ConfDirective* conf = m_root->Get("grace_final_wait");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return (utime_t)(::atol(conf->Arg0().c_str()) * UTIME_MILLISECONDS);
}

bool Config::IsForceGraceQuit()
{
    OVERWRITE_BY_ENV_BOOL("srs.force_grace_quit");

    static bool DEFAULT = false;

    ConfDirective* conf = m_root->Get("force_grace_quit");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

bool Config::DisableDaemonForDocker()
{
    OVERWRITE_BY_ENV_BOOL2("srs.disable_daemon_for_docker");

    static bool DEFAULT = true;

    ConfDirective* conf = m_root->Get("disable_daemon_for_docker");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::InotifyAutoReload()
{
    OVERWRITE_BY_ENV_BOOL("srs.inotify_auto_reload");

    static bool DEFAULT = false;

    ConfDirective* conf = m_root->Get("inotify_auto_reload");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

bool Config::AutoReloadForDocker()
{
    OVERWRITE_BY_ENV_BOOL2("srs.auto_reload_for_docker");

    static bool DEFAULT = true;

    ConfDirective* conf = m_root->Get("auto_reload_for_docker");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

double Config::TcmallocReleaseRate()
{
    if (!Getenv("srs.tcmalloc_release_rate").empty()) {
        double trr = ::atof(Getenv("srs.tcmalloc_release_rate").c_str());
        trr = MIN(10, trr);
        trr = MAX(0, trr);
        return trr;
    }

    static double DEFAULT = PERF_TCMALLOC_RELEASE_RATE;

    ConfDirective* conf = m_root->Get("tcmalloc_release_rate");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    double trr = ::atof(conf->Arg0().c_str());
    trr = MIN(10, trr);
    trr = MAX(0, trr);
    return trr;
}

utime_t Config::GetThreadsInterval()
{
    OVERWRITE_BY_ENV_SECONDS("srs.threads.interval");

    static utime_t DEFAULT = 5 * UTIME_SECONDS;

    ConfDirective* conf = m_root->Get("threads");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("interval");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    int v = ::atoi(conf->Arg0().c_str());
    if (v <= 0) {
        return DEFAULT;
    }

    return v * UTIME_SECONDS;
}

bool Config::GetCircuitBreaker()
{
    OVERWRITE_BY_ENV_BOOL2("srs.circuit_breaker.enabled");

    static bool DEFAULT = true;

    ConfDirective* conf = m_root->Get("circuit_breaker");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

int Config::GetHighThreshold()
{
    OVERWRITE_BY_ENV_INT("srs.circuit_breaker.high_threshold");

    static int DEFAULT = 90;

    ConfDirective* conf = m_root->Get("circuit_breaker");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("high_threshold");
    if (!conf) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

int Config::GetHighPulse()
{
    OVERWRITE_BY_ENV_INT("srs.circuit_breaker.high_pulse");

    static int DEFAULT = 2;

    ConfDirective* conf = m_root->Get("circuit_breaker");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("high_pulse");
    if (!conf) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

int Config::GetCriticalThreshold()
{
    OVERWRITE_BY_ENV_INT("srs.circuit_breaker.critical_threshold");

    static int DEFAULT = 95;

    ConfDirective* conf = m_root->Get("circuit_breaker");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("critical_threshold");
    if (!conf) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

int Config::GetCriticalPulse()
{
    OVERWRITE_BY_ENV_INT("srs.circuit_breaker.critical_pulse");

    static int DEFAULT = 1;

    ConfDirective* conf = m_root->Get("circuit_breaker");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("critical_pulse");
    if (!conf) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

int Config::GetDyingThreshold()
{
    OVERWRITE_BY_ENV_INT("srs.circuit_breaker.dying_threshold");

    static int DEFAULT = 99;

    ConfDirective* conf = m_root->Get("circuit_breaker");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("dying_threshold");
    if (!conf) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

int Config::GetDyingPulse()
{
    OVERWRITE_BY_ENV_INT("srs.circuit_breaker.dying_threshold");

    static int DEFAULT = 5;

    ConfDirective* conf = m_root->Get("circuit_breaker");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("dying_pulse");
    if (!conf) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

bool Config::GetTencentcloudClsEnabled()
{
    OVERWRITE_BY_ENV_BOOL("srs.tencentcloud_cls.enabled");

    static bool DEFAULT = false;

    ConfDirective* conf = m_root->Get("tencentcloud_cls");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

bool Config::GetTencentcloudClsStatHeartbeat()
{
    OVERWRITE_BY_ENV_BOOL2("srs.tencentcloud_cls.stat_heartbeat");

    static bool DEFAULT = true;

    ConfDirective* conf = m_root->Get("tencentcloud_cls");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("stat_heartbeat");
    if (!conf) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::GetTencentcloudClsStatStreams()
{
    OVERWRITE_BY_ENV_BOOL2("srs.tencentcloud_cls.stat_streams");

    static bool DEFAULT = true;

    ConfDirective* conf = m_root->Get("tencentcloud_cls");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("stat_streams");
    if (!conf) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::GetTencentcloudClsDebugLogging()
{
    OVERWRITE_BY_ENV_BOOL("srs.tencentcloud_cls.debug_logging");

    static bool DEFAULT = false;

    ConfDirective* conf = m_root->Get("tencentcloud_cls");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("debug_logging");
    if (!conf) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

int Config::GetTencentcloudClsHeartbeatRatio()
{
    OVERWRITE_BY_ENV_INT("srs.tencentcloud_cls.heartbeat_ratio");

    static int DEFAULT = 1;

    ConfDirective* conf = m_root->Get("tencentcloud_cls");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("heartbeat_ratio");
    if (!conf) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

int Config::GetTencentcloudClsStreamsRatio()
{
    OVERWRITE_BY_ENV_INT("srs.tencentcloud_cls.streams_ratio");

    static int DEFAULT = 1;

    ConfDirective* conf = m_root->Get("tencentcloud_cls");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("streams_ratio");
    if (!conf) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

std::string Config::GetTencentcloudClsLabel()
{
    OVERWRITE_BY_ENV_STRING("srs.tencentcloud_cls.label");

    static std::string DEFAULT = "";

    ConfDirective* conf = m_root->Get("tencentcloud_cls");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("label");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetTencentcloudClsTag()
{
    OVERWRITE_BY_ENV_STRING("srs.tencentcloud_cls.tag");

    static std::string DEFAULT = "";

    ConfDirective* conf = m_root->Get("tencentcloud_cls");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("tag");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetTencentcloudClsSecretId()
{
    OVERWRITE_BY_ENV_STRING("srs.tencentcloud_cls.secret_id");

    static std::string DEFAULT = "";

    ConfDirective* conf = m_root->Get("tencentcloud_cls");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("secret_id");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetTencentcloudClsSecretKey()
{
    OVERWRITE_BY_ENV_STRING("srs.tencentcloud_cls.secret_key");

    static std::string DEFAULT = "";

    ConfDirective* conf = m_root->Get("tencentcloud_cls");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("secret_key");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetTencentcloudClsEndpoint()
{
    OVERWRITE_BY_ENV_STRING("srs.tencentcloud_cls.endpoint");

    static std::string DEFAULT = "";

    ConfDirective* conf = m_root->Get("tencentcloud_cls");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("endpoint");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetTencentcloudClsTopicId()
{
    OVERWRITE_BY_ENV_STRING("srs.tencentcloud_cls.topic_id");

    static std::string DEFAULT = "";

    ConfDirective* conf = m_root->Get("tencentcloud_cls");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("topic_id");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

bool Config::GetTencentcloudApmEnabled()
{
    OVERWRITE_BY_ENV_BOOL("srs.tencentcloud_apm.enabled");

    static bool DEFAULT = false;

    ConfDirective* conf = m_root->Get("tencentcloud_apm");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

std::string Config::GetTencentcloudApmTeam()
{
    OVERWRITE_BY_ENV_STRING("srs.tencentcloud_apm.team");

    static std::string DEFAULT = "";

    ConfDirective* conf = m_root->Get("tencentcloud_apm");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("team");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetTencentcloudApmToken()
{
    OVERWRITE_BY_ENV_STRING("srs.tencentcloud_apm.token");

    static std::string DEFAULT = "";

    ConfDirective* conf = m_root->Get("tencentcloud_apm");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("token");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetTencentcloudApmEndpoint()
{
    OVERWRITE_BY_ENV_STRING("srs.tencentcloud_apm.endpoint");

    static std::string DEFAULT = "";

    ConfDirective* conf = m_root->Get("tencentcloud_apm");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("endpoint");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetTencentcloudApmServiceName()
{
    OVERWRITE_BY_ENV_STRING("srs.tencentcloud_apm.service_name");

    static std::string DEFAULT = "srs-server";

    ConfDirective* conf = m_root->Get("tencentcloud_apm");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("service_name");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

bool Config::GetTencentcloudApmDebugLogging()
{
    OVERWRITE_BY_ENV_BOOL("srs.tencentcloud_apm.debug_logging");

    static bool DEFAULT = false;

    ConfDirective* conf = m_root->Get("tencentcloud_apm");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("debug_logging");
    if (!conf) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

std::vector<ConfDirective *> Config::GetStreamCasters()
{
    Assert(m_root);

    std::vector<ConfDirective*> stream_casters;

    for (int i = 0; i < (int)m_root->m_directives.size(); i++) {
        ConfDirective* conf = m_root->At(i);

        if (!conf->IsStreamCaster()) {
            continue;
        }

        stream_casters.push_back(conf);
    }

    return stream_casters;
}

bool Config::GetStreamCasterEnabled(ConfDirective *conf)
{
    static bool DEFAULT = false;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

std::string Config::GetStreamCasterEngine(ConfDirective *conf)
{
    static std::string DEFAULT = "";

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("caster");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetStreamCasterOutput(ConfDirective *conf)
{
    static std::string DEFAULT = "";

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("output");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

int Config::GetStreamCasterListen(ConfDirective *conf)
{
    static int DEFAULT = 0;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("listen");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

bool Config::GetStreamCasterSipEnable(ConfDirective *conf)
{
    static bool DEFAULT = true;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("sip");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

int Config::GetStreamCasterSipListen(ConfDirective *conf)
{
    static int DEFAULT = 5060;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("sip");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("listen");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

utime_t Config::GetStreamCasterSipTimeout(ConfDirective *conf)
{
    static utime_t DEFAULT = 60 * UTIME_SECONDS;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("sip");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("timeout");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atof(conf->Arg0().c_str()) * UTIME_SECONDS;
}

utime_t Config::GetStreamCasterSipReinvite(ConfDirective *conf)
{
    static utime_t DEFAULT = 5 * UTIME_SECONDS;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("sip");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("reinvite");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atof(conf->Arg0().c_str()) * UTIME_SECONDS;
}

std::string Config::GetStreamCasterSipCandidate(ConfDirective *conf)
{
    static std::string DEFAULT = "*";

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("sip");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("candidate");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    std::string eip = Getenv(conf->Arg0());
    if (!eip.empty()) {
        return eip;
    }

    // If configed as ENV, but no ENV set, use default value.
    if (StringStartsWith(conf->Arg0(), "$")) {
        return DEFAULT;
    }

    return conf->Arg0();
}

bool Config::GetRtcServerEnabled()
{
    ConfDirective* conf = m_root->Get("rtc_server");
    return GetRtcServerEnabled(conf);
}

bool Config::GetRtcServerEnabled(ConfDirective *conf)
{
    OVERWRITE_BY_ENV_BOOL("srs.rtc_server.enabled");

    static bool DEFAULT = false;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

int Config::GetRtcServerListen()
{
    OVERWRITE_BY_ENV_INT("srs.rtc_server.listen");

    static int DEFAULT = 8000;

    ConfDirective* conf = m_root->Get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("listen");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

std::string Config::GetRtcServerCandidates()
{
    OVERWRITE_BY_ENV_STRING("srs.rtc_server.candidate");

    static std::string DEFAULT = "*";

    ConfDirective* conf = m_root->Get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("candidate");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    std::string eip = Getenv(conf->Arg0());
    if (!eip.empty()) {
        return eip;
    }

    // If configed as ENV, but no ENV set, use default value.
    if (StringStartsWith(conf->Arg0(), "$")) {
        return DEFAULT;
    }

    return conf->Arg0();
}

bool Config::GetApiAsCandidates()
{
    OVERWRITE_BY_ENV_BOOL2("srs.rtc_server.api_as_candidates");

    static bool DEFAULT = true;

    ConfDirective* conf = m_root->Get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("api_as_candidates");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::GetResolveApiDomain()
{
    OVERWRITE_BY_ENV_BOOL2("srs.rtc_server.resolve_api_domain");

    static bool DEFAULT = true;

    ConfDirective* conf = m_root->Get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("resolve_api_domain");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::GetKeepApiDomain()
{
    OVERWRITE_BY_ENV_BOOL("srs.rtc_server.keep_api_domain");

    static bool DEFAULT = false;

    ConfDirective* conf = m_root->Get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("keep_api_domain");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

bool Config::GetUseAutoDetectNetworkIp()
{
    OVERWRITE_BY_ENV_BOOL2("srs.rtc_server.use_auto_detect_network_ip");

    static bool DEFAULT = true;

    ConfDirective* conf = m_root->Get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("use_auto_detect_network_ip");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::GetRtcServerTcpEnabled()
{
    OVERWRITE_BY_ENV_BOOL("srs.rtc_server.tcp.enabled");

    static bool DEFAULT = false;

    ConfDirective* conf = m_root->Get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("tcp");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

int Config::GetRtcServerTcpListen()
{
    OVERWRITE_BY_ENV_INT("srs.rtc_server.tcp.listen");

    static int DEFAULT = 8000;

    ConfDirective* conf = m_root->Get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("tcp");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("listen");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

std::string Config::GetRtcServerProtocol()
{
    OVERWRITE_BY_ENV_STRING("srs.rtc_server.protocol");

    static std::string DEFAULT = "udp";

    ConfDirective* conf = m_root->Get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("protocol");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetRtcServerIpFamily()
{
    OVERWRITE_BY_ENV_STRING("srs.rtc_server.ip_family");

    static std::string DEFAULT = "ipv4";

    ConfDirective* conf = m_root->Get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("ip_family");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

bool Config::GetRtcServerEcdsa()
{
    OVERWRITE_BY_ENV_BOOL2("srs.rtc_server.ecdsa");

    static bool DEFAULT = true;

    ConfDirective* conf = m_root->Get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("ecdsa");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::GetRtcServerEncrypt()
{
    OVERWRITE_BY_ENV_BOOL2("srs.rtc_server.encrypt");

    static bool DEFAULT = true;

    ConfDirective* conf = m_root->Get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("encrypt");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

int Config::GetRtcServerReuseport()
{
    int v = GetRtcServerReuseport2();

#if !defined(SO_REUSEPORT)
    if (v > 1) {
        srs_warn("REUSEPORT not supported, reset to 1");
        v = 1;
    }
#endif

    return v;
}

bool Config::GetRtcServerMergeNalus()
{
    OVERWRITE_BY_ENV_BOOL("srs.rtc_server.merge_nalus");

    static int DEFAULT = false;

    ConfDirective* conf = m_root->Get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("merge_nalus");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

bool Config::GetRtcServerBlackHole()
{
    OVERWRITE_BY_ENV_BOOL("srs.rtc_server.black_hole.enabled");

    static bool DEFAULT = false;

    ConfDirective* conf = m_root->Get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("black_hole");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

std::string Config::GetRtcServerBlackHoleAddr()
{
    OVERWRITE_BY_ENV_STRING("srs.rtc_server.black_hole.addr");

    static std::string DEFAULT = "";

    ConfDirective* conf = m_root->Get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("black_hole");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("addr");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

int Config::GetRtcServerReuseport2()
{
    OVERWRITE_BY_ENV_INT("srs.rtc_server.reuseport");

    static int DEFAULT = 1;

    ConfDirective* conf = m_root->Get("rtc_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("reuseport");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

ConfDirective *Config::GetRtc(std::string vhost)
{
    ConfDirective* conf = GetVhost(vhost);
    return conf? conf->Get("rtc") : NULL;
}

bool Config::GetRtcEnabled(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL("srs.vhost.rtc.enabled");

    static bool DEFAULT = false;

    ConfDirective* conf = GetRtc(vhost);

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

bool Config::GetRtcKeepBframe(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL("srs.vhost.rtc.keep_bframe");

    static bool DEFAULT = false;

    ConfDirective* conf = GetRtc(vhost);

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("keep_bframe");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

bool Config::GetRtcFromRtmp(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL("srs.vhost.rtc.rtmp_to_rtc");

    static bool DEFAULT = false;

    ConfDirective* conf = GetRtc(vhost);

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("rtmp_to_rtc");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

utime_t Config::GetRtcStunTimeout(std::string vhost)
{
    OVERWRITE_BY_ENV_SECONDS("srs.vhost.rtc.stun_timeout");

    static utime_t DEFAULT = 30 * UTIME_SECONDS;

    ConfDirective* conf = GetRtc(vhost);

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("stun_timeout");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return (utime_t)(::atoi(conf->Arg0().c_str()) * UTIME_SECONDS);
}

bool Config::GetRtcStunStrictCheck(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL("srs.vhost.rtc.stun_strict_check");

    static bool DEFAULT = false;

    ConfDirective* conf = GetRtc(vhost);

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("stun_strict_check");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

std::string Config::GetRtcDtlsRole(std::string vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.vhost.rtc.dtls_role");

    static std::string DEFAULT = "passive";

    ConfDirective* conf = GetRtc(vhost);

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("dtls_role");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetRtcDtlsVersion(std::string vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.vhost.rtc.dtls_version");

    static std::string DEFAULT = "auto";

    ConfDirective* conf = GetRtc(vhost);

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("dtls_version");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

int Config::GetRtcDropForPt(std::string vhost)
{
    OVERWRITE_BY_ENV_INT("srs.vhost.rtc.drop_for_pt");

    static int DEFAULT = 0;

    ConfDirective* conf = GetRtc(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("drop_for_pt");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

bool Config::GetRtcToRtmp(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL("srs.vhost.rtc.rtc_to_rtmp");

    static bool DEFAULT = false;

    ConfDirective* conf = GetRtc(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("rtc_to_rtmp");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

utime_t Config::GetRtcPliForRtmp(std::string vhost)
{
    static utime_t DEFAULT = 6 * UTIME_SECONDS;
    utime_t v = 0;

    if (!Getenv("srs.vhost.rtc.pli_for_rtmp").empty()) {
        v = (utime_t)(::atof(Getenv("srs.vhost.rtc.pli_for_rtmp").c_str()) * UTIME_SECONDS);
    } else {
        ConfDirective* conf = GetRtc(vhost);
        if (!conf) {
            return DEFAULT;
        }

        conf = conf->Get("pli_for_rtmp");
        if (!conf || conf->Arg0().empty()) {
            return DEFAULT;
        }

        v = (utime_t)(::atof(conf->Arg0().c_str()) * UTIME_SECONDS);
    }

    if (v < 500 * UTIME_MILLISECONDS || v > 30 * UTIME_SECONDS) {
        warn("Reset pli %dms to %dms", u2msi(v), u2msi(DEFAULT));
        return DEFAULT;
    }

    return v;
}

bool Config::GetRtcNackEnabled(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL2("srs.vhost.rtc.nack");

    static bool DEFAULT = true;

    ConfDirective* conf = GetRtc(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("nack");
    if (!conf) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::GetRtcNackNoCopy(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL2("srs.vhost.rtc.nack_no_copy");

    static bool DEFAULT = true;

    ConfDirective* conf = GetRtc(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("nack_no_copy");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::GetRtcTwccEnabled(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL2("srs.vhost.rtc.twcc");

    static bool DEFAULT = true;

    ConfDirective* conf = GetRtc(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("twcc");
    if (!conf) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

ConfDirective *Config::GetVhost(std::string vhost, bool try_default_vhost)
{
    Assert(m_root);

    for (int i = 0; i < (int)m_root->m_directives.size(); i++) {
        ConfDirective* conf = m_root->At(i);

        if (!conf->IsVhost()) {
            continue;
        }

        if (conf->Arg0() == vhost) {
            return conf;
        }
    }

    if (try_default_vhost && vhost != CONSTS_RTMP_DEFAULT_VHOST) {
        return GetVhost(CONSTS_RTMP_DEFAULT_VHOST);
    }

    return NULL;
}

void Config::GetVhosts(std::vector<ConfDirective *> &vhosts)
{
    Assert(m_root);

    for (int i = 0; i < (int)m_root->m_directives.size(); i++) {
        ConfDirective* conf = m_root->At(i);

        if (!conf->IsVhost()) {
            continue;
        }

        vhosts.push_back(conf);
    }
}

bool Config::GetVhostEnabled(std::string vhost)
{
    ConfDirective* conf = GetVhost(vhost);

    return GetVhostEnabled(conf);
}

bool Config::GetVhostEnabled(ConfDirective *conf)
{
    static bool DEFAULT = true;

    // false for NULL vhost.
    if (!conf) {
        return false;
    }

    // perfer true for exists one.
    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::GetGopCache(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL2("srs.vhost.play.gop_cache");

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return PERF_GOP_CACHE;
    }

    conf = conf->Get("play");
    if (!conf) {
        return PERF_GOP_CACHE;
    }

    conf = conf->Get("gop_cache");
    if (!conf || conf->Arg0().empty()) {
        return PERF_GOP_CACHE;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::GetDebugSrsUpnode(std::string vhost)
{
    static bool DEFAULT = true;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("cluster");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("debug_srs_upnode");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::GetAtc(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL("srs.vhost.play.atc");

    static bool DEFAULT = false;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("play");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("atc");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

bool Config::GetAtcAuto(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL("srs.vhost.play.atc_auto");

    static bool DEFAULT = false;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("play");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("atc_auto");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

int Config::GetTimeJitter(std::string vhost)
{
    if (!Getenv("srs.vhost.play.mw_latency").empty()) {
        return TimeJitterString2int(Getenv("srs.vhost.play.mw_latency"));
    }

    static std::string DEFAULT = "full";

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return TimeJitterString2int(DEFAULT);
    }

    conf = conf->Get("play");
    if (!conf) {
        return TimeJitterString2int(DEFAULT);
    }

    conf = conf->Get("time_jitter");
    if (!conf || conf->Arg0().empty()) {
        return TimeJitterString2int(DEFAULT);
    }

    return TimeJitterString2int(conf->Arg0());
}

bool Config::GetMixCorrect(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL("srs.vhost.play.mix_correct");

    static bool DEFAULT = false;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("play");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("mix_correct");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

utime_t Config::GetQueueLength(std::string vhost)
{
    OVERWRITE_BY_ENV_SECONDS("srs.vhost.play.queue_length");

    static utime_t DEFAULT = PERF_PLAY_QUEUE;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("play");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("queue_length");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return utime_t(::atoi(conf->Arg0().c_str()) * UTIME_SECONDS);
}

bool Config::GetReferEnabled(std::string vhost)
{
    static bool DEFAULT = false;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("refer");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

ConfDirective *Config::GetReferAll(std::string vhost)
{
    static ConfDirective* DEFAULT = NULL;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("refer");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Get("all");
}

ConfDirective *Config::GetReferPlay(std::string vhost)
{
    static ConfDirective* DEFAULT = NULL;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("refer");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Get("play");
}

ConfDirective *Config::GetReferPublish(std::string vhost)
{
    static ConfDirective* DEFAULT = NULL;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("refer");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Get("publish");
}

int Config::GetInAckSize(std::string vhost)
{
    static int DEFAULT = 0;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("in_ack_size");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

int Config::GetOutAckSize(std::string vhost)
{
    static int DEFAULT = 2500000;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("out_ack_size");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

int Config::GetChunkSize(std::string vhost)
{
    if (vhost.empty()) {
        return GetGlobalChunkSize();
    }

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        // vhost does not specify the chunk size,
        // use the global instead.
        return GetGlobalChunkSize();
    }

    conf = conf->Get("chunk_size");
    if (!conf || conf->Arg0().empty()) {
        // vhost does not specify the chunk size,
        // use the global instead.
        return GetGlobalChunkSize();
    }

    return ::atoi(conf->Arg0().c_str());
}

bool Config::GetParseSps(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL2("srs.vhost.publish.parse_sps");

    static bool DEFAULT = true;

    ConfDirective* conf = GetVhost(vhost);

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("publish");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("parse_sps");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::TryAnnexbFirst(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL2("srs.vhost.publish.try_annexb_first");

    static bool DEFAULT = true;

    ConfDirective* conf = GetVhost(vhost);

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("publish");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("try_annexb_first");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::GetMrEnabled(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL("srs.vhost.publish.mr");

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return PERF_MR_ENABLED;
    }

    conf = conf->Get("publish");
    if (!conf) {
        return PERF_MR_ENABLED;
    }

    conf = conf->Get("mr");
    if (!conf || conf->Arg0().empty()) {
        return PERF_MR_ENABLED;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

utime_t Config::GetMrSleep(std::string vhost)
{
    OVERWRITE_BY_ENV_MILLISECONDS("srs.vhost.publish.mr_latency");

    static utime_t DEFAULT = PERF_MR_SLEEP;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("publish");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("mr_latency");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return (utime_t)(::atoi(conf->Arg0().c_str()) * UTIME_MILLISECONDS);
}

utime_t Config::GetMwSleep(std::string vhost, bool is_rtc)
{
    if (!Getenv("srs.vhost.play.mw_latency").empty()) {
        int v = ::atoi(Getenv("srs.vhost.play.mw_latency").c_str());
        if (is_rtc && v > 0) {
            warn("For RTC, we ignore mw_latency");
            return 0;
        }

        return (utime_t)(v * UTIME_MILLISECONDS);
    }

    static utime_t SYS_DEFAULT = PERF_MW_SLEEP;
    static utime_t RTC_DEFAULT = 0;

    utime_t DEFAULT = is_rtc? RTC_DEFAULT : SYS_DEFAULT;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("play");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("mw_latency");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    int v = ::atoi(conf->Arg0().c_str());
    if (is_rtc && v > 0) {
        warn("For RTC, we ignore mw_latency");
        return 0;
    }

    return (utime_t)(v * UTIME_MILLISECONDS);
}

int Config::GetMwMsgs(std::string vhost, bool is_realtime, bool is_rtc)
{
    if (!Getenv("srs.vhost.play.mw_msgs").empty()) {
        int v = ::atoi(Getenv("srs.vhost.play.mw_msgs").c_str());
        if (v > PERF_MW_MSGS) {
            warn("reset mw_msgs %d to max %d", v, PERF_MW_MSGS);
            v = PERF_MW_MSGS;
        }

        return v;
    }

    int DEFAULT = PERF_MW_MIN_MSGS;
    if (is_rtc) {
        DEFAULT = PERF_MW_MIN_MSGS_FOR_RTC;
    }
    if (is_realtime) {
        DEFAULT = PERF_MW_MIN_MSGS_REALTIME;
    }

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("play");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("mw_msgs");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    int v = ::atoi(conf->Arg0().c_str());
    if (v > PERF_MW_MSGS) {
        warn("reset mw_msgs %d to max %d", v, PERF_MW_MSGS);
        v = PERF_MW_MSGS;
    }

    return v;
}

bool Config::GetRealtimeEnabled(std::string vhost, bool is_rtc)
{
    if (is_rtc) {
        OVERWRITE_BY_ENV_BOOL2("srs.vhost.min_latency");
    } else {
        OVERWRITE_BY_ENV_BOOL("srs.vhost.min_latency");
    }

    static bool SYS_DEFAULT = PERF_MIN_LATENCY_ENABLED;
    static bool RTC_DEFAULT = true;

    bool DEFAULT = is_rtc? RTC_DEFAULT : SYS_DEFAULT;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("min_latency");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    if (is_rtc) {
        return CONF_PERFER_TRUE(conf->Arg0());
    } else {
        return CONF_PERFER_FALSE(conf->Arg0());
    }
}

bool Config::GetTcpNodelay(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL("srs.vhost.tcp_nodelay");

    static bool DEFAULT = false;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("tcp_nodelay");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

utime_t Config::GetSendMinInterval(std::string vhost)
{
    OVERWRITE_BY_ENV_FLOAT_MILLISECONDS("srs.vhost.play.send_min_interval");

    static utime_t DEFAULT = 0;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("play");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("send_min_interval");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return utime_t(::atof(conf->Arg0().c_str()) * UTIME_MILLISECONDS);
}

bool Config::GetReduceSequenceHeader(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL("srs.vhost.play.reduce_sequence_header");

    static bool DEFAULT = false;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("play");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("reduce_sequence_header");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

utime_t Config::GetPublish1StpktTimeout(std::string vhost)
{
    OVERWRITE_BY_ENV_MILLISECONDS("srs.vhost.publish.firstpkt_timeout");

    // when no msg recevied for publisher, use larger timeout.
    static utime_t DEFAULT = 20 * UTIME_SECONDS;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("publish");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("firstpkt_timeout");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return (utime_t)(::atoi(conf->Arg0().c_str()) * UTIME_MILLISECONDS);
}

utime_t Config::GetPublishNormalTimeout(std::string vhost)
{
    OVERWRITE_BY_ENV_MILLISECONDS("srs.vhost.publish.normal_timeout");

    // the timeout for publish recv.
    // we must use more smaller timeout, for the recv never know the status
    // of underlayer socket.
    static utime_t DEFAULT = 5 * UTIME_SECONDS;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("publish");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("normal_timeout");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return (utime_t)(::atoi(conf->Arg0().c_str()) * UTIME_MILLISECONDS);
}

int Config::GetGlobalChunkSize()
{
    OVERWRITE_BY_ENV_INT("srs.chunk_size");

    ConfDirective* conf = m_root->Get("chunk_size");
    if (!conf || conf->Arg0().empty()) {
        return CONSTS_RTMP_CHUNK_SIZE;
    }

    return ::atoi(conf->Arg0().c_str());
}

bool Config::GetForwardEnabled(std::string vhost)
{
    static bool DEFAULT = false;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    return GetForwardEnabled(conf);
}

bool Config::GetForwardEnabled(ConfDirective *vhost)
{
    static bool DEFAULT = false;

    ConfDirective* conf = vhost->Get("forward");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

ConfDirective *Config::GetForwards(std::string vhost)
{
    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return NULL;
    }

    conf = conf->Get("forward");
    if (!conf) {
        return NULL;
    }

    return conf->Get("destination");
}

ConfDirective *Config::GetForwardBackend(std::string vhost)
{
    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return NULL;
    }

    conf = conf->Get("forward");
    if (!conf) {
        return NULL;
    }

    return conf->Get("backend");
}

bool Config::GetSrtEnabled()
{
    OVERWRITE_BY_ENV_BOOL("srs.srt_server.enabled");

    static bool DEFAULT = false;

    ConfDirective* conf = m_root->Get("srt_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

unsigned short Config::GetSrtListenPort()
{
    OVERWRITE_BY_ENV_INT("srs.srt_server.listen");

    static unsigned short DEFAULT = 10080;
    ConfDirective* conf = m_root->Get("srt_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("listen");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }
    return (unsigned short)atoi(conf->Arg0().c_str());
}

int Config::GetSrtoMaxbw()
{
    OVERWRITE_BY_ENV_INT("srs.srt_server.maxbw");

    static int64_t DEFAULT = -1;
    ConfDirective* conf = m_root->Get("srt_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("maxbw");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }
    return atoi(conf->Arg0().c_str());
}

int Config::GetSrtoMss()
{
    OVERWRITE_BY_ENV_INT("srs.srt_server.mms");

    static int DEFAULT = 1500;
    ConfDirective* conf = m_root->Get("srt_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("mms");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }
    return atoi(conf->Arg0().c_str());
}

bool Config::GetSrtoTsbpdmode()
{
    OVERWRITE_BY_ENV_BOOL2("srs.srt_server.tsbpdmode");

    static bool DEFAULT = true;
    ConfDirective* conf = m_root->Get("srt_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("tsbpdmode");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }
    return CONF_PERFER_TRUE(conf->Arg0());
}

int Config::GetSrtoLatency()
{
    OVERWRITE_BY_ENV_INT("srs.srt_server.latency");

    static int DEFAULT = 120;
    ConfDirective* conf = m_root->Get("srt_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("latency");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }
    return atoi(conf->Arg0().c_str());
}

int Config::GetSrtoRecvLatency()
{
    OVERWRITE_BY_ENV_INT("srs.srt_server.recvlatency");

    static int DEFAULT = 120;
    ConfDirective* conf = m_root->Get("srt_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("recvlatency");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }
    return atoi(conf->Arg0().c_str());
}

int Config::GetSrtoPeerLatency()
{
    OVERWRITE_BY_ENV_INT("srs.srt_server.peerlatency");

    static int DEFAULT = 0;
    ConfDirective* conf = m_root->Get("srt_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("peerlatency");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }
    return atoi(conf->Arg0().c_str());
}

bool Config::GetSrtSeiFilter()
{
    OVERWRITE_BY_ENV_BOOL2("srs.srt_server.sei_filter");

    static bool DEFAULT = true;
    ConfDirective* conf = m_root->Get("srt_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("sei_filter");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }
    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::GetSrtoTlpktdrop()
{
    OVERWRITE_BY_ENV_BOOL2("srs.srt_server.tlpkdrop.tlpktdrop");

    static bool DEFAULT = true;
    ConfDirective* srt_server_conf = m_root->Get("srt_server");
    if (!srt_server_conf) {
        return DEFAULT;
    }

    ConfDirective* conf = srt_server_conf->Get("tlpkdrop");
    if (!conf) {
        // make it compatible tlpkdrop and tlpktdrop opt.
        conf = srt_server_conf->Get("tlpktdrop");
    }
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }
    return CONF_PERFER_TRUE(conf->Arg0());
}

utime_t Config::GetSrtoConntimeout()
{
    OVERWRITE_BY_ENV_MILLISECONDS("srs.srt_server.connect_timeout");

    static utime_t DEFAULT = 3 * UTIME_SECONDS;
    ConfDirective* conf = m_root->Get("srt_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("connect_timeout");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }
    return (utime_t)(::atoi(conf->Arg0().c_str()) * UTIME_MILLISECONDS);
}

utime_t Config::GetSrtoPeeridletimeout()
{
    OVERWRITE_BY_ENV_MILLISECONDS("srs.srt_server.peer_idle_timeout");

    static utime_t DEFAULT = 10 * UTIME_SECONDS;
    ConfDirective* conf = m_root->Get("srt_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("peer_idle_timeout");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }
    return (utime_t)(::atoi(conf->Arg0().c_str()) * UTIME_MILLISECONDS);
}

int Config::GetSrtoSendbuf()
{
    OVERWRITE_BY_ENV_INT("srs.srt_server.sendbuf");

    static int DEFAULT = 8192 * (1500-28);
    ConfDirective* conf = m_root->Get("srt_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("sendbuf");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }
    return atoi(conf->Arg0().c_str());
}

int Config::GetSrtoRecvbuf()
{
    OVERWRITE_BY_ENV_INT("srs.srt_server.recvbuf");

    static int DEFAULT = 8192 * (1500-28);
    ConfDirective* conf = m_root->Get("srt_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("recvbuf");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }
    return atoi(conf->Arg0().c_str());
}

int Config::GetSrtoPayloadsize()
{
    OVERWRITE_BY_ENV_INT("srs.srt_server.payloadsize");

    static int DEFAULT = 1316;
    ConfDirective* conf = m_root->Get("srt_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("payloadsize");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }
    return atoi(conf->Arg0().c_str());
}

std::string Config::GetSrtoPassphrase()
{
    OVERWRITE_BY_ENV_STRING("srs.srt_server.passphrase");

    static std::string DEFAULT = "";
    ConfDirective* conf = m_root->Get("srt_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("passphrase");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }
    return conf->Arg0();
}

int Config::GetSrtoPbkeylen()
{
    OVERWRITE_BY_ENV_INT("srs.srt_server.pbkeylen");

    static int DEFAULT = 0;
    ConfDirective* conf = m_root->Get("srt_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("pbkeylen");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }
    return atoi(conf->Arg0().c_str());
}

std::string Config::GetDefaultAppName()
{
    OVERWRITE_BY_ENV_STRING("srs.srt_server.default_app");

    static std::string DEFAULT = "live";
    ConfDirective* conf = m_root->Get("srt_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("default_app");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }
    return conf->Arg0();
}

ConfDirective *Config::GetSrt(std::string vhost)
{
    ConfDirective* conf = GetVhost(vhost);
    return conf? conf->Get("srt") : NULL;
}

bool Config::GetSrtEnabled(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL("srs.vhost.srt.enabled");

    static bool DEFAULT = false;

    ConfDirective* conf = GetSrt(vhost);

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

bool Config::GetSrtToRtmp(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL("srs.vhost.srt.srt_to_rtmp");

    static bool DEFAULT = true;

    ConfDirective* conf = GetSrt(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("srt_to_rtmp");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

ConfDirective *Config::GetVhostHttpHooks(std::string vhost)
{
    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return NULL;
    }

    return conf->Get("http_hooks");
}

bool Config::GetVhostHttpHooksEnabled(std::string vhost)
{
    static bool DEFAULT = false;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    return GetVhostHttpHooksEnabled(conf);
}

bool Config::GetVhostHttpHooksEnabled(ConfDirective *vhost)
{
    static bool DEFAULT = false;

    ConfDirective* conf = vhost->Get("http_hooks");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

ConfDirective *Config::GetVhostOnConnect(std::string vhost)
{
    ConfDirective* conf = GetVhostHttpHooks(vhost);
    if (!conf) {
        return NULL;
    }

    return conf->Get("on_connect");
}

ConfDirective *Config::GetVhostOnClose(std::string vhost)
{
    ConfDirective* conf = GetVhostHttpHooks(vhost);
    if (!conf) {
        return NULL;
    }

    return conf->Get("on_close");
}

ConfDirective *Config::GetVhostOnPublish(std::string vhost)
{
    ConfDirective* conf = GetVhostHttpHooks(vhost);
    if (!conf) {
        return NULL;
    }

    return conf->Get("on_publish");
}

ConfDirective *Config::GetVhostOnUnpublish(std::string vhost)
{
    ConfDirective* conf = GetVhostHttpHooks(vhost);
    if (!conf) {
        return NULL;
    }

    return conf->Get("on_unpublish");
}

ConfDirective *Config::GetVhostOnPlay(std::string vhost)
{
    ConfDirective* conf = GetVhostHttpHooks(vhost);
    if (!conf) {
        return NULL;
    }

    return conf->Get("on_play");
}

ConfDirective *Config::GetVhostOnStop(std::string vhost)
{
    ConfDirective* conf = GetVhostHttpHooks(vhost);
    if (!conf) {
        return NULL;
    }

    return conf->Get("on_stop");
}

ConfDirective *Config::GetVhostOnDvr(std::string vhost)
{
    ConfDirective* conf = GetVhostHttpHooks(vhost);
    if (!conf) {
        return NULL;
    }

    return conf->Get("on_dvr");
}

ConfDirective *Config::GetVhostOnHls(std::string vhost)
{
    ConfDirective* conf = GetVhostHttpHooks(vhost);
    if (!conf) {
        return NULL;
    }

    return conf->Get("on_hls");
}

ConfDirective *Config::GetVhostOnHlsNotify(std::string vhost)
{
    ConfDirective* conf = GetVhostHttpHooks(vhost);
    if (!conf) {
        return NULL;
    }

    return conf->Get("on_hls_notify");
}

bool Config::GetVhostIsEdge(std::string vhost)
{
    ConfDirective* conf = GetVhost(vhost);
    return GetVhostIsEdge(conf);
}

bool Config::GetVhostIsEdge(ConfDirective *vhost)
{
    static bool DEFAULT = false;

    ConfDirective* conf = vhost;
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("cluster");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("mode");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return "remote" == conf->Arg0();
}

ConfDirective *Config::GetVhostEdgeOrigin(std::string vhost)
{
    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return NULL;
    }

    conf = conf->Get("cluster");
    if (!conf) {
        return NULL;
    }

    return conf->Get("origin");
}

std::string Config::GetVhostEdgeProtocol(std::string vhost)
{
    static std::string DEFAULT = "rtmp";

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("cluster");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("protocol");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

bool Config::GetVhostEdgeFollowClient(std::string vhost)
{
    static bool DEFAULT = false;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("cluster");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("follow_client");
    if (!conf) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

bool Config::GetVhostEdgeTokenTraverse(std::string vhost)
{
    static bool DEFAULT = false;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("cluster");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("token_traverse");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

std::string Config::GetVhostEdgeTransformVhost(std::string vhost)
{
    static std::string DEFAULT = "[vhost]";

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("cluster");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("vhost");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

bool Config::GetVhostOriginCluster(std::string vhost)
{
    ConfDirective* conf = GetVhost(vhost);
    return GetVhostOriginCluster(conf);
}

bool Config::GetVhostOriginCluster(ConfDirective *vhost)
{
    static bool DEFAULT = false;

    ConfDirective* conf = vhost;
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("cluster");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("origin_cluster");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

std::vector<std::string> Config::GetVhostCoworkers(std::string vhost)
{
    std::vector<std::string> coworkers;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return coworkers;
    }

    conf = conf->Get("cluster");
    if (!conf) {
        return coworkers;
    }

    conf = conf->Get("coworkers");
    if (!conf) {
        return coworkers;
    }
    for (int i = 0; i < (int)conf->m_args.size(); i++) {
        coworkers.push_back(conf->m_args.at(i));
    }

    return coworkers;
}

bool Config::GetSecurityEnabled(std::string vhost)
{
    static bool DEFAULT = false;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    return GetSecurityEnabled(conf);
}

bool Config::GetSecurityEnabled(ConfDirective *vhost)
{
    static bool DEFAULT = false;

    ConfDirective* conf = vhost->Get("security");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

ConfDirective *Config::GetSecurityRules(std::string vhost)
{
    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return NULL;
    }

    return conf->Get("security");
}

ConfDirective *Config::GetTranscode(std::string vhost, std::string scope)
{
    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return NULL;
    }

    conf = conf->Get("transcode");
    if (!conf || conf->Arg0() != scope) {
        return NULL;
    }

    return conf;
}

bool Config::GetTranscodeEnabled(ConfDirective *conf)
{
    static bool DEFAULT = false;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

std::string Config::GetTranscodeFfmpeg(ConfDirective *conf)
{
    static std::string DEFAULT = "";

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("ffmpeg");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::vector<ConfDirective *> Config::GetTranscodeEngines(ConfDirective *conf)
{
    std::vector<ConfDirective*> engines;

    if (!conf) {
        return engines;
    }

    for (int i = 0; i < (int)conf->m_directives.size(); i++) {
        ConfDirective* engine = conf->m_directives[i];

        if (engine->m_name == "engine") {
            engines.push_back(engine);
        }
    }

    return engines;
}

bool Config::GetEngineEnabled(ConfDirective *conf)
{
    static bool DEFAULT = false;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

std::string PrefixUnderscoresIfno(std::string name)
{
    if (StringStartsWith(name, "-")) {
        return name;
    } else {
        return "-" + name;
    }
}

std::vector<std::string> Config::GetEnginePerfile(ConfDirective *conf)
{
    std::vector<std::string> perfile;

    if (!conf) {
        return perfile;
    }

    conf = conf->Get("perfile");
    if (!conf) {
        return perfile;
    }

    for (int i = 0; i < (int)conf->m_directives.size(); i++) {
        ConfDirective* option = conf->m_directives[i];
        if (!option) {
            continue;
        }

        perfile.push_back(PrefixUnderscoresIfno(option->m_name));
        if (!option->Arg0().empty()) {
            perfile.push_back(option->Arg0());
        }
    }

    return perfile;
}

std::string Config::GetEngineIformat(ConfDirective *conf)
{
    static std::string DEFAULT = "flv";

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("iformat");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::vector<std::string> Config::GetEngineVfilter(ConfDirective *conf)
{
    std::vector<std::string> vfilter;

    if (!conf) {
        return vfilter;
    }

    conf = conf->Get("vfilter");
    if (!conf) {
        return vfilter;
    }

    for (int i = 0; i < (int)conf->m_directives.size(); i++) {
        ConfDirective* filter = conf->m_directives[i];
        if (!filter) {
            continue;
        }

        vfilter.push_back(PrefixUnderscoresIfno(filter->m_name));
        if (!filter->Arg0().empty()) {
            vfilter.push_back(filter->Arg0());
        }
    }

    return vfilter;
}

std::string Config::GetEngineVcodec(ConfDirective *conf)
{
    static std::string DEFAULT = "";

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("vcodec");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

int Config::GetEngineVbitrate(ConfDirective *conf)
{
    static int DEFAULT = 0;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("vbitrate");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

double Config::GetEngineVfps(ConfDirective *conf)
{
    static double DEFAULT = 0;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("vfps");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atof(conf->Arg0().c_str());
}

int Config::GetEngineVwidth(ConfDirective *conf)
{
    static int DEFAULT = 0;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("vwidth");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

int Config::GetEngineVheight(ConfDirective *conf)
{
    static int DEFAULT = 0;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("vheight");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

int Config::GetEngineVthreads(ConfDirective *conf)
{
    static int DEFAULT = 1;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("vthreads");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

std::string Config::GetEngineVprofile(ConfDirective *conf)
{
    static std::string DEFAULT = "";

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("vprofile");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetEngineVpreset(ConfDirective *conf)
{
    static std::string DEFAULT = "";

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("vpreset");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::vector<std::string> Config::GetEngineVparams(ConfDirective *conf)
{
    std::vector<std::string> vparams;

    if (!conf) {
        return vparams;
    }

    conf = conf->Get("vparams");
    if (!conf) {
        return vparams;
    }

    for (int i = 0; i < (int)conf->m_directives.size(); i++) {
        ConfDirective* filter = conf->m_directives[i];
        if (!filter) {
            continue;
        }

        vparams.push_back(PrefixUnderscoresIfno(filter->m_name));
        if (!filter->Arg0().empty()) {
            vparams.push_back(filter->Arg0());
        }
    }

    return vparams;
}

std::string Config::GetEngineAcodec(ConfDirective *conf)
{
    static std::string DEFAULT = "";

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("acodec");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

int Config::GetEngineAbitrate(ConfDirective *conf)
{
    static int DEFAULT = 0;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("abitrate");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

int Config::GetEngineAsampleRate(ConfDirective *conf)
{
    static int DEFAULT = 0;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("asample_rate");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

int Config::GetEngineAchannels(ConfDirective *conf)
{
    static int DEFAULT = 0;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("achannels");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

std::vector<std::string> Config::GetEngineAparams(ConfDirective *conf)
{
    std::vector<std::string> aparams;

    if (!conf) {
        return aparams;
    }

    conf = conf->Get("aparams");
    if (!conf) {
        return aparams;
    }

    for (int i = 0; i < (int)conf->m_directives.size(); i++) {
        ConfDirective* filter = conf->m_directives[i];
        if (!filter) {
            continue;
        }

        aparams.push_back(PrefixUnderscoresIfno(filter->m_name));
        if (!filter->Arg0().empty()) {
            aparams.push_back(filter->Arg0());
        }
    }

    return aparams;
}

std::string Config::GetEngineOformat(ConfDirective *conf)
{
    static std::string DEFAULT = "flv";

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("oformat");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetEngineOutput(ConfDirective *conf)
{
    static std::string DEFAULT = "";

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("output");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

ConfDirective *Config::GetExec(std::string vhost)
{
    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return NULL;
    }

    return conf->Get("exec");
}

bool Config::GetExecEnabled(std::string vhost)
{
    static bool DEFAULT = false;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    return GetExecEnabled(conf);
}

bool Config::GetExecEnabled(ConfDirective *vhost)
{
    static bool DEFAULT = false;

    ConfDirective* conf = vhost->Get("exec");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

std::vector<ConfDirective *> Config::GetExecPublishs(std::string vhost)
{
    std::vector<ConfDirective*> eps;

    ConfDirective* conf = GetExec(vhost);
    if (!conf) {
        return eps;
    }

    for (int i = 0; i < (int)conf->m_directives.size(); i++) {
        ConfDirective* ep = conf->At(i);
        if (ep->m_name == "publish") {
            eps.push_back(ep);
        }
    }

    return eps;
}

std::vector<ConfDirective *> Config::GetIngesters(std::string vhost)
{
    std::vector<ConfDirective*> integers;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return integers;
    }

    for (int i = 0; i < (int)conf->m_directives.size(); i++) {
        ConfDirective* ingester = conf->m_directives[i];

        if (ingester->m_name == "ingest") {
            integers.push_back(ingester);
        }
    }

    return integers;
}

ConfDirective *Config::GetIngestById(std::string vhost, std::string ingest_id)
{
    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return NULL;
    }

    return conf->Get("ingest", ingest_id);
}

bool Config::GetIngestEnabled(ConfDirective *conf)
{
    static bool DEFAULT = false;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

std::string Config::GetIngestFfmpeg(ConfDirective *conf)
{
    static std::string DEFAULT = "";

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("ffmpeg");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetIngestInputType(ConfDirective *conf)
{
    static std::string DEFAULT = "file";

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("input");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("type");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetIngestInputUrl(ConfDirective *conf)
{
    static std::string DEFAULT = "";

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("input");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("url");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

extern bool in_docker;

bool Config::GetLogTankFile()
{
    if (!Getenv("srs.log_tank").empty()) {
        return Getenv("srs.log_tank") != "console";
    }

    static bool DEFAULT = true;

    if (in_docker) {
        DEFAULT = false;
    }

    ConfDirective* conf = m_root->Get("log_tank");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0() != "console";
}

std::string Config::GetLogLevel()
{
    OVERWRITE_BY_ENV_STRING("srs.log_level");

    static std::string DEFAULT = "trace";

    ConfDirective* conf = m_root->Get("log_level");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetLogLevelV2()
{
    OVERWRITE_BY_ENV_STRING("srs.log_level_v2");

    static std::string DEFAULT = "";

    ConfDirective* conf = m_root->Get("log_level_v2");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetLogFile()
{
    OVERWRITE_BY_ENV_STRING("srs.log_file");

    static std::string DEFAULT = "./objs/srs.log";

    ConfDirective* conf = m_root->Get("log_file");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

bool Config::GetFfLogEnabled()
{
    std::string log = GetFfLogDir();
    return log != CONSTS_NULL_FILE;
}

std::string Config::GetFfLogDir()
{
    OVERWRITE_BY_ENV_STRING("srs.ff_log_dir");

    static std::string DEFAULT = "./objs";

    ConfDirective* conf = m_root->Get("ff_log_dir");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetFfLogLevel()
{
    OVERWRITE_BY_ENV_STRING("srs.ff_log_level");

    static std::string DEFAULT = "info";

    ConfDirective* conf = m_root->Get("ff_log_level");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

ConfDirective *Config::GetDash(std::string vhost)
{
    ConfDirective* conf = GetVhost(vhost);
    return conf? conf->Get("dash") : NULL;
}

bool Config::GetDashEnabled(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL("srs.vhost.dash.enabled");

    static bool DEFAULT = false;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    return GetDashEnabled(conf);
}

bool Config::GetDashEnabled(ConfDirective *vhost)
{
    static bool DEFAULT = false;

    ConfDirective* conf = vhost->Get("dash");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

utime_t Config::GetDashFragment(std::string vhost)
{
    OVERWRITE_BY_ENV_FLOAT_SECONDS("srs.vhost.dash.dash_fragment");

    static int DEFAULT = 30 * UTIME_SECONDS;

    ConfDirective* conf = GetDash(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("dash_fragment");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return (utime_t)(::atof(conf->Arg0().c_str()) * UTIME_SECONDS);
}

utime_t Config::GetDashUpdatePeriod(std::string vhost)
{
    OVERWRITE_BY_ENV_FLOAT_SECONDS("srs.vhost.dash.dash_update_period");

    static utime_t DEFAULT = 150 * UTIME_SECONDS;

    ConfDirective* conf = GetDash(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("dash_update_period");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return (utime_t)(::atof(conf->Arg0().c_str()) * UTIME_SECONDS);
}

utime_t Config::GetDashTimeshift(std::string vhost)
{
    OVERWRITE_BY_ENV_FLOAT_SECONDS("srs.vhost.dash.dash_timeshift");

    static utime_t DEFAULT = 300 * UTIME_SECONDS;

    ConfDirective* conf = GetDash(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("dash_timeshift");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return (utime_t)(::atof(conf->Arg0().c_str()) * UTIME_SECONDS);
}

std::string Config::GetDashPath(std::string vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.vhost.dash.dash_path");

    static std::string DEFAULT = "./objs/nginx/html";

    ConfDirective* conf = GetDash(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("dash_path");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetDashMpdFile(std::string vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.vhost.dash.dash_mpd_file");

    static std::string DEFAULT = "[app]/[stream].mpd";

    ConfDirective* conf = GetDash(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("dash_mpd_file");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

ConfDirective *Config::GetHls(std::string vhost)
{
    ConfDirective* conf = GetVhost(vhost);
    return conf? conf->Get("hls") : NULL;
}

bool Config::GetHlsEnabled(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL("srs.vhost.hls.enabled");

    static bool DEFAULT = false;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    return GetHlsEnabled(conf);
}

bool Config::GetHlsEnabled(ConfDirective *vhost)
{
    static bool DEFAULT = false;

    ConfDirective* conf = vhost->Get("hls");

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

std::string Config::GetHlsEntryPrefix(std::string vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.vhost.hls.hls_entry_prefix");

    static std::string DEFAULT = "";

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_entry_prefix");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetHlsPath(std::string vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.vhost.hls.hls_path");

    static std::string DEFAULT = "./objs/nginx/html";

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_path");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetHlsM3u8File(std::string vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.vhost.hls.hls_m3u8_file");

    static std::string DEFAULT = "[app]/[stream].m3u8";

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_m3u8_file");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetHlsTsFile(std::string vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.vhost.hls.hls_ts_file");

    static std::string DEFAULT = "[app]/[stream]-[seq].ts";

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_ts_file");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

bool Config::GetHlsTsFloor(std::string vhost)
{
    static bool DEFAULT = false;

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_ts_floor");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

utime_t Config::GetHlsFragment(std::string vhost)
{
    OVERWRITE_BY_ENV_FLOAT_SECONDS("srs.vhost.hls.hls_fragment");

    static utime_t DEFAULT = 10 * UTIME_SECONDS;

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_fragment");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return utime_t(::atof(conf->Arg0().c_str()) * UTIME_SECONDS);
}

double Config::GetHlsTdRatio(std::string vhost)
{
    OVERWRITE_BY_ENV_FLOAT("srs.vhost.hls.hls_td_ratio");

    static double DEFAULT = 1.5;

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_td_ratio");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atof(conf->Arg0().c_str());
}

double Config::GetHlsAofRatio(std::string vhost)
{
    OVERWRITE_BY_ENV_FLOAT("srs.vhost.hls.hls_aof_ratio");

    static double DEFAULT = 2.0;

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_aof_ratio");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atof(conf->Arg0().c_str());
}

utime_t Config::GetHlsWindow(std::string vhost)
{
    OVERWRITE_BY_ENV_FLOAT_SECONDS("srs.vhost.hls.hls_window");

    static utime_t DEFAULT = (60 * UTIME_SECONDS);

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_window");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return utime_t(::atof(conf->Arg0().c_str()) * UTIME_SECONDS);
}

std::string Config::GetHlsOnError(std::string vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.vhost.hls.hls_on_error");

    // try to ignore the error.
    static std::string DEFAULT = "continue";

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_on_error");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetHlsAcodec(std::string vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.vhost.hls.hls_acodec");

    static std::string DEFAULT = "aac";

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_acodec");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetHlsVcodec(std::string vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.vhost.hls.hls_vcodec");

    static std::string DEFAULT = "h264";

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_vcodec");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

bool Config::GetHlsCleanup(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL2("srs.vhost.hls.hls_cleanup");

    static bool DEFAULT = true;

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_cleanup");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

utime_t Config::GetHlsDispose(std::string vhost)
{
    OVERWRITE_BY_ENV_SECONDS("srs.vhost.hls.hls_dispose");

    static utime_t DEFAULT = 0;

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_dispose");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return (utime_t)(::atoi(conf->Arg0().c_str()) * UTIME_SECONDS);
}

bool Config::GetHlsWaitKeyframe(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL2("srs.vhost.hls.hls_wait_keyframe");

    static bool DEFAULT = true;

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_wait_keyframe");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::GetHlsKeys(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL2("srs.vhost.hls.hls_keys");

    static bool DEFAULT = false;

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_keys");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

int Config::GetHlsFragmentsPerKey(std::string vhost)
{
    OVERWRITE_BY_ENV_INT("srs.vhost.hls.hls_fragments_per_key");

    static int DEFAULT = 5;

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_fragments_per_key");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

std::string Config::GetHlsKeyFile(std::string vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.vhost.hls.hls_key_file");

    static std::string DEFAULT = "[app]/[stream]-[seq].key";

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_key_file");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetHlsKeyFilePath(std::string vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.vhost.hls.hls_key_file_path");

     //put the key in ts path defaultly.
    static std::string DEFAULT = GetHlsPath(vhost);

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_key_file_path");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetHlsKeyUrl(std::string vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.vhost.hls.hls_key_url");

     //put the key in ts path defaultly.
    static std::string DEFAULT = GetHlsPath(vhost);

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_key_url");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

int Config::GetVhostHlsNbNotify(std::string vhost)
{
    OVERWRITE_BY_ENV_INT("srs.vhost.hls.hls_nb_notify");

    static int DEFAULT = 64;

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_nb_notify");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

bool Config::GetVhostHlsDtsDirectly(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL2("srs.vhost.hls.hls_dts_directly");

    static bool DEFAULT = true;

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_dts_directly");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::GetHlsCtxEnabled(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL2("srs.vhost.hls.hls_ctx");

    static bool DEFAULT = true;

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_ctx");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::GetHlsTsCtxEnabled(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL2("srs.vhost.hls.hls_ts_ctx");

    static bool DEFAULT = true;

    ConfDirective* conf = GetHls(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hls_ts_ctx");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

ConfDirective *Config::GetHds(const std::string &vhost)
{
    ConfDirective* conf = GetVhost(vhost);

    if (!conf) {
        return NULL;
    }

    return conf->Get("hds");
}

bool Config::GetHdsEnabled(const std::string &vhost)
{
    OVERWRITE_BY_ENV_BOOL("srs.vhost.hds.enabled");

    static bool DEFAULT = false;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    return GetHdsEnabled(conf);
}

bool Config::GetHdsEnabled(ConfDirective *vhost)
{
    static bool DEFAULT = false;

    ConfDirective* conf = vhost->Get("hds");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

std::string Config::GetHdsPath(const std::string &vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.vhost.hds.hds_path");

    static std::string DEFAULT = "./objs/nginx/html";

    ConfDirective* conf = GetHds(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hds_path");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

utime_t Config::GetHdsFragment(const std::string &vhost)
{
    OVERWRITE_BY_ENV_FLOAT_SECONDS("srs.vhost.hds.hds_fragment");

    static utime_t DEFAULT = (10 * UTIME_SECONDS);

    ConfDirective* conf = GetHds(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hds_fragment");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return utime_t(::atof(conf->Arg0().c_str()) * UTIME_SECONDS);
}

utime_t Config::GetHdsWindow(const std::string &vhost)
{
    OVERWRITE_BY_ENV_FLOAT_SECONDS("srs.vhost.hds.hds_window");

    static utime_t DEFAULT = (60 * UTIME_SECONDS);

    ConfDirective* conf = GetHds(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("hds_window");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return utime_t(::atof(conf->Arg0().c_str()) * UTIME_SECONDS);
}

ConfDirective *Config::GetDvr(std::string vhost)
{
    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return NULL;
    }

    return conf->Get("dvr");
}

bool Config::GetDvrEnabled(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL("srs.vhost.dvr.enabled");

    static bool DEFAULT = false;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    return GetDvrEnabled(conf);
}

bool Config::GetDvrEnabled(ConfDirective *vhost)
{
    static bool DEFAULT = false;

    ConfDirective* conf = vhost->Get("dvr");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

ConfDirective *Config::GetDvrApply(std::string vhost)
{
    ConfDirective* conf = GetDvr(vhost);
    if (!conf) {
        return NULL;
    }

    conf = conf->Get("dvr_apply");
    if (!conf || conf->Arg0().empty()) {
        return NULL;
    }

    return conf;
}

std::string Config::GetDvrPath(std::string vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.vhost.dvr.dvr_path");

    static std::string DEFAULT = "./objs/nginx/html/[app]/[stream].[timestamp].flv";

    ConfDirective* conf = GetDvr(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("dvr_path");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetDvrPlan(std::string vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.vhost.dvr.dvr_plan");

    static std::string DEFAULT = "session";

    ConfDirective* conf = GetDvr(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("dvr_plan");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

utime_t Config::GetDvrDuration(std::string vhost)
{
    OVERWRITE_BY_ENV_SECONDS("srs.vhost.dvr.dvr_duration");

    static utime_t DEFAULT = 30 * UTIME_SECONDS;

    ConfDirective* conf = GetDvr(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("dvr_duration");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return (utime_t)(::atoi(conf->Arg0().c_str()) * UTIME_SECONDS);
}

bool Config::GetDvrWaitKeyframe(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL2("srs.vhost.dvr.dvr_wait_keyframe");

    static bool DEFAULT = true;

    ConfDirective* conf = GetDvr(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("dvr_wait_keyframe");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

int Config::GetDvrTimeJitter(std::string vhost)
{
    if (!Getenv("srs.vhost.dvr.dvr_wait_keyframe").empty()) {
        return TimeJitterString2int(Getenv("srs.vhost.dvr.dvr_wait_keyframe"));
    }

    static std::string DEFAULT = "full";

    ConfDirective* conf = GetDvr(vhost);

    if (!conf) {
        return TimeJitterString2int(DEFAULT);
    }

    conf = conf->Get("time_jitter");
    if (!conf || conf->Arg0().empty()) {
        return TimeJitterString2int(DEFAULT);
    }

    return TimeJitterString2int(conf->Arg0());
}

bool Config::GetHttpApiEnabled(ConfDirective *conf)
{
    OVERWRITE_BY_ENV_BOOL("srs.http_api.enabled");

    static bool DEFAULT = false;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

bool Config::GetHttpApiEnabled()
{
    ConfDirective* conf = m_root->Get("http_api");
    return GetHttpApiEnabled(conf);
}

std::string Config::GetHttpApiListen()
{
    OVERWRITE_BY_ENV_STRING("srs.http_api.listen");

    static std::string DEFAULT = "1985";

    ConfDirective* conf = m_root->Get("http_api");

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("listen");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

bool Config::GetHttpApiCrossdomain()
{
    OVERWRITE_BY_ENV_BOOL2("srs.http_api.crossdomain");

    static bool DEFAULT = true;

    ConfDirective* conf = m_root->Get("http_api");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("crossdomain");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

bool Config::GetRawApi()
{
    OVERWRITE_BY_ENV_BOOL("srs.http_api.raw_api.enabled");

    static bool DEFAULT = false;

    ConfDirective* conf = m_root->Get("http_api");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("raw_api");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

bool Config::GetRawApiAllowReload()
{
    OVERWRITE_BY_ENV_BOOL("srs.http_api.raw_api.allow_reload");

    static bool DEFAULT = false;

    ConfDirective* conf = m_root->Get("http_api");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("raw_api");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("allow_reload");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

bool Config::GetRawApiAllowQuery()
{
    // Disable RAW API query, @see https://github.com/ossrs/srs/issues/2653#issuecomment-939389178
    return false;
}

bool Config::GetRawApiAllowUpdate()
{
    // Disable RAW API update, @see https://github.com/ossrs/srs/issues/2653#issuecomment-939389178
    return false;
}

ConfDirective *Config::GetHttpsApi()
{
    ConfDirective* conf = m_root->Get("http_api");
    if (!conf) {
        return NULL;
    }

    return conf->Get("https");
}

bool Config::GetHttpsApiEnabled()
{
    OVERWRITE_BY_ENV_BOOL("srs.http_api.https.enabled");

    static bool DEFAULT = false;

    ConfDirective* conf = GetHttpsApi();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

std::string Config::GetHttpsApiListen()
{
    OVERWRITE_BY_ENV_STRING("srs.http_api.https.listen");

    // We should not use static default, because we need to reset for different use scenarios.
    std::string DEFAULT = "1990";
    // Follow the HTTPS server if config HTTP API as the same of HTTP server.
    if (GetHttpApiListen() == GetHttpStreamListen()) {
        DEFAULT = GetHttpsStreamListen();
    }

    ConfDirective* conf = GetHttpsApi();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("listen");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetHttpsApiSslKey()
{
    OVERWRITE_BY_ENV_STRING("srs.http_api.https.key");

    static std::string DEFAULT = "./conf/server.key";

    ConfDirective* conf = GetHttpsApi();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("key");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetHttpsApiSslCert()
{
    OVERWRITE_BY_ENV_STRING("srs.http_api.https.cert");

    static std::string DEFAULT = "./conf/server.crt";

    ConfDirective* conf = GetHttpsApi();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("cert");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

bool Config::GetHttpStreamEnabled(ConfDirective *conf)
{
    OVERWRITE_BY_ENV_BOOL("srs.http_server.enabled");

    static bool DEFAULT = false;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

bool Config::GetHttpStreamEnabled()
{
    ConfDirective* conf = m_root->Get("http_server");
    return GetHttpStreamEnabled(conf);

}

std::string Config::GetHttpStreamListen()
{
    OVERWRITE_BY_ENV_STRING("srs.http_server.listen");

    static std::string DEFAULT = "8080";

    ConfDirective* conf = m_root->Get("http_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("listen");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetHttpStreamDir()
{
    OVERWRITE_BY_ENV_STRING("srs.http_server.dir");

    static std::string DEFAULT = "./objs/nginx/html";

    ConfDirective* conf = m_root->Get("http_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("dir");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

bool Config::GetHttpStreamCrossdomain()
{
    OVERWRITE_BY_ENV_BOOL2("srs.http_server.crossdomain");

    static bool DEFAULT = true;

    ConfDirective* conf = m_root->Get("http_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("crossdomain");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

ConfDirective *Config::GetHttpsStream()
{
    ConfDirective* conf = m_root->Get("http_server");
    if (!conf) {
        return NULL;
    }

    return conf->Get("https");
}

bool Config::GetHttpsStreamEnabled()
{
    OVERWRITE_BY_ENV_BOOL("srs.http_server.https.enabled");

    static bool DEFAULT = false;

    ConfDirective* conf = GetHttpsStream();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

std::string Config::GetHttpsStreamListen()
{
    OVERWRITE_BY_ENV_STRING("srs.http_server.https.listen");

    static std::string DEFAULT = "8088";

    ConfDirective* conf = GetHttpsStream();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("listen");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetHttpsStreamSslKey()
{
    OVERWRITE_BY_ENV_STRING("srs.http_server.https.key");

    static std::string DEFAULT = "./conf/server.key";

    ConfDirective* conf = GetHttpsStream();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("key");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetHttpsStreamSslCert()
{
    OVERWRITE_BY_ENV_STRING("srs.http_server.https.cert");

    static std::string DEFAULT = "./conf/server.crt";

    ConfDirective* conf = GetHttpsStream();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("cert");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

bool Config::GetVhostHttpEnabled(std::string vhost)
{
    OVERWRITE_BY_ENV_BOOL("srs.http_static.enabled");

    static bool DEFAULT = false;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("http_static");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

std::string Config::GetVhostHttpMount(std::string vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.http_static.mount");

    static std::string DEFAULT = "[vhost]/";

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("http_static");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("mount");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetVhostHttpDir(std::string vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.http_static.dir");

    static std::string DEFAULT = "./objs/nginx/html";

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("http_static");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("dir");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

bool Config::GetVhostHttpRemuxEnabled(std::string vhost)
{
    static bool DEFAULT = false;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    return GetVhostHttpRemuxEnabled(conf);
}

bool Config::GetVhostHttpRemuxEnabled(ConfDirective *vhost)
{
    OVERWRITE_BY_ENV_BOOL("srs.http_remux.enabled");

    static bool DEFAULT = false;

    ConfDirective* conf = vhost->Get("http_remux");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

utime_t Config::GetVhostHttpRemuxFastCache(std::string vhost)
{
    OVERWRITE_BY_ENV_FLOAT_SECONDS("srs.http_remux.fast_cache");

    static utime_t DEFAULT = 0;

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("http_remux");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("fast_cache");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return utime_t(::atof(conf->Arg0().c_str()) * UTIME_SECONDS);
}

std::string Config::GetVhostHttpRemuxMount(std::string vhost)
{
    OVERWRITE_BY_ENV_STRING("srs.http_remux.mount");

    static std::string DEFAULT = "[vhost]/[app]/[stream].flv";

    ConfDirective* conf = GetVhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("http_remux");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("mount");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

ConfDirective *Config::GetHeartbeart()
{
    return m_root->Get("heartbeat");
}

bool Config::GetHeartbeatEnabled()
{
    OVERWRITE_BY_ENV_BOOL("srs.heartbeat.enabled");

    static bool DEFAULT = false;

    ConfDirective* conf = GetHeartbeart();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

utime_t Config::GetHeartbeatInterval()
{
    OVERWRITE_BY_ENV_SECONDS("srs.heartbeat.interval");

    static utime_t DEFAULT = (utime_t)(10 * UTIME_SECONDS);

    ConfDirective* conf = GetHeartbeart();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("interval");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return (utime_t)(::atoi(conf->Arg0().c_str()) * UTIME_SECONDS);
}

std::string Config::GetHeartbeatUrl()
{
    OVERWRITE_BY_ENV_STRING("srs.heartbeat.url");

    static std::string DEFAULT = "http://" CONSTS_LOCALHOST ":8085/api/v1/servers";

    ConfDirective* conf = GetHeartbeart();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("url");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetHeartbeatDeviceId()
{
    OVERWRITE_BY_ENV_STRING("srs.heartbeat.device_id");

    static std::string DEFAULT = "";

    ConfDirective* conf = GetHeartbeart();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("device_id");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}

bool Config::GetHeartbeatSummaries()
{
    OVERWRITE_BY_ENV_BOOL("srs.heartbeat.summaries");

    static bool DEFAULT = false;

    ConfDirective* conf = GetHeartbeart();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("summaries");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

ConfDirective *Config::GetStats()
{
    return m_root->Get("stats");
}

bool Config::GetStatsEnabled()
{
    static bool DEFAULT = true;

    ConfDirective* conf = GetStats();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_TRUE(conf->Arg0());
}

int Config::GetStatsNetwork()
{
    static int DEFAULT = 0;

    ConfDirective* conf = GetStats();
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("network");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->Arg0().c_str());
}

ConfDirective *Config::GetStatsDiskDevice()
{
    ConfDirective* conf = GetStats();
    if (!conf) {
        return NULL;
    }

    conf = conf->Get("disk");
    if (!conf || conf->m_args.size() == 0) {
        return NULL;
    }

    return conf;
}

bool Config::GetExporterEnabled()
{
    OVERWRITE_BY_ENV_BOOL("SRS_EXPORTER_ENABLED");

    static bool DEFAULT = false;

    ConfDirective* conf = m_root->Get("exporter");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

std::string Config::GetExporterListen()
{
    OVERWRITE_BY_ENV_STRING("SRS_EXPORTER_LISTEN");

    static std::string DEFAULT = "9972";

    ConfDirective* conf = m_root->Get("exporter");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("listen");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetExporterLabel()
{
    OVERWRITE_BY_ENV_STRING("SRS_EXPORTER_LABEL");

    static std::string DEFAULT = "";

    ConfDirective* conf = m_root->Get("exporter");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("label");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

std::string Config::GetExporterTag()
{
    OVERWRITE_BY_ENV_STRING("SRS_EXPORTER_TAG");

    static std::string DEFAULT = "";

    ConfDirective* conf = m_root->Get("exporter");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("tag");
    if (!conf) {
        return DEFAULT;
    }

    return conf->Arg0();
}

bool Config::GetServiceEnabled(ConfDirective *conf)
{
    OVERWRITE_BY_ENV_BOOL("srs.service_server.enabled");

    static bool DEFAULT = false;

    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("enabled");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return CONF_PERFER_FALSE(conf->Arg0());
}

bool Config::GetServiceEnabled()
{
    ConfDirective* conf = m_root->Get("service_server");
    return GetServiceEnabled(conf);

}

std::string Config::GetServiceListen()
{
    OVERWRITE_BY_ENV_STRING("srs.service_server.listen");

    static std::string DEFAULT = "8888";

    ConfDirective* conf = m_root->Get("service_server");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->Get("listen");
    if (!conf || conf->Arg0().empty()) {
        return DEFAULT;
    }

    return conf->Arg0();
}
