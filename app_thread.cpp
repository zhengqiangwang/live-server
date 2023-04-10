#include "app_thread.h"
#include "app_async_call.h"
#include "app_config.h"
#include "app_hybrid.h"
#include "app_utility.h"
#include "kbps.h"
#include "log.h"
#include "protocol_log.h"
#include "utility.h"
#include "app_log.h"
#include "app_source.h"
#include "app_pithy_print.h"
#include "app_conn.h"
#include "core.h"
#include "app_service_conn.h"
#include <fcntl.h>

extern StageManager* _stages;
extern Config* config;
extern IContext* Context;
extern CourseManager* courses;

Pps* pps_aloss2 = NULL;
extern Pps* pps_cids_get;
extern Pps* pps_cids_set;

extern Pps* pps_ids;
extern Pps* pps_fids;
extern Pps* pps_fids_level0;
extern Pps* pps_dispose;

extern Pps* pps_timer;
extern Pps* pps_conn;
extern Pps* pps_pub;
extern Pps* pps_clock_15ms;
extern Pps* pps_clock_20ms;
extern Pps* pps_clock_25ms;
extern Pps* pps_clock_30ms;
extern Pps* pps_clock_35ms;
extern Pps* pps_clock_40ms;
extern Pps* pps_clock_80ms;
extern Pps* pps_clock_160ms;
extern Pps* pps_timer_s;
extern Pps* pps_rpkts;
extern Pps* pps_addrs;
extern Pps* pps_fast_addrs;

extern Pps* pps_spkts;
extern Pps* pps_objs_msgs;

CircuitBreaker::CircuitBreaker()
{
    m_enabled = false;
    m_highThreshold = 0;
    m_highPulse = 0;
    m_criticalThreshold = 0;
    m_criticalPulse = 0;
    m_dyingThreshold = 0;
    m_dyingPulse = 0;

    m_hybridHighWaterLevel = 0;
    m_hybridCriticalWaterLevel = 0;
    m_hybridDyingWaterLevel = 0;
}

CircuitBreaker::~CircuitBreaker()
{

}

error CircuitBreaker::Initialize()
{
    error err = SUCCESS;

    m_enabled = config->GetCircuitBreaker();
    m_highThreshold = config->GetHighThreshold();
    m_highPulse = config->GetHighPulse();
    m_criticalThreshold = config->GetCriticalThreshold();
    m_criticalPulse = config->GetCriticalPulse();
    m_dyingThreshold = config->GetDyingThreshold();
    m_dyingPulse = config->GetDyingPulse();

    // Update the water level for circuit breaker.
    // @see SrsCircuitBreaker::on_timer()
    hybrid->timer1s()->Subscribe(this);

    trace("CircuitBreaker: enabled=%d, high=%dx%d, critical=%dx%d, dying=%dx%d", m_enabled,
        m_highPulse, m_highThreshold, m_criticalPulse, m_criticalThreshold,
        m_dyingPulse, m_dyingThreshold);

    return err;
}

bool CircuitBreaker::HybridHighWaterLevel()
{
    return m_enabled && (HybridCriticalWaterLevel() || m_hybridHighWaterLevel);
}

bool CircuitBreaker::HybridCriticalWaterLevel()
{
    return m_enabled && (HybridDyingWaterLevel() || m_hybridCriticalWaterLevel);
}

bool CircuitBreaker::HybridDyingWaterLevel()
{
    return m_enabled && m_dyingPulse && m_hybridDyingWaterLevel >= m_dyingPulse;
}

