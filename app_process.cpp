#include "app_process.h"

#include "app_utility.h"
#include "error.h"
#include "log.h"
#include "protocol_st.h"
#include "protocol_utility.h"
#include "utility.h"
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

#ifndef _WIN32
#include <unistd.h>
#endif

Process::Process()
{
    m_isStarted         = false;
    m_fastStopped       = false;
    m_pid                = -1;
}

Process::~Process()
{

}

int Process::GetPid()
{
    return m_pid;
}

bool Process::Started()
{
    return m_isStarted;
}

error Process::Initialize(std::string binary, std::vector<std::string> argv)
{
    error err = SUCCESS;

    m_bin = binary;
    m_cli = "";
    m_actualCli = "";
    m_params.clear();

    for (int i = 0; i < (int)argv.size(); i++) {
        std::string ffp = argv[i];
        std::string nffp = (i < (int)argv.size() - 1)? argv[i + 1] : "";
        std::string nnffp = (i < (int)argv.size() - 2)? argv[i + 2] : "";

        // >file
        if (StringStartsWith(ffp, ">")) {
            m_stdoutFile = ffp.substr(1);
            continue;
        }

        // 1>file
        if (StringStartsWith(ffp, "1>")) {
            m_stdoutFile = ffp.substr(2);
            continue;
        }

        // 2>file
        if (StringStartsWith(ffp, "2>")) {
            m_stderrFile = ffp.substr(2);
            continue;
        }

        // 1 >X
        if (ffp == "1" && StringStartsWith(nffp, ">")) {
            if (nffp == ">") {
                // 1 > file
                if (!nnffp.empty()) {
                    m_stdoutFile = nnffp;
                    i++;
                }
            } else {
                // 1 >file
                m_stdoutFile = StringTrimStart(nffp, ">");
            }
            // skip the >
            i++;
            continue;
        }

        // 2 >X
        if (ffp == "2" && StringStartsWith(nffp, ">")) {
            if (nffp == ">") {
                // 2 > file
                if (!nnffp.empty()) {
                    m_stderrFile = nnffp;
                    i++;
                }
            } else {
                // 2 >file
                m_stderrFile = StringTrimStart(nffp, ">");
            }
            // skip the >
            i++;
            continue;
        }

        m_params.push_back(ffp);
    }

    m_actualCli = JoinVectorString(m_params, " ");
    m_cli = JoinVectorString(argv, " ");

    return err;
}

error srs_redirect_output(std::string from_file, int to_fd)
{
    error err = SUCCESS;

    // use default output.
    if (from_file.empty()) {
        return err;
    }

    // redirect the fd to file.
    int fd = -1;
    int flags = O_CREAT|O_RDWR|O_APPEND;
    mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;

    if ((fd = ::open(from_file.c_str(), flags, mode)) < 0) {
        return ERRORNEW(ERROR_FORK_OPEN_LOG, "open process %d %s failed", to_fd, from_file.c_str());
    }

    int r0 = dup2(fd, to_fd);
    ::close(fd);

    if (r0 < 0) {
        return ERRORNEW(ERROR_FORK_DUP2_LOG, "dup2 fd=%d, to=%d, file=%s failed, r0=%d",
            fd, to_fd, from_file.c_str(), r0);
    }

    return err;
}

error Process::RedirectIo()
{
    error err = SUCCESS;

    // for the stdout, ignore when not specified.
    // redirect stdout to file if possible.
    if ((err = srs_redirect_output(m_stdoutFile, STDOUT_FILENO)) != SUCCESS) {
        return ERRORWRAP(err, "redirect stdout");
    }

    // for the stderr, ignore when not specified.
    // redirect stderr to file if possible.
    if ((err = srs_redirect_output(m_stderrFile, STDERR_FILENO)) != SUCCESS) {
        return ERRORWRAP(err, "redirect stderr");
    }

    // No stdin for process, @bug https://github.com/ossrs/srs/issues/1592
    if ((err = srs_redirect_output("/dev/null", STDIN_FILENO)) != SUCCESS) {
        return ERRORWRAP(err, "redirect /dev/null");
    }

    return err;
}

