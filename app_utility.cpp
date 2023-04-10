#include "app_utility.h"
#include "protocol_json.h"
#include "st/st.h"
#include "utility.h"
#include "error.h"
#include "protocol_utility.h"
#include <sys/resource.h>
#include <netdb.h>
#include <cstring>
#include <csignal>
#include <sys/wait.h>
#include <climits>

//the longest time to wait for a process to quit
#define PROCESS_QUIT_TIMEOUT_MS 1000

LogLevel GetLogLevel(std::string level)
{
    if("verbose" == level){
        return LogLevelVerbose;
    } else if ("info" == level){
        return LogLevelInfo;
    } else if ("trace" == level){
        return LogLevelTrace;
    } else if ("warn" == level){
        return LogLevelWarn;
    } else if ("error" == level){
        return LogLevelError;
    } else {
        return LogLevelDisabled;
    }
}

LogLevel GetLogLevelV2(std::string level)
{
    if("verbose" == level){
        return LogLevelVerbose;
    } else if ("debug" == level){
        return LogLevelInfo;
    } else if ("trace" == level){
        return LogLevelTrace;
    } else if ("warn" == level){
        return LogLevelWarn;
    } else if ("error" == level){
        return LogLevelError;
    } else {
        return LogLevelDisabled;
    }
}

std::string PathBuildStream(std::string template_path, std::string vhost, std::string app, std::string stream)
{
    std::string path = template_path;

    //variable [vhost]
    path = StringReplace(path, "[vhost]", vhost);
    //variable [app]
    path = StringReplace(path, "[app]", app);
    //variable [stream]
    path = StringReplace(path, "[stream]", stream);

    return path;
}

std::string PathBuildTimestamp(std::string template_path)
{
    std::string path = template_path;

    //date and time substitude
    //clock time
    timeval tv;
    if(gettimeofday(&tv, nullptr) == -1){
        return path;
    }

    //to calendar time
    struct tm now;
    //each of these functions returns nullptr in case an error was detected.
    //TO DO
//    if (_srs_config->get_utc_time()) {
//        if (gmtime_r(&tv.tv_sec, &now) == NULL) {
//            return path;
//        }
//    } else {
        if (localtime_r(&tv.tv_sec, &now) == NULL) {
            return path;
        }
//    }

    // the buffer to format the date and time.
    char buf[64];

    // [2006], replace with current year.
    if (true) {
        snprintf(buf, sizeof(buf), "%04d", 1900 + now.tm_year);
        path = StringReplace(path, "[2006]", buf);
    }
    // [01], replace this const to current month.
    if (true) {
        snprintf(buf, sizeof(buf), "%02d", 1 + now.tm_mon);
        path = StringReplace(path, "[01]", buf);
    }
    // [02], replace this const to current date.
    if (true) {
        snprintf(buf, sizeof(buf), "%02d", now.tm_mday);
        path = StringReplace(path, "[02]", buf);
    }
    // [15], replace this const to current hour.
    if (true) {
        snprintf(buf, sizeof(buf), "%02d", now.tm_hour);
        path = StringReplace(path, "[15]", buf);
    }
    // [04], repleace this const to current minute.
    if (true) {
        snprintf(buf, sizeof(buf), "%02d", now.tm_min);
        path = StringReplace(path, "[04]", buf);
    }
    // [05], repleace this const to current second.
    if (true) {
        snprintf(buf, sizeof(buf), "%02d", now.tm_sec);
        path = StringReplace(path, "[05]", buf);
    }
    // [999], repleace this const to current millisecond.
    if (true) {
        snprintf(buf, sizeof(buf), "%03d", (int)(tv.tv_usec / 1000));
        path = StringReplace(path, "[999]", buf);
    }
    // [timestamp],replace this const to current UNIX timestamp in ms.
    if (true) {
        int64_t now_us = ((int64_t)tv.tv_sec) * 1000 * 1000 + (int64_t)tv.tv_usec;
        path = StringReplace(path, "[timestamp]", Int2Str(now_us / 1000));
    }

    return path;

}

error KillForced(int &pid)
{
    error err = SUCCESS;

    if(pid <= 0){
        return err;
    }

    //first, try kill by SIGTERM
    if(kill(pid, SIGTERM) < 0){
        return ERRORNEW(ERROR_SYSTEM_KILL, "kill");
    }

    //wait to quit
    trace("send SIGTERM to pid=%d", pid);
    for(int i = 0; i < PROCESS_QUIT_TIMEOUT_MS / 10; ++i){
        int status = 0;
        pid_t qpid = -1;
        if((qpid = waitpid(pid, &status, WNOHANG)) < 0){
            return ERRORNEW(ERROR_SYSTEM_KILL, "kill");
        }

        //0 is not quit yet
        if (qpid == 0){
            st_usleep(10 * 1000);
            continue;
        }

        //killed, set pid to -1
        trace("SIGTERM stop process pid=%d ok.", pid);
        pid = -1;
        return err;
    }

    //then, try kill by SIGKILL
    if(kill(pid, SIGKILL) < 0){
        return ERRORNEW(ERROR_SYSTEM_KILL, "kill");
    }

    //wait for the process to quit.
    // for example, ffmpeg will gracefully quit if signal is:
    //         1) SIGHUP     2) SIGINT     3) SIGQUIT
    // other signals, directly exit(123), for example:
    //        9) SIGKILL    15) SIGTERM
    int status = 0;
    // @remark when we use SIGKILL to kill process, it must be killed,
    //      so we always wait it to quit by infinite loop.
    while(waitpid(pid, &status, 0) < 0){
        st_usleep(10 * 1000);
        continue;
    }

    trace("SIGKILL stop process pid=%d ok.", pid);
    pid = -1;

    return err;
}

