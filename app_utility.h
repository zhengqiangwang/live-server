#ifndef APP_UTILITY_H
#define APP_UTILITY_H

#include "log.h"
#include <bits/types/struct_rusage.h>
#include <string>

class Kbps;
class Buffer;
class JsonObject;

//convert level in string to log level in int
extern LogLevel GetLogLevel(std::string level);
extern LogLevel GetLogLevelV2(std::string level);

//build the path according to vhost/app/stream, where replace variables:
//  [vhost], the vhost of stream
//  [app], the app of stream
//  [stream], the stream name of stream
//@return the replaced path
extern std::string PathBuildStream(std::string template_path, std::string vhost, std::string app, std::string stream);

// Build the path according to timestamp, where replace variables:
//       [2006], replace this const to current year.
//       [01], replace this const to current month.
//       [02], replace this const to current date.
//       [15], replace this const to current hour.
//       [04], repleace this const to current minute.
//       [05], repleace this const to current second.
//       [999], repleace this const to current millisecond.
//       [timestamp],replace this const to current UNIX timestamp in ms.
// @return the replaced path.
extern std::string PathBuildTimestamp(std::string template_path);

// Kill the pid by SIGINT, then wait to quit,
// Kill the pid by SIGKILL again when exceed the timeout.
// @param pid the pid to kill. ignore for -1. set to -1 when killed.
// @return an int error code.
extern error KillForced(int& pid);

//current process resouce usage
//@see: man getrusage
class Rusage
{
public:
    //whether the data is ok
    bool m_ok;
    //the time in ms when sample
    int64_t m_sampleTime;

public:
    rusage m_r;

public:
    Rusage();
};

//get system rusage, use cache to avoid performance problem
extern Rusage* GetSystemRusage();
//the daemon st-thread will update it
extern void UpdateSystemRusage();

//to stat the process info
//@see: man 5 proc, /proc/[pid]/stat
class ProcSelfStat
{
public:
    //whether the data is ok
    bool m_ok;
    //the time in ms when sample
    int64_t m_sampleTime;
    //the percent of usage. 0.153 is 15.3%
    float m_percent;