error CircuitBreaker::OnTimer(utime_t interval)
{
    error err = SUCCESS;

    // Update the CPU usage.
    UpdateProcStat();
    ProcSelfStat* stat = GetSelfProcStat();

    // Reset the high water-level when CPU is low for N times.
    if (stat->m_percent * 100 > m_highThreshold) {
        m_hybridHighWaterLevel = m_highPulse;
    } else if (m_hybridHighWaterLevel > 0) {
        m_hybridHighWaterLevel--;
    }

    // Reset the critical water-level when CPU is low for N times.
    if (stat->m_percent * 100 > m_criticalThreshold) {
        m_hybridCriticalWaterLevel = m_criticalPulse;
    } else if (m_hybridCriticalWaterLevel > 0) {
        m_hybridCriticalWaterLevel--;
    }

    // Reset the dying water-level when CPU is low for N times.
    if (stat->m_percent * 100 > m_dyingThreshold) {
        m_hybridDyingWaterLevel = MIN(m_dyingPulse + 1, m_hybridDyingWaterLevel + 1);
    } else if (m_hybridDyingWaterLevel > 0) {
        m_hybridDyingWaterLevel = 0;
    }

    // Show statistics for RTC server.
    ProcSelfStat* u = GetSelfProcStat();
    // Resident Set Size: number of pages the process has in real memory.
    int memory = (int)(u->m_rss * 4 / 1024);

    // The hybrid thread cpu and memory.
    float thread_percent = stat->m_percent * 100;

    static char buf[128];

    std::string snk_desc;
#ifdef SRS_RTC
    if (_srs_pps_snack2->r10s()) {
        snprintf(buf, sizeof(buf), ", snk=%d,%d,%d",
            _srs_pps_snack2->r10s(), _srs_pps_snack3->r10s(), _srs_pps_snack4->r10s() // NACK packet,seqs sent.
        );
        snk_desc = buf;
    }
#endif

    if (m_enabled && (HybridHighWaterLevel() || HybridCriticalWaterLevel())) {
        trace("CircuitBreaker: cpu=%.2f%%,%dMB, break=%d,%d,%d, cond=%.2f%%%s",
            u->m_percent * 100, memory,
            HybridHighWaterLevel(), HybridCriticalWaterLevel(), HybridDyingWaterLevel(), // Whether Circuit-Break is enable.
            thread_percent, // The conditions to enable Circuit-Breaker.
            snk_desc.c_str()
        );
    }

    return err;
}

CircuitBreaker* circuit_breaker = NULL;
AsyncCallWorker* _dvr_async = NULL;

error GlobalInitialize()
{
    error err = SUCCESS;

    // Root global objects.
    Log = new FileLog();
    Context = new ThreadContext();
    config = new Config();


    // The clock wall object.
    _clock = new WallClock();

    // The pps cids depends by st init.
    pps_cids_get = new Pps();
    pps_cids_set = new Pps();

    // The global objects which depends on ST.
    hybrid = new HybridServer();
    sources = new LiveSourceManager();
    _stages = new StageManager();
    circuit_breaker = new CircuitBreaker();
    courses = new CourseManager();

#ifdef SRS_SRT
    _srs_srt_sources = new SrsSrtSourceManager();
#endif

#ifdef SRS_RTC
    _srs_rtc_sources = new SrsRtcSourceManager();
    _srs_blackhole = new SrsRtcBlackhole();

    _srs_rtc_manager = new SrsResourceManager("RTC", true);
    _srs_rtc_dtls_certificate = new SrsDtlsCertificate();
#endif
#ifdef SRS_GB28181
    _srs_gb_manager = new SrsResourceManager("GB", true);
#endif
    _gc = new LazySweepGc();

    // Initialize global pps, which depends on _srs_clock
    pps_ids = new Pps();
    pps_fids = new Pps();
    pps_fids_level0 = new Pps();
    pps_dispose = new Pps();

    pps_timer = new Pps();
    pps_conn = new Pps();
    pps_pub = new Pps();

#ifdef SRS_RTC
    _srs_pps_snack = new SrsPps();
    _srs_pps_snack2 = new SrsPps();
    _srs_pps_snack3 = new SrsPps();
    _srs_pps_snack4 = new SrsPps();
    _srs_pps_sanack = new SrsPps();
    _srs_pps_svnack = new SrsPps();

    _srs_pps_rnack = new SrsPps();
    _srs_pps_rnack2 = new SrsPps();
    _srs_pps_rhnack = new SrsPps();
    _srs_pps_rmnack = new SrsPps();
#endif

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_recvfrom = new SrsPps();
    _srs_pps_recvfrom_eagain = new SrsPps();
    _srs_pps_sendto = new SrsPps();
    _srs_pps_sendto_eagain = new SrsPps();

    _srs_pps_read = new SrsPps();
    _srs_pps_read_eagain = new SrsPps();
    _srs_pps_readv = new SrsPps();
    _srs_pps_readv_eagain = new SrsPps();
    _srs_pps_writev = new SrsPps();
    _srs_pps_writev_eagain = new SrsPps();

    _srs_pps_recvmsg = new SrsPps();
    _srs_pps_recvmsg_eagain = new SrsPps();
    _srs_pps_sendmsg = new SrsPps();
    _srs_pps_sendmsg_eagain = new SrsPps();

    _srs_pps_epoll = new SrsPps();
    _srs_pps_epoll_zero = new SrsPps();
    _srs_pps_epoll_shake = new SrsPps();
    _srs_pps_epoll_spin = new SrsPps();

    _srs_pps_sched_15ms = new SrsPps();
    _srs_pps_sched_20ms = new SrsPps();
    _srs_pps_sched_25ms = new SrsPps();
    _srs_pps_sched_30ms = new SrsPps();
    _srs_pps_sched_35ms = new SrsPps();
    _srs_pps_sched_40ms = new SrsPps();
    _srs_pps_sched_80ms = new SrsPps();
    _srs_pps_sched_160ms = new SrsPps();
    _srs_pps_sched_s = new SrsPps();
#endif

    pps_clock_15ms = new Pps();
    pps_clock_20ms = new Pps();
    pps_clock_25ms = new Pps();
    pps_clock_30ms = new Pps();
    pps_clock_35ms = new Pps();
    pps_clock_40ms = new Pps();
    pps_clock_80ms = new Pps();
    pps_clock_160ms = new Pps();
    pps_timer_s = new Pps();

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_thread_run = new SrsPps();
    _srs_pps_thread_idle = new SrsPps();
    _srs_pps_thread_yield = new SrsPps();
    _srs_pps_thread_yield2 = new SrsPps();
#endif

    pps_rpkts = new Pps();
    pps_addrs = new Pps();
    pps_fast_addrs = new Pps();

    pps_spkts = new Pps();
    pps_objs_msgs = new Pps();

#ifdef SRS_RTC
    _srs_pps_sstuns = new SrsPps();
    _srs_pps_srtcps = new SrsPps();
    _srs_pps_srtps = new SrsPps();

    _srs_pps_rstuns = new SrsPps();
    _srs_pps_rrtps = new SrsPps();
    _srs_pps_rrtcps = new SrsPps();

    _srs_pps_aloss2 = new SrsPps();

    _srs_pps_pli = new SrsPps();
    _srs_pps_twcc = new SrsPps();
    _srs_pps_rr = new SrsPps();

    _srs_pps_objs_rtps = new SrsPps();
    _srs_pps_objs_rraw = new SrsPps();
    _srs_pps_objs_rfua = new SrsPps();
    _srs_pps_objs_rbuf = new SrsPps();
    _srs_pps_objs_rothers = new SrsPps();
#endif

    // Create global async worker for DVR.
    _dvr_async = new AsyncCallWorker();

//    // Initialize global TencentCloud CLS object.
//    _srs_cls = new ClsClient();
//    _srs_apm = new ApmClient();

    return err;
}