static Rusage system_rusage;

Rusage::Rusage()
{
    m_ok = false;
    m_sampleTime = 0;
    memset(&m_r, 0, sizeof(rusage));
}

Rusage *GetSystemRusage()
{
    return &system_rusage;
}

void UpdateSystemRusage()
{
    if(getrusage(RUSAGE_SELF, &system_rusage.m_r) < 0){
        warn("getrusage failed, ignore");
        return;
    }

    system_rusage.m_sampleTime = u2ms(UpdateSystemTime());

    system_rusage.m_ok = true;
}

static ProcSelfStat system_cpu_self_stat;
static ProcSystemStat system_cpu_system_stat;

ProcSelfStat::ProcSelfStat()
{
    m_ok = false;
    m_sampleTime = 0;
    m_percent = 0;
    memset(m_comm, 0, sizeof(m_comm));
    m_state = '0';
    m_ppid = 0;
    m_pgrp = 0;
    m_session = 0;
    m_ttyNr = 0;
    m_tpgid = 0;
    m_flags = 0;
    m_minflt = 0;
    m_cminflt = 0;
    m_majflt = 0;
    m_cmajflt = 0;
    m_utime = 0;
    m_stime = 0;
    m_cutime = 0;
    m_cstime = 0;
    m_priority = 0;
    m_nice = 0;
    m_numThreads = 0;
    m_itrealvalue = 0;
    m_starttime = 0;
    m_vsize = 0;
    m_rss = 0;
    m_rsslim = 0;
    m_startcode = 0;
    m_endcode = 0;
    m_startstack = 0;
    m_kstkesp = 0;
    m_kstkeip = 0;
    m_signal = 0;
    m_blocked = 0;
    m_sigignore = 0;
    m_sigcatch = 0;
    m_wchan = 0;
    m_nswap = 0;
    m_cnswap = 0;
    m_exitSignal = 0;
    m_processor = 0;
    m_rtPriority = 0;
    m_policy = 0;
    m_delayacctBlkioTicks = 0;
    m_guestTime = 0;
    m_cguestTime = 0;
}

ProcSystemStat::ProcSystemStat()
{
    m_ok = false;
    m_sampleTime = 0;
    m_percent = 0;
    m_totalDelta = 0;
    m_user = 0;
    m_nice = 0;
    m_sys = 0;
    m_idle = 0;
    m_iowait = 0;
    m_irq = 0;
    m_softirq = 0;
    m_steal = 0;
    m_guest = 0;
}

int64_t ProcSystemStat::Total()
{
    return m_user + m_nice + m_sys + m_idle + m_iowait + m_irq + m_softirq + m_steal + m_guest;
}

ProcSelfStat *GetSelfProcStat()
{
    return &system_cpu_self_stat;
}

ProcSystemStat *GetSystemProcStat()
{
    return &system_cpu_system_stat;
}

bool GetProcSystemStat(ProcSystemStat& r)
{
#ifndef SRS_OSX
    FILE* f = fopen("/proc/stat", "r");
    if (f == NULL) {
        warn("open system cpu stat failed, ignore");
        return false;
    }

    static char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        if (strncmp(buf, "cpu ", 4) != 0) {
            continue;
        }

        // @see: read_stat_cpu() from https://github.com/sysstat/sysstat/blob/master/rd_stats.c#L88
        // @remark, ignore the filed 10 cpu_guest_nice
        sscanf(buf + 5, "%llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
               &r.m_user,
               &r.m_nice,
               &r.m_sys,
               &r.m_idle,
               &r.m_iowait,
               &r.m_irq,
               &r.m_softirq,
               &r.m_steal,
               &r.m_guest);

        break;
    }

    fclose(f);
#endif

    r.m_ok = true;

    return true;
}

bool GetProcSelfStat(ProcSelfStat& r)
{
#ifndef SRS_OSX
    FILE* f = fopen("/proc/self/stat", "r");
    if (f == NULL) {
        warn("open self cpu stat failed, ignore");
        return false;
    }

    // Note that we must read less than the size of r.comm, such as %31s for r.comm is char[32].
    fscanf(f, "%d %31s %c %d %d %d %d "
           "%d %u %lu %lu %lu %lu "
           "%lu %lu %ld %ld %ld %ld "
           "%ld %ld %llu %lu %ld "
           "%lu %lu %lu %lu %lu "
           "%lu %lu %lu %lu %lu "
           "%lu %lu %lu %d %d "
           "%u %u %llu "
           "%lu %ld",
           &r.m_pid, r.m_comm, &r.m_state, &r.m_ppid, &r.m_pgrp, &r.m_session, &r.m_ttyNr,
           &r.m_tpgid, &r.m_flags, &r.m_minflt, &r.m_cminflt, &r.m_majflt, &r.m_cmajflt,
           &r.m_utime, &r.m_stime, &r.m_cutime, &r.m_cstime, &r.m_priority, &r.m_nice,
           &r.m_numThreads, &r.m_itrealvalue, &r.m_starttime, &r.m_vsize, &r.m_rss,
           &r.m_rsslim, &r.m_startcode, &r.m_endcode, &r.m_startstack, &r.m_kstkesp,
           &r.m_kstkeip, &r.m_signal, &r.m_blocked, &r.m_sigignore, &r.m_sigcatch,
           &r.m_wchan, &r.m_nswap, &r.m_cnswap, &r.m_exitSignal, &r.m_processor,
           &r.m_rtPriority, &r.m_policy, &r.m_delayacctBlkioTicks,
           &r.m_guestTime, &r.m_cguestTime);

    fclose(f);
#endif

    r.m_ok = true;

    return true;
}