    //data of /proc/[pid]/stat
public:
    //pid %d the process ID
    int m_pid;
    //comm %s the filename of the executable, in parentheses. this is visible whether or not the executable is swapped out
    char m_comm[32];
    //state %c  one character from the string "RSDZTW" where R is running, S is sleeping in an interruptible wait, D
    // is waiting in uninterruptible disk sleep, Z is zombie, T is traced or stopped (on a signal), and W is
    // paging.
    unsigned char m_state;
    //ppid %d  the pid of the parent.
    int m_ppid;
    // pgrp %d     The process group ID of the process.
    int m_pgrp;
    // session %d  The session ID of the process.
    int m_session;
    // tty_nr %d   The controlling terminal of the process.  (The minor device number is contained in the combination  of
    //             bits 31 to 20 and 7 to 0; the major device number is in bits 15 t0 8.)
    int m_ttyNr;
    // tpgid %d    The ID of the foreground process group of the controlling terminal of the process.
    int m_tpgid;
    // flags %u (%lu before Linux 2.6.22)
    //             The  kernel  flags  word  of  the process.  For bit meanings, see the PF_* defines in <linux/sched.h>.
    //             Details depend on the kernel version.
    unsigned int m_flags;
    // minflt %lu  The number of minor faults the process has made which have not required loading  a  memory  page  from
    //             disk.
    unsigned long m_minflt;
    // cminflt %lu The number of minor faults that the process's waited-for children have made.
    unsigned long m_cminflt;
    // majflt %lu  The number of major faults the process has made which have required loading a memory page from disk.
    unsigned long m_majflt;
    // cmajflt %lu The number of major faults that the process's waited-for children have made.
    unsigned long m_cmajflt;
    // utime %lu   Amount  of  time that this process has been scheduled in user mode, measured in clock ticks (divide by
    //             sysconf(_SC_CLK_TCK).  This includes guest time, guest_time (time spent running  a  virtual  CPU,  see
    //             below),  so  that  applications  that are not aware of the guest time field do not lose that time from
    //             their calculations.
    unsigned long m_utime;
    // stime %lu   Amount of time that this process has been scheduled in kernel mode, measured in clock ticks (divide by
    //             sysconf(_SC_CLK_TCK).
    unsigned long m_stime;
    // cutime %ld  Amount  of  time that this process's waited-for children have been scheduled in user mode, measured in
    //             clock ticks (divide  by  sysconf(_SC_CLK_TCK).   (See  also  times(2).)   This  includes  guest  time,
    //             cguest_time (time spent running a virtual CPU, see below).
    long m_cutime;
    // cstime %ld  Amount of time that this process's waited-for children have been scheduled in kernel mode, measured in
    //             clock ticks (divide by sysconf(_SC_CLK_TCK).
    long m_cstime;
    // priority %ld
    //          (Explanation for Linux 2.6) For processes running a real-time scheduling  policy  (policy  below;  see
    //          sched_setscheduler(2)),  this  is the negated scheduling priority, minus one; that is, a number in the
    //          range -2 to -100, corresponding to real-time priorities 1 to 99.  For processes running under  a  non-
    //          real-time scheduling policy, this is the raw nice value (setpriority(2)) as represented in the kernel.
    //          The kernel stores nice values as numbers in the range 0 (high) to 39 (low), corresponding to the user-
    //          visible nice range of -20 to 19.
    //
    //          Before Linux 2.6, this was a scaled value based on the scheduler weighting given to this process.
    long m_priority;
    // nice %ld    The nice value (see setpriority(2)), a value in the range 19 (low priority) to -20 (high priority).
    long m_nice;
    // num_threads %ld
    //          Number  of threads in this process (since Linux 2.6).  Before kernel 2.6, this field was hard coded to
    //          0 as a placeholder for an earlier removed field.
    long m_numThreads;
    // itrealvalue %ld
    //          The time in jiffies before the next SIGALRM is sent to the process due to an  interval  timer.   Since
    //          kernel 2.6.17, this field is no longer maintained, and is hard coded as 0.
    long m_itrealvalue;
    // starttime %llu (was %lu before Linux 2.6)
    //          The time in jiffies the process started after system boot.
    long long m_starttime;
    // vsize %lu   Virtual memory size in bytes.
    unsigned long m_vsize;
    // rss %ld     Resident Set Size: number of pages the process has in real memory.  This is just the pages which count
    //             towards text, data, or stack space.  This does not include pages which have not been demand-loaded in,
    //             or which are swapped out.
    long m_rss;
    // rsslim %lu  Current  soft limit in bytes on the rss of the process; see the description of RLIMIT_RSS in getprior-
    //             ity(2).
    unsigned long m_rsslim;
    // startcode %lu
    //             The address above which program text can run.
    unsigned long m_startcode;
    // endcode %lu The address below which program text can run.
    unsigned long m_endcode;
    // startstack %lu
    //             The address of the start (i.e., bottom) of the stack.
    unsigned long m_startstack;
    // kstkesp %lu The current value of ESP (stack pointer), as found in the kernel stack page for the process.
    unsigned long m_kstkesp;
    // kstkeip %lu The current EIP (instruction pointer).
    unsigned long m_kstkeip;
    // signal %lu  The bitmap of pending signals, displayed as a decimal number.  Obsolete, because it does  not  provide
    //             information on real-time signals; use /proc/[pid]/status instead.
    unsigned long m_signal;
    // blocked %lu The  bitmap  of blocked signals, displayed as a decimal number.  Obsolete, because it does not provide
    //             information on real-time signals; use /proc/[pid]/status instead.
    unsigned long m_blocked;
    // sigignore %lu
    //             The bitmap of ignored signals, displayed as a decimal number.  Obsolete, because it does  not  provide
    //             information on real-time signals; use /proc/[pid]/status instead.
    unsigned long m_sigignore;
    // sigcatch %lu
    //             The  bitmap  of  caught signals, displayed as a decimal number.  Obsolete, because it does not provide
    //             information on real-time signals; use /proc/[pid]/status instead.
    unsigned long m_sigcatch;
    // wchan %lu   This is the "channel" in which the process is waiting.  It is the address of a system call, and can be
    //             looked  up in a namelist if you need a textual name.  (If you have an up-to-date /etc/psdatabase, then
    //             try ps -l to see the WCHAN field in action.)
    unsigned long m_wchan;
    // nswap %lu   Number of pages swapped (not maintained).
    unsigned long m_nswap;
    // cnswap %lu  Cumulative nswap for child processes (not maintained).
    unsigned long m_cnswap;
    // exit_signal %d (since Linux 2.1.22)
    //             Signal to be sent to parent when we die.
    int m_exitSignal;
    // processor %d (since Linux 2.2.8)
    //             CPU number last executed on.
    int m_processor;
    // rt_priority %u (since Linux 2.5.19; was %lu before Linux 2.6.22)
    //             Real-time scheduling priority, a number in the range 1 to 99 for processes scheduled under a real-time
    //             policy, or 0, for non-real-time processes (see sched_setscheduler(2)).
    unsigned int m_rtPriority;
    // policy %u (since Linux 2.5.19; was %lu before Linux 2.6.22)
    //             Scheduling policy (see sched_setscheduler(2)).  Decode using the SCHED_* constants in linux/sched.h.
    unsigned int m_policy;
    // delayacct_blkio_ticks %llu (since Linux 2.6.18)
    //             Aggregated block I/O delays, measured in clock ticks (centiseconds).
    unsigned long long m_delayacctBlkioTicks;
    // guest_time %lu (since Linux 2.6.24)
    //             Guest time of the process (time spent running a virtual CPU for a guest operating system), measured in
    //             clock ticks (divide by sysconf(_SC_CLK_TCK).
    unsigned long m_guestTime;
    // cguest_time %ld (since Linux 2.6.24)
    //             Guest time of the process's children, measured in clock ticks (divide by sysconf(_SC_CLK_TCK).
    long m_cguestTime;

public:
    ProcSelfStat();
};

