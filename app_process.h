#ifndef APP_PROCESS_H
#define APP_PROCESS_H

#include "core.h"
#include <string>
#include <vector>

// Start and stop a process. Call cycle to restart the process when terminated.
// The usage:
//      // the binary is the process to fork.
//      binary = "./objs/ffmpeg/bin/ffmpeg";
//      // where argv is a array contains each params.
//      argv = ["./objs/ffmpeg/bin/ffmpeg", "-i", "in.flv", "1", ">", "/dev/null", "2", ">", "/dev/null"];
//
//      process = new SrsProcess();
//      if ((ret = process->initialize(binary, argv)) != ERROR_SUCCESS) { return ret; }
//      if ((ret = process->start()) != ERROR_SUCCESS) { return ret; }
//      if ((ret = process->cycle()) != ERROR_SUCCESS) { return ret; }
//      process->fast_stop();
//      process->stop();
class Process
{
private:
    bool m_isStarted;
    // Whether SIGTERM send but need to wait or SIGKILL.
    bool m_fastStopped;
    pid_t m_pid;
private:
    std::string m_bin;
    std::string m_stdoutFile;
    std::string m_stderrFile;
    std::vector<std::string> m_params;
    // The cli to fork process.
    std::string m_cli;
    std::string m_actualCli;
public:
    Process();
    virtual ~Process();
public:
    // Get pid of process.
    virtual int GetPid();
    // whether process is already started.
    virtual bool Started();
    // Initialize the process with binary and argv.
    // @param binary the binary path to exec.
    // @param argv the argv for binary path, the argv[0] generally is the binary.
    // @remark the argv[0] must be the binary.
    virtual error Initialize(std::string binary, std::vector<std::string> argv);
private:
    // Redirect standard I/O.
    virtual error RedirectIo();
public:
    // Start the process, ignore when already started.
    virtual error Start();
    // cycle check the process, update the state of process.
    // @remark when process terminated(not started), user can restart it again by start().
    virtual error Cycle();
    // Send SIGTERM then SIGKILL to ensure the process stopped.
    // the stop will wait [0, SRS_PROCESS_QUIT_TIMEOUT_MS] depends on the
    // process quit timeout.
    // @remark use fast_stop before stop one by one, when got lots of process to quit.
    virtual void Stop();
public:
    // The fast stop is to send a SIGTERM.
    // for example, the ingesters owner lots of FFMPEG, it will take a long time
    // to stop one by one, instead the ingesters can fast_stop all FFMPEG, then
    // wait one by one to stop, it's more faster.
    // @remark user must use stop() to ensure the ffmpeg to stopped.
    // @remark we got N processes to stop, compare the time we spend,
    //      when use stop without fast_stop, we spend maybe [0, SRS_PROCESS_QUIT_TIMEOUT_MS * N]
    //      but use fast_stop then stop, the time is almost [0, SRS_PROCESS_QUIT_TIMEOUT_MS].
    virtual void FastStop();
    // Directly kill process, never use it except server quiting.
    virtual void FastKill();
};

#endif // APP_PROCESS_H