void UpdateProcStat()
{
    static int user_hz = 0;
    if(user_hz <= 0){
        user_hz = (int)sysconf(_SC_CLK_TCK);
        info("USER_HZ=%d", user_hz);
        Assert(user_hz > 0);
    }

    //system cpu stat
    if(true){
        ProcSystemStat r;
        if(GetProcSystemStat(r)){
            return;
        }

        r.m_sampleTime = u2ms(UpdateSystemTime());

        //calc usage in percent
        ProcSystemStat& o = system_cpu_system_stat;

        // @see: http://blog.csdn.net/nineday/article/details/1928847
        // @see: http://stackoverflow.com/questions/16011677/calculating-cpu-usage-using-proc-files
        if(o.Total() > 0){
            r.m_totalDelta = r.Total() - o.Total();
        }
        if(r.m_totalDelta > 0){
            int64_t idle = r.m_idle - o.m_idle;
            r.m_percent = (float)(1 - idle / (double)r.m_totalDelta);
        }

        //update cache
        system_cpu_system_stat = r;
    }

    //self cpu stat
    if(true){
        ProcSelfStat r;
        if(!GetProcSelfStat(r)){
            return;
        }

        r.m_sampleTime = u2ms(UpdateSystemTime());

        //calc usage in percent
        ProcSelfStat& o = system_cpu_self_stat;

        // @see: http://stackoverflow.com/questions/16011677/calculating-cpu-usage-using-proc-files
        int64_t total = r.m_sampleTime - o.m_sampleTime;
        int64_t usage = (r.m_utime + r.m_stime) - (o.m_utime + o.m_stime);
        if (total > 0) {
            r.m_percent = (float)(usage * 1000 / (double)total / user_hz);
        }

        // upate cache.
        system_cpu_self_stat = r;
    }
}

DiskStat::DiskStat()
{
    m_ok = false;
    m_sampleTime = 0;
    m_inKBps = m_outKBps = 0;
    m_busy = 0;

    m_pgpgin = 0;
    m_pgpgout = 0;

    m_rdIos = m_rdMerges = 0;
    m_rdSectors = 0;
    m_rdTicks = 0;

    m_wrIos = m_wrMerges = 0;
    m_wrSectors = 0;
    m_wrTicks = m_nbCurrent = m_ticks = m_aveq = 0;
}

static DiskStat disk_stat;

DiskStat *GetDiskStat()
{
    return &disk_stat;
}

bool GetDiskVmstatStat(DiskStat& r)
{
#ifndef SRS_OSX
    FILE* f = fopen("/proc/vmstat", "r");
    if (f == NULL) {
        warn("open vmstat failed, ignore");
        return false;
    }

    r.m_sampleTime = u2ms(UpdateSystemTime());

    static char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        // @see: read_vmstat_paging() from https://github.com/sysstat/sysstat/blob/master/rd_stats.c#L495
        if (strncmp(buf, "pgpgin ", 7) == 0) {
            sscanf(buf + 7, "%lu\n", &r.m_pgpgin);
        } else if (strncmp(buf, "pgpgout ", 8) == 0) {
            sscanf(buf + 8, "%lu\n", &r.m_pgpgout);
        }
    }

    fclose(f);
#endif

    r.m_ok = true;

    return true;
}

bool GetDiskDiskstatsStat(DiskStat& r)
{
    r.m_ok = true;
    r.m_sampleTime = u2ms(UpdateSystemTime());

#ifndef SRS_OSX
    // if disabled, ignore all devices.
//    SrsConfDirective* conf = _srs_config->get_stats_disk_device();
//    if (conf == NULL) {
//        return true;
//    }

    FILE* f = fopen("/proc/diskstats", "r");
    if (f == NULL) {
        warn("open vmstat failed, ignore");
        return false;
    }

    static char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        unsigned int major = 0;
        unsigned int minor = 0;
        static char name[32];
        unsigned int rd_ios = 0;
        unsigned int rd_merges = 0;
        unsigned long long rd_sectors = 0;
        unsigned int rd_ticks = 0;
        unsigned int wr_ios = 0;
        unsigned int wr_merges = 0;
        unsigned long long wr_sectors = 0;
        unsigned int wr_ticks = 0;
        unsigned int nb_current = 0;
        unsigned int ticks = 0;
        unsigned int aveq = 0;
        memset(name, 0, sizeof(name));

        sscanf(buf, "%4d %4d %31s %u %u %llu %u %u %u %llu %u %u %u %u",
               &major,
               &minor,
               name,
               &rd_ios,
               &rd_merges,
               &rd_sectors,
               &rd_ticks,
               &wr_ios,
               &wr_merges,
               &wr_sectors,
               &wr_ticks,
               &nb_current,
               &ticks,
               &aveq);

//        for (int i = 0; i < (int)conf->args.size(); i++) {
//            string name_ok = conf->args.at(i);

//            if (strcmp(name_ok.c_str(), name) != 0) {
//                continue;
//            }

//            r.m_rdIos += rd_ios;
//            r.m_rdMerges += rd_merges;
//            r.m_rdSectors += rd_sectors;
//            r.m_rdTicks += rd_ticks;
//            r.m_wrIos += wr_ios;
//            r.m_wrMerges += wr_merges;
//            r.m_wrSectors += wr_sectors;
//            r.m_wrTicks += wr_ticks;
//            r.m_nbCurrent += nb_current;
//            r.m_ticks += ticks;
//            r.m_aveq += aveq;

//            break;
//        }
    }

    fclose(f);