// To stat the cpu time.
// @see: man 5 proc, /proc/stat
// about the cpu time, @see: http://stackoverflow.com/questions/16011677/calculating-cpu-usage-using-proc-files
// for example, for ossrs.net, a single cpu machine:
//       [winlin@SRS ~]$ cat /proc/uptime && cat /proc/stat
//           5275153.01 4699624.99
//           cpu  43506750 973 8545744 466133337 4149365 190852 804666 0 0
// Where the uptime is 5275153.01s
// generally, USER_HZ sysconf(_SC_CLK_TCK)=100, which means the unit of /proc/stat is "1/100ths seconds"
//       that is, USER_HZ=1/100 seconds
// cpu total = 43506750+973+8545744+466133337+4149365+190852+804666+0+0 (USER_HZ)
//           = 523331687 (USER_HZ)
//           = 523331687 * 1/100 (seconds)
//           = 5233316.87 seconds
// The cpu total seconds almost the uptime, the delta is more precise.
//
// we run the command about 26minutes:
//       [winlin@SRS ~]$ cat /proc/uptime && cat /proc/stat
//           5276739.83 4701090.76
//           cpu  43514105 973 8548948 466278556 4150480 190899 804937 0 0
// Where the uptime is 5276739.83s
// cpu total = 43514105+973+8548948+466278556+4150480+190899+804937+0+0 (USER_HZ)
//           = 523488898 (USER_HZ)
//           = 523488898 * 1/100 (seconds)
//           = 5234888.98 seconds
// where:
//       uptime delta = 1586.82s
//       cpu total delta = 1572.11s
// The deviation is more smaller.
class ProcSystemStat
{
public:
    // Whether the data is ok.
    bool m_ok;
    // The time in ms when sample.
    int64_t m_sampleTime;
    // The percent of usage. 0.153 is 15.3%.
    // The percent is in [0, 1], where 1 is 100%.
    // for multiple core cpu, max also is 100%.
    float m_percent;
    // The total cpu time units
    // @remark, zero for the previous total() is zero.
    //          the usaged_cpu_delta = total_delta * percent
    //          previous cpu total = this->total() - total_delta
    int64_t m_totalDelta;

    // data of /proc/stat
public:
    // The amount of time, measured in units  of  USER_HZ
    // (1/100ths  of  a  second  on  most  architectures,  use
    // sysconf(_SC_CLK_TCK)  to  obtain  the  right value)
    //
    // The system spent in user mode,
    unsigned long long m_user;
    // user mode with low priority (nice),
    unsigned long long m_nice;
    // system mode,
    unsigned long long m_sys;
    // and the idle task, respectively.
    unsigned long long m_idle;

    // In  Linux 2.6 this line includes three additional columns:
    //
    // iowait - time waiting for I/O to complete (since 2.5.41);
    unsigned long long m_iowait;
    // irq - time servicing interrupts (since 2.6.0-test4);
    unsigned long long m_irq;
    // softirq  -  time  servicing  softirqs  (since 2.6.0-test4).
    unsigned long long m_softirq;