ThreadMutex::ThreadMutex()
{
    // https://man7.org/linux/man-pages/man3/pthread_mutexattr_init.3.html
    int r0 = pthread_mutexattr_init(&m_attr);
    Assert(!r0);

    // https://man7.org/linux/man-pages/man3/pthread_mutexattr_gettype.3p.html
    r0 = pthread_mutexattr_settype(&m_attr, PTHREAD_MUTEX_ERRORCHECK);
    Assert(!r0);

    // https://michaelkerrisk.com/linux/man-pages/man3/pthread_mutex_init.3p.html
    r0 = pthread_mutex_init(&m_lock, &m_attr);
    Assert(!r0);
}

ThreadMutex::~ThreadMutex()
{
    int r0 = pthread_mutex_destroy(&m_lock);
    Assert(!r0);

    r0 = pthread_mutexattr_destroy(&m_attr);
    Assert(!r0);
}

void ThreadMutex::Lock()
{
    // https://man7.org/linux/man-pages/man3/pthread_mutex_lock.3p.html
    //        EDEADLK
    //                 The mutex type is PTHREAD_MUTEX_ERRORCHECK and the current
    //                 thread already owns the mutex.
    int r0 = pthread_mutex_lock(&m_lock);
    Assert(!r0);
}

void ThreadMutex::UnLock()
{
    int r0 = pthread_mutex_unlock(&m_lock);
    Assert(!r0);
}

ThreadEntry::ThreadEntry()
{
    m_pool = nullptr;
    m_start = nullptr;
    m_arg = nullptr;
    m_num = 0;
    m_tid = 0;

    m_err = SUCCESS;
}

ThreadEntry::~ThreadEntry()
{
    Freep(m_err);

    // TODO: FIXME: Should dispose trd.
}