#endif

    r.m_ok = true;

    return true;
}

void UpdateDiskStat()
{
    DiskStat r;
    if (!GetDiskVmstatStat(r)) {
        return;
    }
    if (!GetDiskDiskstatsStat(r)) {
        return;
    }
    if (!GetProcSystemStat(r.m_cpu)) {
        return;
    }

    DiskStat& o = disk_stat;
    if (!o.m_ok) {
        disk_stat = r;
        return;
    }

    // vmstat
    if (true) {
        int64_t duration_ms = r.m_sampleTime - o.m_sampleTime;

        if (o.m_pgpgin > 0 && r.m_pgpgin > o.m_pgpgin && duration_ms > 0) {
            // KBps = KB * 1000 / ms = KB/s
            r.m_inKBps = (int)((r.m_pgpgin - o.m_pgpgin) * 1000 / duration_ms);
        }

        if (o.m_pgpgout > 0 && r.m_pgpgout > o.m_pgpgout && duration_ms > 0) {
            // KBps = KB * 1000 / ms = KB/s
            r.m_outKBps = (int)((r.m_pgpgout - o.m_pgpgout) * 1000 / duration_ms);
        }
    }

    // diskstats
    if (r.m_cpu.m_ok && o.m_cpu.m_ok) {
        CpuInfo* cpuinfo = GetCpuinfo();
        r.m_cpu.m_totalDelta = r.m_cpu.Total() - o.m_cpu.Total();

        if (r.m_cpu.m_ok && r.m_cpu.m_totalDelta > 0
            && cpuinfo->m_ok && cpuinfo->m_nbProcessors > 0
            && o.m_ticks < r.m_ticks
            ) {
            // @see: write_ext_stat() from https://github.com/sysstat/sysstat/blob/master/iostat.c#L979
            // TODO: FIXME: the USER_HZ assert to 100, so the total_delta ticks *10 is ms.
            double delta_ms = r.m_cpu.m_totalDelta * 10 / cpuinfo->m_nbProcessors;
            unsigned int ticks = r.m_ticks - o.m_ticks;

            // busy in [0, 1], where 0.1532 means 15.32%
            r.m_busy = (float)(ticks / delta_ms);
        }
    }

    disk_stat = r;
}

MemInfo::MemInfo()
{
    m_ok = false;
    m_sampleTime = 0;

    m_percentRam = 0;
    m_percentSwap = 0;

    m_MemActive = 0;
    m_RealInUse = 0;
    m_NotInUse = 0;
    m_MemTotal = 0;
    m_MemFree = 0;
    m_Buffers = 0;
    m_Cached = 0;
    m_SwapTotal = 0;
    m_SwapFree = 0;
}

static MemInfo system_meminfo;

MemInfo *GetMeminfo()
{
    return &system_meminfo;
}

void UpdateMeminfo()
{
    MemInfo& r = system_meminfo;

    FILE* f = fopen("/proc/meminfo", "r");
    if(f == nullptr){
        warn("open meminfo failed, ignore");
        return;
    }

    static char buf[1024];
    while(fgets(buf, sizeof(buf), f)){
        // @see: read_meminfo() from https://github.com/sysstat/sysstat/blob/master/rd_stats.c#L227
        if (strncmp(buf, "MemTotal:", 9) == 0) {
            sscanf(buf + 9, "%lu", &r.m_MemTotal);
        } else if (strncmp(buf, "MemFree:", 8) == 0) {
            sscanf(buf + 8, "%lu", &r.m_MemFree);
        } else if (strncmp(buf, "Buffers:", 8) == 0) {
            sscanf(buf + 8, "%lu", &r.m_Buffers);
        } else if (strncmp(buf, "Cached:", 7) == 0) {
            sscanf(buf + 7, "%lu", &r.m_Cached);
        } else if (strncmp(buf, "SwapTotal:", 10) == 0) {
            sscanf(buf + 10, "%lu", &r.m_SwapTotal);
        } else if (strncmp(buf, "SwapFree:", 9) == 0) {
            sscanf(buf + 9, "%lu", &r.m_SwapFree);
        }
    }

    fclose(f);

    r.m_sampleTime = u2ms(UpdateSystemTime());
    r.m_MemActive = r.m_MemTotal - r.m_MemFree;
    r.m_RealInUse = r.m_MemActive - r.m_Buffers - r.m_Cached;
    r.m_NotInUse = r.m_MemTotal - r.m_RealInUse;

    if(r.m_MemTotal > 0){
        r.m_percentRam = (float)(r.m_RealInUse / (double)r.m_MemTotal);
    }
    if(r.m_SwapTotal > 0){
        r.m_percentSwap = (float)((r.m_SwapTotal - r.m_SwapFree) / (double)r.m_SwapTotal);
    }

    r.m_ok = true;
}

CpuInfo::CpuInfo()
{
    m_ok = false;

    m_nbProcessors = 0;
    m_nbProcessorsOnline = 0;
}