    // Since  Linux 2.6.11, there is an eighth column,
    // steal - stolen time, which is the time spent in other oper-
    // ating systems when running in a virtualized environment
    unsigned long long m_steal;

    // Since Linux 2.6.24, there is a ninth column,
    // guest, which is the time spent running a virtual CPU for guest
    // operating systems under the control of the Linux kernel.
    unsigned long long m_guest;

public:
    ProcSystemStat();

    // Get total cpu units.
    int64_t Total();
};

// Get system cpu stat, use cache to avoid performance problem.
extern ProcSelfStat* GetSelfProcStat();
// Get system cpu stat, use cache to avoid performance problem.
extern ProcSystemStat* GetSystemProcStat();
// The daemon st-thread will update it.
extern void UpdateProcStat();

// Stat disk iops
// @see: http://stackoverflow.com/questions/4458183/how-the-util-of-iostat-is-computed
// for total disk io, @see: cat /proc/vmstat |grep pgpg
// for device disk io, @see: cat /proc/diskstats
// @remark, user can use command to test the disk io:
//      time dd if=/dev/zero bs=1M count=2048 of=file_2G
// @remark, the iotop is right, the dstat result seems not ok,
//      while the iostat only show the number of writes, not the bytes,
//      where the dd command will give the write MBps, it's absolutely right.
class DiskStat
{
public:
    // Whether the data is ok.
    bool m_ok;
    // The time in ms when sample.
    int64_t m_sampleTime;

    // input(read) KBytes per seconds
    int m_inKBps;
    // output(write) KBytes per seconds
    int m_outKBps;

    // @see: print_partition_stats() of iostat.c
    // but its value is [0, +], for instance, 0.1532 means 15.32%.
    float m_busy;
    // for stat the busy%
    ProcSystemStat m_cpu;

public:
    // @see: cat /proc/vmstat
    // The in(read) page count, pgpgin*1024 is the read bytes.
    // Total number of kilobytes the system paged in from disk per second.
    unsigned long m_pgpgin;
    // The out(write) page count, pgpgout*1024 is the write bytes.
    // Total number of kilobytes the system paged out to disk per second.
    unsigned long m_pgpgout;

    // @see: https://www.kernel.org/doc/Documentation/iostats.txt
    // @see: http://tester-higkoo.googlecode.com/svn-history/r14/trunk/Tools/iostat/iostat.c
    // @see: cat /proc/diskstats
    //
    // Number of issued reads.
    // This is the total number of reads completed successfully.
    // Read I/O operations
    unsigned int m_rdIos;
    // Number of reads merged
    // Reads merged
    unsigned int m_rdMerges;
    // Number of sectors read.
    // This is the total number of sectors read successfully.
    // Sectors read
    unsigned long long m_rdSectors;
    // Number of milliseconds spent reading.
    // This is the total number of milliseconds spent by all reads
    // (as measured from make_request() to end_that_request_last()).
    // Time in queue + service for read
    unsigned int m_rdTicks;
    //
    // Number of writes completed.
    // This is the total number of writes completed successfully
    // Write I/O operations
    unsigned int m_wrIos;
    // Number of writes merged Reads and writes which are adjacent
    // To each other may be merged for efficiency. Thus two 4K
    // reads may become one 8K read before it is ultimately
    // handed to the disk, and so it will be counted (and queued)
    // as only one I/O. This field lets you know how often this was done.
    // Writes merged
    unsigned int m_wrMerges;
    // Number of sectors written.
    // This is the total number of sectors written successfully.
    // Sectors written
    unsigned long long m_wrSectors;
    // Number of milliseconds spent writing .
    // This is the total number of milliseconds spent by all writes
    // (as measured from make_request() to end_that_request_last()).
    // Time in queue + service for write
    unsigned int m_wrTicks;
    //
    // Number of I/Os currently in progress.
    // The only field that should go to zero.
    // Incremented as requests are given to appropriate request_queue_t
    // and decremented as they finish.
    unsigned int m_nbCurrent;
    // Number of milliseconds spent doing I/Os.
    // This field is increased so long as field 9 is nonzero.
    // Time of requests in queue
    unsigned int m_ticks;
    // Number of milliseconds spent doing I/Os.
    // This field is incremented at each I/O start, I/O completion,
    // I/O merge, or read of these stats by the number of I/Os in
    // progress (field 9) times the number of milliseconds spent
    // doing I/O since the last update of this field. This can
    // provide an easy measure of both I/O completion time and
    // The backlog that may be accumulating.
    // Average queue length
    unsigned int m_aveq;

public:
    DiskStat();
};