ThreadPool::ThreadPool()
{
    m_entry = NULL;
    m_lock = new ThreadMutex();
    m_hybrid = NULL;

    // Add primordial thread, current thread itself.
    ThreadEntry* entry = new ThreadEntry();
    m_threads.push_back(entry);
    m_entry = entry;

    entry->m_pool = this;
    entry->m_label = "primordial";
    entry->m_start = NULL;
    entry->m_arg = NULL;
    entry->m_num = 1;
    entry->m_trd = pthread_self();
    entry->m_tid = gettid();

    char buf[256];
    snprintf(buf, sizeof(buf), "srs-master-%d", entry->m_num);
    entry->m_name = buf;

    m_pidFd = -1;
}

ThreadPool::~ThreadPool()
{
    Freep(m_lock);

    if (m_pidFd > 0) {
        ::close(m_pidFd);
        m_pidFd = -1;
    }
}

error ThreadPool::SetupThreadLocals()
{
    error err = SUCCESS;

    // Initialize ST, which depends on pps cids.
    if ((err = StInit()) != SUCCESS) {
        return ERRORWRAP(err, "initialize st failed");
    }

    return err;
}

error ThreadPool::Initialize()
{
    error err = SUCCESS;

    if ((err = AcquirePidFile()) != SUCCESS) {
        return ERRORWRAP(err, "acquire pid file");
    }

    // Initialize the master primordial thread.
    ThreadEntry* entry = (ThreadEntry*)m_entry;

    m_interval = config->GetThreadsInterval();

    trace("Thread #%d(%s): init name=%s, interval=%dms", entry->m_num, entry->m_label.c_str(), entry->m_name.c_str(), u2msi(m_interval));

    return err;
}

error ThreadPool::AcquirePidFile()
{
    std::string pid_file = config->GetPidFile();

    // -rw-r--r--
    // 644
    int mode = S_IRUSR | S_IWUSR |  S_IRGRP | S_IROTH;

    int fd;
    // open pid file
    if ((fd = ::open(pid_file.c_str(), O_WRONLY | O_CREAT, mode)) == -1) {
        return ERRORNEW(ERROR_SYSTEM_PID_ACQUIRE, "open pid file=%s", pid_file.c_str());
    }

    // require write lock
    struct flock lock;

    lock.l_type = F_WRLCK; // F_RDLCK, F_WRLCK, F_UNLCK
    lock.l_start = 0; // type offset, relative to l_whence
    lock.l_whence = SEEK_SET;  // SEEK_SET, SEEK_CUR, SEEK_END
    lock.l_len = 0;

    if (fcntl(fd, F_SETLK, &lock) == -1) {
        if(errno == EACCES || errno == EAGAIN) {
            ::close(fd);
            ERROR("srs is already running!");
            return ERRORNEW(ERROR_SYSTEM_PID_ALREADY_RUNNING, "srs is already running");
        }
        return ERRORNEW(ERROR_SYSTEM_PID_LOCK, "access to pid=%s", pid_file.c_str());
    }

    // truncate file
    if (ftruncate(fd, 0) != 0) {
        return ERRORNEW(ERROR_SYSTEM_PID_TRUNCATE_FILE, "truncate pid file=%s", pid_file.c_str());
    }

    // write the pid
    std::string pid = Int2Str(getpid());
    if (write(fd, pid.c_str(), pid.length()) != (int)pid.length()) {
        return ERRORNEW(ERROR_SYSTEM_PID_WRITE_FILE, "write pid=%s to file=%s", pid.c_str(), pid_file.c_str());
    }

    // auto close when fork child process.
    int val;
    if ((val = fcntl(fd, F_GETFD, 0)) < 0) {
        return ERRORNEW(ERROR_SYSTEM_PID_GET_FILE_INFO, "fcntl fd=%d", fd);
    }
    val |= FD_CLOEXEC;
    if (fcntl(fd, F_SETFD, val) < 0) {
        return ERRORNEW(ERROR_SYSTEM_PID_SET_FILE_INFO, "lock file=%s fd=%d", pid_file.c_str(), fd);
    }

    trace("write pid=%s to %s success!", pid.c_str(), pid_file.c_str());
    m_pidFd = fd;

    return SUCCESS;
}