CpuInfo *GetCpuinfo()
{
    static CpuInfo* cpu = nullptr;
    if(cpu != nullptr){
        return cpu;
    }

    //initialize cpu info
    cpu = new CpuInfo();
    cpu->m_ok = true;
    cpu->m_nbProcessors = (int)sysconf(_SC_NPROCESSORS_CONF);
    cpu->m_nbProcessorsOnline = (int)sysconf(_SC_NPROCESSORS_ONLN);

    return cpu;
}

PlatformInfo::PlatformInfo()
{
    m_ok = false;

    m_startupTime = 0;

    m_osUptime = 0;
    m_osIldeTime = 0;

    m_loadOneMinutes = 0;
    m_loadFiveMinutes = 0;
    m_loadFifteenMinutes = 0;
}

static PlatformInfo system_platform_info;

PlatformInfo *GetPlatformInfo()
{
    return &system_platform_info;
}

void UpdatePlatformInfo()
{
    PlatformInfo& r = system_platform_info;

    r.m_startupTime = u2ms(GetSystemStartupTime());

    if(true){
        FILE* f = fopen("/proc/uptime", "r");
        if(f == nullptr){
            warn("open uptime failed, ignore");
            return;
        }

        fscanf(f, "%lf %lf\n", &r.m_osUptime, &r.m_osIldeTime);

        fclose(f);
    }

    if(true){
        FILE* f = fopen("/proc/loadavg", "r");
        if(f == nullptr){
            warn("open loadavg failed, ignore");
            return;
        }

        // @see: read_loadavg() from https://github.com/sysstat/sysstat/blob/master/rd_stats.c#L402
        // @remark, we use our algorithm, not sysstat.
        fscanf(f, "%lf %lf %lf\n",
               &r.m_loadOneMinutes,
               &r.m_loadFiveMinutes,
               &r.m_loadFifteenMinutes);

        fclose(f);
    }

    r.m_ok = true;
}

SnmpUdpStat::SnmpUdpStat()
{
    m_ok = false;

    m_inDatagrams = 0;
    m_noPorts = 0;
    m_inErrors = 0;
    m_outDatagrams = 0;
    m_rcvBufErrors = 0;
    m_sndBufErrors = 0;
    m_inCsumErrors = 0;

    m_rcvBufErrorsDelta = 0;
    m_sndBufErrorsDelta = 0;
}

SnmpUdpStat::~SnmpUdpStat()
{

}

static SnmpUdpStat snmp_udp_stat;

bool GetUdpSnmpStatistic(SnmpUdpStat& r)
{
#ifndef SRS_OSX
    if (true) {
        FILE* f = fopen("/proc/net/snmp", "r");
        if (f == NULL) {
            warn("open proc network snmp failed, ignore");
            return false;
        }

        // ignore title.
        static char buf[1024];
        fgets(buf, sizeof(buf), f);

        while (fgets(buf, sizeof(buf), f)) {
            // udp stat title
            if (strncmp(buf, "Udp: ", 5) == 0) {
                // read tcp stat data
                if (!fgets(buf, sizeof(buf), f)) {
                    break;
                }
                // parse tcp stat data
                if (strncmp(buf, "Udp: ", 5) == 0) {
                    sscanf(buf + 5, "%llu %llu %llu %llu %llu %llu %llu\n",
                        &r.m_inDatagrams,
                        &r.m_noPorts,
                        &r.m_inErrors,
                        &r.m_outDatagrams,
                        &r.m_rcvBufErrors,
                        &r.m_sndBufErrors,
                        &r.m_inCsumErrors);
                }
            }
        }
        fclose(f);
    }
#endif
    r.m_ok = true;

    return true;
}

SnmpUdpStat *GetUdpSnmpStat()
{
    return &snmp_udp_stat;
}

void UpdateUdpSnmpStatistic()
{
    SnmpUdpStat r;
    if(!GetUdpSnmpStatistic(r)){
        return;
    }

    SnmpUdpStat& o = snmp_udp_stat;
    if(o.m_rcvBufErrors > 0){
        r.m_rcvBufErrorsDelta = int(r.m_rcvBufErrors - o.m_rcvBufErrors);
    }

    if(o.m_sndBufErrors > 0){
        r.m_sndBufErrorsDelta = int(r.m_sndBufErrors - o.m_sndBufErrors);
    }

    snmp_udp_stat = r;
}

NetworkDevices::NetworkDevices()
{
    m_ok = false;

    memset(m_name, 0, sizeof(m_name));
    m_sampleTime = 0;

    m_rbytes = 0;
    m_rpackets = 0;
    m_rerrs = 0;
    m_rdrop = 0;
    m_rfifo = 0;
    m_rframe = 0;
    m_rcompressed = 0;
    m_rmulticast = 0;

    m_sbytes = 0;
    m_spackets = 0;
    m_serrs = 0;
    m_sdrop = 0;
    m_sfifo = 0;
    m_scolls = 0;
    m_scarrier = 0;
    m_scompressed = 0;
}

#define MAX_NETWORK_DEVICES_COUNT 16
static NetworkDevices system_network_devices[MAX_NETWORK_DEVICES_COUNT];
static int nb_system_network_devices = -1;

NetworkDevices *GetNetworkDevices()
{
    return system_network_devices;
}

int GetNetworkDevicesCount()
{
    return nb_system_network_devices;
}