// Get disk stat, use cache to avoid performance problem.
extern DiskStat* GetDiskStat();
// The daemon st-thread will update it.
extern void UpdateDiskStat();

// Stat system memory info
// @see: cat /proc/meminfo
class MemInfo
{
public:
    // Whether the data is ok.
    bool m_ok;
    // The time in ms when sample.
    int64_t m_sampleTime;
    // The percent of usage. 0.153 is 15.3%.
    float m_percentRam;
    float m_percentSwap;

    // data of /proc/meminfo
public:
    // MemActive = MemTotal - MemFree
    uint64_t m_MemActive;
    // RealInUse = MemActive - Buffers - Cached
    uint64_t m_RealInUse;
    // NotInUse = MemTotal - RealInUse
    //          = MemTotal - MemActive + Buffers + Cached
    //          = MemTotal - MemTotal + MemFree + Buffers + Cached
    //          = MemFree + Buffers + Cached
    uint64_t m_NotInUse;

    unsigned long m_MemTotal;
    unsigned long m_MemFree;
    unsigned long m_Buffers;
    unsigned long m_Cached;
    unsigned long m_SwapTotal;
    unsigned long m_SwapFree;

public:
    MemInfo();
};

// Get system meminfo, use cache to avoid performance problem.
extern MemInfo* GetMeminfo();
// The daemon st-thread will update it.
extern void UpdateMeminfo();

// system cpu hardware info.
// @see: cat /proc/cpuinfo
// @remark, we use sysconf(_SC_NPROCESSORS_CONF) to get the cpu count.
class CpuInfo
{
public:
    // Whether the data is ok.
    bool m_ok;

    // data of /proc/cpuinfo
public:
    // The number of processors configured.
    int m_nbProcessors;
    // The number of processors currently online (available).
    int m_nbProcessorsOnline;

public:
    CpuInfo();
};

// Get system cpu info, use cache to avoid performance problem.
extern CpuInfo* GetCpuinfo();

// The platform(os, srs) uptime/load summary
class PlatformInfo
{
public:
    // Whether the data is ok.
    bool m_ok;

    // srs startup time, in ms.
    int64_t m_startupTime;

public:
    // @see: cat /proc/uptime
    // system startup time in seconds.
    double m_osUptime;
    // system all cpu idle time in seconds.
    // @remark to cal the cpu ustime percent:
    //      os_ilde_time % (os_uptime * SrsCpuInfo.nb_processors_online)
    double m_osIldeTime;

    // @see: cat /proc/loadavg
    double m_loadOneMinutes;
    double m_loadFiveMinutes;
    double m_loadFifteenMinutes;

public:
    PlatformInfo();
};

// Get platform info, use cache to avoid performance problem.
extern PlatformInfo* GetPlatformInfo();
// The daemon st-thread will update it.
extern void UpdatePlatformInfo();

class SnmpUdpStat
{
public:
    // Whether the data is ok.
    bool m_ok;
    // send and recv buffer error delta
    int m_rcvBufErrorsDelta;
    int m_sndBufErrorsDelta;

public:
    // @see: cat /proc/uptimecat /proc/net/snmp|grep 'Udp:'
    // @see: https://blog.packagecloud.io/eng/2017/02/06/monitoring-tuning-linux-networking-stack-sending-data/#procnetsnmp
    // InDatagrams: incremented when recvmsg was used by a userland program to read datagram.
    // also incremented when a UDP packet is encapsulated and sent back for processing.
    unsigned long long m_inDatagrams;
    // NoPorts: incremented when UDP packets arrive destined for a port where no program is listening.
    unsigned long long m_noPorts;
    // InErrors: incremented in several cases: no memory in the receive queue, when a bad checksum is seen,
    // and if sk_add_backlog fails to add the datagram.
    unsigned long long m_inErrors;
    // OutDatagrams: incremented when a UDP packet is handed down without error to the IP protocol layer to be sent.
    unsigned long long m_outDatagrams;
    // RcvbufErrors: incremented when sock_queue_rcv_skb reports that no memory is available;
    // this happens if sk->sk_rmem_alloc is greater than or equal to sk->sk_rcvbuf.
    unsigned long long m_rcvBufErrors;
    // SndbufErrors: incremented if the IP protocol layer reported an error when trying to send the packet
    // and no error queue has been setup. also incremented if no send queue space or kernel memory are available.
    unsigned long long m_sndBufErrors;
    // InCsumErrors: incremented when a UDP checksum failure is detected.
    // Note that in all cases I could find, InCsumErrors is incremented at the same time as InErrors.
    // Thus, InErrors - InCsumErros should yield the count of memory related errors on the receive side.
    unsigned long long m_inCsumErrors;
public:
    SnmpUdpStat();
    ~SnmpUdpStat();
};