error ThreadPool::Execute(std::string label, error (*start)(void *), void *arg)
{
    error err = SUCCESS;

    ThreadEntry* entry = new ThreadEntry();

    // Update the hybrid thread entry for circuit breaker.
    if (label == "hybrid") {
        m_hybrid = entry;
        m_hybrids.push_back(entry);
    }

    // To protect the threads_ for executing thread-safe.
    if (true) {
        ThreadLocker(m_lock);
        m_threads.push_back(entry);
    }

    entry->m_pool = this;
    entry->m_label = label;
    entry->m_start = start;
    entry->m_arg = arg;

    // The id of thread, should equal to the debugger thread id.
    // For gdb, it's: info threads
    // For lldb, it's: thread list
    static int num = m_entry->m_num + 1;
    entry->m_num = num++;

    char buf[256];
    snprintf(buf, sizeof(buf), "srs-%s-%d", entry->m_label.c_str(), entry->m_num);
    entry->m_name = buf;

    // https://man7.org/linux/man-pages/man3/pthread_create.3.html
    pthread_t trd;
    int r0 = pthread_create(&trd, NULL, ThreadPool::Start, entry);
    if (r0 != 0) {
        entry->m_err = ERRORNEW(ERROR_THREAD_CREATE, "create thread %s, r0=%d", label.c_str(), r0);
        return ERRORCOPY(entry->m_err);
    }

    entry->m_trd = trd;

    return err;
}

error ThreadPool::Run()
{
    error err = SUCCESS;

    while (true) {
        std::vector<ThreadEntry*> threads;
        if (true) {
            ThreadLocker(m_lock);
            threads = m_threads;
        }

        // Check the threads status fastly.
        int loops = (int)(m_interval / UTIME_SECONDS);
        for (int i = 0; i < loops; i++) {
            for (int i = 0; i < (int)threads.size(); i++) {
                ThreadEntry* entry = threads.at(i);
                if (entry->m_err != SUCCESS) {
                    // Quit with success.
                    if (ERRORCODE(entry->m_err) == ERROR_THREAD_FINISHED) {
                        trace("quit for thread #%d(%s) finished", entry->m_num, entry->m_label.c_str());
                        Freep(err);
                        return SUCCESS;
                    }

                    // Quit with specified error.
                    err = ERRORCOPY(entry->m_err);
                    err = ERRORWRAP(err, "thread #%d(%s)", entry->m_num, entry->m_label.c_str());
                    return err;
                }
            }

            Usleep(1 * UTIME_SECONDS);
        }

        // Show statistics for RTC server.
        ProcSelfStat* u = GetSelfProcStat();
        // Resident Set Size: number of pages the process has in real memory.
        int memory = (int)(u->m_rss * 4 / 1024);

        trace("Process: cpu=%.2f%%,%dMB, threads=%d", u->m_percent * 100, memory, (int)m_threads.size());
    }

    return err;
}

void ThreadPool::Stop()
{
    // TODO: FIXME: Should notify other threads to do cleanup and quit.
}

ThreadEntry *ThreadPool::Self()
{
    std::vector<ThreadEntry*> threads;

    if (true) {
        ThreadLocker(m_lock);
        threads = m_threads;
    }

    for (int i = 0; i < (int)threads.size(); i++) {
        ThreadEntry* entry = threads.at(i);
        if (entry->m_trd == pthread_self()) {
            return entry;
        }
    }

    return NULL;
}

ThreadEntry *ThreadPool::Hybrid()
{
    return m_hybrid;
}

std::vector<ThreadEntry *> ThreadPool::Hybrids()
{
    return m_hybrids;
}

void *ThreadPool::Start(void *arg)
{
    error err = SUCCESS;

    ThreadEntry* entry = (ThreadEntry*)arg;

    // Initialize thread-local variables.
    if ((err = ThreadPool::SetupThreadLocals()) != SUCCESS) {
        entry->m_err = err;
        return NULL;
    }

    // Set the thread local fields.
    entry->m_tid = gettid();

#ifndef SRS_OSX
    // https://man7.org/linux/man-pages/man3/pthread_setname_np.3.html
    pthread_setname_np(pthread_self(), entry->m_name.c_str());
#else
    pthread_setname_np(entry->name.c_str());
#endif

    trace("Thread #%d: run with tid=%d, entry=%p, label=%s, name=%s", entry->m_num, (int)entry->m_tid, entry, entry->m_label.c_str(), entry->m_name.c_str());

    if ((err = entry->m_start(entry->m_arg)) != SUCCESS) {
        entry->m_err = err;
    }

    // We use a special error to indicates the normally done.
    if (entry->m_err == SUCCESS) {
        entry->m_err = ERRORNEW(ERROR_THREAD_FINISHED, "finished normally");
    }

    // We do not use the return value, the err has been set to entry->err.
    return NULL;
}

// It MUST be thread-safe, global and shared object.
ThreadPool* thread_pool = new ThreadPool();