void UpdateNetworkDevices()
{
    if (true) {
        FILE* f = fopen("/proc/net/dev", "r");
        if (f == NULL) {
            warn("open proc network devices failed, ignore");
            return;
        }

        // ignore title.
        static char buf[1024];
        fgets(buf, sizeof(buf), f);
        fgets(buf, sizeof(buf), f);

        for (int i = 0; i < MAX_NETWORK_DEVICES_COUNT; i++) {
            if (!fgets(buf, sizeof(buf), f)) {
                break;
            }

            NetworkDevices& r = system_network_devices[i];

            // @see: read_net_dev() from https://github.com/sysstat/sysstat/blob/master/rd_stats.c#L786
            // @remark, we use our algorithm, not sysstat.
            char fname[7];
            sscanf(buf, "%6[^:]:%llu %lu %lu %lu %lu %lu %lu %lu %llu %lu %lu %lu %lu %lu %lu %lu\n",
                   fname, &r.m_rbytes, &r.m_rpackets, &r.m_rerrs, &r.m_rdrop, &r.m_rfifo, &r.m_rframe, &r.m_rcompressed, &r.m_rmulticast,
                   &r.m_sbytes, &r.m_spackets, &r.m_serrs, &r.m_sdrop, &r.m_sfifo, &r.m_scolls, &r.m_scarrier, &r.m_scompressed);

            sscanf(fname, "%s", r.m_name);
            nb_system_network_devices = i + 1;
            info("scan network device ifname=%s, total=%d", r.m_name, nb_system_network_devices);

            r.m_sampleTime = u2ms(UpdateSystemTime());
            r.m_ok = true;
        }

        fclose(f);
    }
}

NetworkRtmpServer::NetworkRtmpServer()
{
    m_ok = false;
    m_sampletime = m_rbytes = m_sbytes = 0;
    m_nbConnSys = m_nbConnSrs = 0;
    m_nbConnSysEt = m_nbConnSysTw = 0;
    m_nbConnSysUdp = 0;
    m_rkbps = m_skbps = 0;
    m_rkbps30s = m_skbps30s = 0;
    m_rkbps5m = m_skbps5m = 0;
}

static NetworkRtmpServer network_rtmp_server;

NetworkRtmpServer *GetNetworkRtmpServer()
{
    return &network_rtmp_server;
}

// @see: http://stackoverflow.com/questions/5992211/list-of-possible-internal-socket-statuses-from-proc
enum {
    SYS_TCP_ESTABLISHED = 0x01,
    SYS_TCP_SYN_SENT,       // 0x02
    SYS_TCP_SYN_RECV,       // 0x03
    SYS_TCP_FIN_WAIT1,      // 0x04
    SYS_TCP_FIN_WAIT2,      // 0x05
    SYS_TCP_TIME_WAIT,      // 0x06
    SYS_TCP_CLOSE,          // 0x07
    SYS_TCP_CLOSE_WAIT,     // 0x08
    SYS_TCP_LAST_ACK,       // 0x09
    SYS_TCP_LISTEN,         // 0x0A
    SYS_TCP_CLOSING,        // 0x0B /* Now a valid state */

    SYS_TCP_MAX_STATES      // 0x0C /* Leave at the end! */
};

void UpdateRtmpServer(int nb_conn, Kbps *kbps)
{
    NetworkRtmpServer& r = network_rtmp_server;

    int nb_socks = 0;
    int nb_tcp4_hashed = 0;
    int nb_tcp_orphans = 0;
    int nb_tcp_tws = 0;
    int nb_tcp_total = 0;
    int nb_tcp_mem = 0;
    int nb_udp4 = 0;

    if (true) {
        FILE* f = fopen("/proc/net/sockstat", "r");
        if (f == NULL) {
            warn("open proc network sockstat failed, ignore");
            return;
        }

        // ignore title.
        static char buf[1024];
        fgets(buf, sizeof(buf), f);

        while (fgets(buf, sizeof(buf), f)) {
            // @see: et_sockstat_line() from https://github.com/shemminger/iproute2/blob/master/misc/ss.c
            if (strncmp(buf, "sockets: used ", 14) == 0) {
                sscanf(buf + 14, "%d\n", &nb_socks);
            } else if (strncmp(buf, "TCP: ", 5) == 0) {
                sscanf(buf + 5, "%*s %d %*s %d %*s %d %*s %d %*s %d\n",
                       &nb_tcp4_hashed,
                       &nb_tcp_orphans,
                       &nb_tcp_tws,
                       &nb_tcp_total,
                       &nb_tcp_mem);
            } else if (strncmp(buf, "UDP: ", 5) == 0) {
                sscanf(buf + 5, "%*s %d\n", &nb_udp4);
            }
        }

        fclose(f);
    }

    int nb_tcp_estab = 0;


    if (true) {
        FILE* f = fopen("/proc/net/snmp", "r");
        if (f == NULL) {
            warn("open proc network snmp failed, ignore");
            return;
        }

        // ignore title.
        static char buf[1024];
        fgets(buf, sizeof(buf), f);

        // @see: https://github.com/shemminger/iproute2/blob/master/misc/ss.c
        while (fgets(buf, sizeof(buf), f)) {
            // @see: get_snmp_int("Tcp:", "CurrEstab", &sn.tcp_estab)
            // tcp stat title
            if (strncmp(buf, "Tcp: ", 5) == 0) {
                // read tcp stat data
                if (!fgets(buf, sizeof(buf), f)) {
                    break;
                }
                // parse tcp stat data
                if (strncmp(buf, "Tcp: ", 5) == 0) {
                    sscanf(buf + 5, "%*d %*d %*d %*d %*d %*d %*d %*d %d\n", &nb_tcp_estab);
                }
            }
        }

        fclose(f);
    }

    // @see: https://github.com/shemminger/iproute2/blob/master/misc/ss.c
    // TODO: FIXME: ignore the slabstat, @see: get_slabstat()
    if (true) {
        // @see: print_summary()
        r.m_nbConnSys = nb_tcp_total + nb_tcp_tws;
        r.m_nbConnSysEt = nb_tcp_estab;
        r.m_nbConnSysTw = nb_tcp_tws;
        r.m_nbConnSysUdp = nb_udp4;
    }

    if (true) {
        r.m_ok = true;

        r.m_nbConnSrs = nb_conn;
        r.m_sampletime = u2ms(UpdateSystemTime());

        //TO DO
//        r.m_rbytes = kbps->GetRecvBytes();
//        r.rkbps = kbps->get_recv_kbps();
//        r.rkbps_30s = kbps->get_recv_kbps_30s();
//        r.rkbps_5m = kbps->get_recv_kbps_5m();

//        r.sbytes = kbps->get_send_bytes();
//        r.skbps = kbps->get_send_kbps();
//        r.skbps_30s = kbps->get_send_kbps_30s();
//        r.skbps_5m = kbps->get_send_kbps_5m();
    }
}