// Get SNMP udp statistic, use cache to avoid performance problem.
extern SnmpUdpStat* GetUdpSnmpStat();
// The daemon st-thread will update it.
void UpdateUdpSnmpStatistic();

// The network device summary for each network device, for example, eth0, eth1, ethN
class NetworkDevices
{
public:
    // Whether the network device is ok.
    bool m_ok;

    // 6-chars interfaces name
    char m_name[7];
    // The sample time in ms.
    int64_t m_sampleTime;

public:
    // data for receive.
    unsigned long long m_rbytes;
    unsigned long m_rpackets;
    unsigned long m_rerrs;
    unsigned long m_rdrop;
    unsigned long m_rfifo;
    unsigned long m_rframe;
    unsigned long m_rcompressed;
    unsigned long m_rmulticast;

    // data for transmit
    unsigned long long m_sbytes;
    unsigned long m_spackets;
    unsigned long m_serrs;
    unsigned long m_sdrop;
    unsigned long m_sfifo;
    unsigned long m_scolls;
    unsigned long m_scarrier;
    unsigned long m_scompressed;

public:
    NetworkDevices();
};

// Get network devices info, use cache to avoid performance problem.
extern NetworkDevices* GetNetworkDevices();
extern int GetNetworkDevicesCount();
// The daemon st-thread will update it.
extern void UpdateNetworkDevices();

// The system connections, and srs rtmp network summary
class NetworkRtmpServer
{
public:
    // Whether the network device is ok.
    bool m_ok;

    // The sample time in ms.
    int64_t m_sampletime;

public:
    // data for receive.
    int64_t m_rbytes;
    int m_rkbps;
    int m_rkbps30s;
    int m_rkbps5m;

    // data for transmit
    int64_t m_sbytes;
    int m_skbps;
    int m_skbps30s;
    int m_skbps5m;

    // connections
    // @see: /proc/net/snmp
    // @see: /proc/net/sockstat
    int m_nbConnSys;
    int m_nbConnSysEt; // established
    int m_nbConnSysTw; // time wait
    int m_nbConnSysUdp; // udp

    // retrieve from srs interface
    int m_nbConnSrs;

public:
    NetworkRtmpServer();
};

// Get network devices info, use cache to avoid performance problem.
extern NetworkRtmpServer* GetNetworkRtmpServer();
// The daemon st-thread will update it.
extern void UpdateRtmpServer(int nb_conn, Kbps* kbps);

// Get local or peer ip.
// Where local ip is the server ip which client connected.
extern std::string GetLocalIp(int fd);
// Get the local id port.
extern int GetLocalPort(int fd);
// Where peer ip is the client public ip which connected to server.
extern std::string GetPeerIp(int fd);
extern int GetPeerPort(int fd);

// Whether string is boolean
//      is_bool("true") == true
//      is_bool("false") == true
//      otherwise, false.
extern bool IsBoolean(std::string str);

// Dump summaries for /api/v1/summaries.
extern void ApiDumpSummaries(JsonObject* obj);

// Dump string(str in length) to hex, it will process min(limit, length) chars.
// Append seperator between each elem, and newline when exceed line_limit, '\0' to ignore.
extern std::string StringDumpsHex(const std::string& str);
extern std::string StringDumpsHex(const char* str, int length);
extern std::string StringDumpsHex(const char* str, int length, int limit);
extern std::string StringDumpsHex(const char* str, int length, int limit, char seperator, int line_limit, char newline);

// Get ENV variable, which may starts with $.
//      srs_getenv("EIP") === srs_getenv("$EIP")
extern std::string Getenv(const std::string& key);

#endif // APP_UTILITY_H