error Process::Start()
{
    error err = SUCCESS;

    if (m_isStarted) {
        return err;
    }

    // generate the argv of process.
    info("fork process: %s", m_cli.c_str());

    // for log
    ContextId cid = Context->GetId();
    int ppid = getpid();

    // TODO: fork or vfork?
    if ((m_pid = fork()) < 0) {
        return ERRORNEW(ERROR_ENCODER_FORK, "vfork process failed, cli=%s", m_cli.c_str());
    }

    // for osx(lldb) to debug the child process.
    // user can use "lldb -p <pid>" to resume the parent or child process.
    //kill(0, SIGSTOP);

    // child process: ffmpeg encoder engine.
    if (m_pid == 0) {
        // ignore the SIGINT and SIGTERM
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_IGN);

        // redirect standard I/O, if it failed, output error to stdout, and exit child process.
        if ((err = RedirectIo()) != SUCCESS) {
            fprintf(stdout, "child process error, %s\n", ERRORDESC(err).c_str());
            exit(-1);
        }

        // should never close the fd 3+, for it myabe used.
        // for fd should close at exec, use fnctl to set it.

        // log basic info to stderr.
        if (true) {
            fprintf(stdout, "\n");
            fprintf(stdout, "process ppid=%d, cid=%s, pid=%d, in=%d, out=%d, err=%d\n",
                ppid, cid.Cstr(), getpid(), STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);
            fprintf(stdout, "process binary=%s, cli: %s\n", m_bin.c_str(), m_cli.c_str());
            fprintf(stdout, "process actual cli: %s\n", m_actualCli.c_str());
        }

        // memory leak in child process, it's ok.
        char** argv = new char*[m_params.size() + 1];
        for (int i = 0; i < (int)m_params.size(); i++) {
            std::string& p = m_params[i];

            // memory leak in child process, it's ok.
            char* v = new char[p.length() + 1];
            argv[i] = strcpy(v, p.data());
        }
        argv[m_params.size()] = NULL;

        // use execv to start the program.
        int r0 = execv(m_bin.c_str(), argv);
        if (r0 < 0) {
            fprintf(stderr, "fork process failed, errno=%d(%s)", errno, strerror(errno));
        }
        exit(r0);
    }

    // parent.
    if (m_pid > 0) {
        // Wait for a while for process to really started.
        // @see https://github.com/ossrs/srs/issues/1634#issuecomment-597568840
        Usleep(10 * UTIME_MILLISECONDS);

        m_isStarted = true;
        trace("fored process, pid=%d, bin=%s, stdout=%s, stderr=%s, argv=%s",
                  m_pid, m_bin.c_str(), m_stdoutFile.c_str(), m_stderrFile.c_str(), m_actualCli.c_str());
        return err;
    }

    return err;
}

error Process::Cycle()
{
    error err = SUCCESS;

    if (!m_isStarted) {
        return err;
    }

    // ffmpeg is prepare to stop, donot cycle.
    if (m_fastStopped) {
        return err;
    }

    int status = 0;
    pid_t p = waitpid(m_pid, &status, WNOHANG);

    if (p < 0) {
        return ERRORNEW(ERROR_SYSTEM_WAITPID, "process waitpid failed, pid=%d", m_pid);
    }

    if (p == 0) {
        info("process process pid=%d is running.", m_pid);
        return err;
    }

    trace("process pid=%d terminate, please restart it.", m_pid);
    m_isStarted = false;

    return err;
}

void Process::Stop()
{
    if (!m_isStarted) {
        return;
    }

    // kill the ffmpeg,
    // when rewind, upstream will stop publish(unpublish),
    // unpublish event will stop all ffmpeg encoders,
    // then publish will start all ffmpeg encoders.
    error err = KillForced(m_pid);
    if (err != SUCCESS) {
        warn("ignore kill the process failed, pid=%d. err=%s", m_pid, ERRORDESC(err).c_str());
        Freep(err);
        return;
    }

    // terminated, set started to false to stop the cycle.
    m_isStarted = false;
}

void Process::FastStop()
{
    int ret = ERROR_SUCCESS;

    if (!m_isStarted) {
        return;
    }

    if (m_pid <= 0) {
        return;
    }

    if (kill(m_pid, SIGTERM) < 0) {
        ret = ERROR_SYSTEM_KILL;
        warn("ignore fast stop process failed, pid=%d. ret=%d", m_pid, ret);
        return;
    }

    return;
}

void Process::FastKill()
{
    int ret = ERROR_SUCCESS;

    if (!m_isStarted) {
        return;
    }

    if (m_pid <= 0) {
        return;
    }

    if (kill(m_pid, SIGKILL) < 0) {
        ret = ERROR_SYSTEM_KILL;
        warn("ignore fast kill process failed, pid=%d. ret=%d", m_pid, ret);
        return;
    }

    // Try to wait pid to avoid zombie FFMEPG.
    int status = 0;
    waitpid(m_pid, &status, WNOHANG);

    return;
}