std::string GetLocalIp(int fd)
{
    // discovery client information
    sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    if (getsockname(fd, (sockaddr*)&addr, &addrlen) == -1) {
        return "";
    }

    char saddr[64];
    char* h = (char*)saddr;
    socklen_t nbh = (socklen_t)sizeof(saddr);
    const int r0 = getnameinfo((const sockaddr*)&addr, addrlen, h, nbh,NULL, 0, NI_NUMERICHOST);
    if(r0) {
        return "";
    }

    return std::string(saddr);
}

int GetLocalPort(int fd)
{
    // discovery client information
    sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    if (getsockname(fd, (sockaddr*)&addr, &addrlen) == -1) {
        return 0;
    }

    int port = 0;
    switch(addr.ss_family) {
        case AF_INET:
            port = ntohs(((sockaddr_in*)&addr)->sin_port);
         break;
        case AF_INET6:
            port = ntohs(((sockaddr_in6*)&addr)->sin6_port);
         break;
    }

    return port;
}

std::string GetPeerIp(int fd)
{
    // discovery client information
    sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    if (getpeername(fd, (sockaddr*)&addr, &addrlen) == -1) {
        return "";
    }

    char saddr[64];
    char* h = (char*)saddr;
    socklen_t nbh = (socklen_t)sizeof(saddr);
    const int r0 = getnameinfo((const sockaddr*)&addr, addrlen, h, nbh, NULL, 0, NI_NUMERICHOST);
    if(r0) {
        return "";
    }

    return std::string(saddr);
}

int GetPeerPort(int fd)
{
    // discovery client information
    sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    if (getpeername(fd, (sockaddr*)&addr, &addrlen) == -1) {
        return 0;
    }

    int port = 0;
    switch(addr.ss_family) {
        case AF_INET:
            port = ntohs(((sockaddr_in*)&addr)->sin_port);
         break;
        case AF_INET6:
            port = ntohs(((sockaddr_in6*)&addr)->sin6_port);
         break;
    }

    return port;
}

bool IsBoolean(std::string str)
{
    return str == "true" || str == "false";
}

void ApiDumpSummaries(JsonObject *obj)
{
    Rusage* r = GetSystemRusage();
    ProcSelfStat* u = GetSelfProcStat();
    ProcSystemStat* s = GetSystemProcStat();
    CpuInfo* c = GetCpuinfo();
    MemInfo* m = GetMeminfo();
    PlatformInfo* p = GetPlatformInfo();
    NetworkDevices* n = GetNetworkDevices();
    NetworkRtmpServer* nrs = GetNetworkRtmpServer();
    DiskStat* d = GetDiskStat();

    float self_mem_percent = 0;
    if (m->m_MemTotal > 0) {
        self_mem_percent = (float)(r->m_r.ru_maxrss / (double)m->m_MemTotal);
    }

    int64_t now = u2ms(UpdateSystemTime());
    double srs_uptime = (now - p->m_startupTime) / 100 / 10.0;

    int64_t n_sample_time = 0;
    int64_t nr_bytes = 0;
    int64_t ns_bytes = 0;
    int64_t nri_bytes = 0;
    int64_t nsi_bytes = 0;
    int nb_n = GetNetworkDevicesCount();
    for (int i = 0; i < nb_n; i++) {
        NetworkDevices& o = n[i];

        // ignore the lo interface.
        std::string inter = o.m_name;
        if (!o.m_ok) {
            continue;
        }

        // update the sample time.
        n_sample_time = o.m_sampleTime;

        // stat the intranet bytes.
        if (inter == "lo" || !NetDeviceIsInternet(inter)) {
            nri_bytes += o.m_rbytes;
            nsi_bytes += o.m_sbytes;
            continue;
        }

        nr_bytes += o.m_rbytes;
        ns_bytes += o.m_sbytes;
    }

    // all data is ok?
    bool ok = (r->m_ok && u->m_ok && s->m_ok && c->m_ok
               && d->m_ok && m->m_ok && p->m_ok && nrs->m_ok);

    JsonObject* data = JsonAny::Object();
    obj->Set("data", data);

    data->Set("ok", JsonAny::Boolean(ok));
    data->Set("now_ms", JsonAny::Integer(now));

    // self
    JsonObject* self = JsonAny::Object();
    data->Set("self", self);

    self->Set("version", JsonAny::Str(RTMP_SIG_VERSION));
    self->Set("pid", JsonAny::Integer(getpid()));
    self->Set("ppid", JsonAny::Integer(u->m_ppid));
    //TO DO
//    self->Set("argv", JsonAny::Str(config->argv().c_str()));
//    self->Set("cwd", JsonAny::Str(config->cwd().c_str()));
    self->Set("mem_kbyte", JsonAny::Integer(r->m_r.ru_maxrss));
    self->Set("mem_percent", JsonAny::Number(self_mem_percent));
    self->Set("cpu_percent", JsonAny::Number(u->m_percent));
    self->Set("srs_uptime", JsonAny::Integer(srs_uptime));

    // system
    JsonObject* sys = JsonAny::Object();
    data->Set("system", sys);

    sys->Set("cpu_percent", JsonAny::Number(s->m_percent));
    sys->Set("disk_read_KBps", JsonAny::Integer(d->m_inKBps));
    sys->Set("disk_write_KBps", JsonAny::Integer(d->m_outKBps));
    sys->Set("disk_busy_percent", JsonAny::Number(d->m_busy));
    sys->Set("mem_ram_kbyte", JsonAny::Integer(m->m_MemTotal));
    sys->Set("mem_ram_percent", JsonAny::Number(m->m_percentRam));
    sys->Set("mem_swap_kbyte", JsonAny::Integer(m->m_SwapTotal));
    sys->Set("mem_swap_percent", JsonAny::Number(m->m_percentSwap));
    sys->Set("cpus", JsonAny::Integer(c->m_nbProcessors));
    sys->Set("cpus_online", JsonAny::Integer(c->m_nbProcessorsOnline));
    sys->Set("uptime", JsonAny::Number(p->m_osUptime));
    sys->Set("ilde_time", JsonAny::Number(p->m_osIldeTime));
    sys->Set("load_1m", JsonAny::Number(p->m_loadOneMinutes));
    sys->Set("load_5m", JsonAny::Number(p->m_loadFiveMinutes));
    sys->Set("load_15m", JsonAny::Number(p->m_loadFifteenMinutes));
    // system network bytes stat.
    sys->Set("net_sample_time", JsonAny::Integer(n_sample_time));
    // internet public address network device bytes.
    sys->Set("net_recv_bytes", JsonAny::Integer(nr_bytes));
    sys->Set("net_send_bytes", JsonAny::Integer(ns_bytes));
    // intranet private address network device bytes.
    sys->Set("net_recvi_bytes", JsonAny::Integer(nri_bytes));
    sys->Set("net_sendi_bytes", JsonAny::Integer(nsi_bytes));
    // srs network bytes stat.
    sys->Set("srs_sample_time", JsonAny::Integer(nrs->m_sampletime));
    sys->Set("srs_recv_bytes", JsonAny::Integer(nrs->m_rbytes));
    sys->Set("srs_send_bytes", JsonAny::Integer(nrs->m_sbytes));
    sys->Set("conn_sys", JsonAny::Integer(nrs->m_nbConnSys));
    sys->Set("conn_sys_et", JsonAny::Integer(nrs->m_nbConnSysEt));
    sys->Set("conn_sys_tw", JsonAny::Integer(nrs->m_nbConnSysTw));
    sys->Set("conn_sys_udp", JsonAny::Integer(nrs->m_nbConnSysUdp));
    sys->Set("conn_srs", JsonAny::Integer(nrs->m_nbConnSrs));
}

std::string StringDumpsHex(const std::string &str)
{
    return StringDumpsHex(str.c_str(), str.size());
}

std::string StringDumpsHex(const char *str, int length)
{
    return StringDumpsHex(str, length, INT_MAX);
}

std::string StringDumpsHex(const char *str, int length, int limit)
{
    return StringDumpsHex(str, length, limit, ' ', 128, '\n');
}

std::string StringDumpsHex(const char *str, int length, int limit, char seperator, int line_limit, char newline)
{
    // 1 byte trailing '\0'.
    const int LIMIT = 1024*16 + 1;
    static char buf[LIMIT];

    int len = 0;
    for (int i = 0; i < length && i < limit && len < LIMIT; ++i) {
        int nb = snprintf(buf + len, LIMIT - len, "%02x", (uint8_t)str[i]);
        if (nb <= 0 || nb >= LIMIT - len) {
            break;
        }
        len += nb;

        // Only append seperator and newline when not last byte.
        if (i < length - 1 && i < limit - 1 && len < LIMIT) {
            if (seperator) {
                buf[len++] = seperator;
            }

            if (newline && line_limit && i > 0 && ((i + 1) % line_limit) == 0) {
                buf[len++] = newline;
            }
        }
    }

    // Empty string.
    if (len <= 0) {
        return "";
    }

    // If overflow, cut the trailing newline.
    if (newline && len >= LIMIT - 2 && buf[len - 1] == newline) {
        len--;
    }

    // If overflow, cut the trailing seperator.
    if (seperator && len >= LIMIT - 3 && buf[len - 1] == seperator) {
        len--;
    }

    return std::string(buf, len);
}

std::string Getenv(const std::string &key)
{
    std::string ekey = key;
    if (StringStartsWith(key, "$")) {
        ekey = key.substr(1);
    }

    if (ekey.empty()) {
        return "";
    }

    std::string::iterator it;
    for (it = ekey.begin(); it != ekey.end(); ++it) {
        if (*it >= 'a' && *it <= 'z') {
            *it += ('A' - 'a');
        } else if (*it == '.') {
            *it = '_';
        }
    }

    char* value = ::getenv(ekey.c_str());
    if (value) {
        return value;
    }

    return "";
}
