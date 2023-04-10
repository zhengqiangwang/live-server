#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <string.h>
#include "st/st.h"
#include <sys/wait.h>
#include <vector>
#include "app_thread.h"
#include "rtmpprotocol.h"
#include "log.h"
#include "error.h"
#include "utility.h"
#include "app_config.h"
#include "app_hybrid.h"
#include "protocol_log.h"
#include "app_server.h"
#include "error.h"

error run_hybrid_server(void* /*arg*/);
error ThreadRun();

//@global log and context
ILog* Log = nullptr;

//it should be thread-safe, because it use thread-local thread private data
IContext* Context = nullptr;

//@global config object for app module
Config* config = nullptr;

//@global version
extern const char* _version;

//@global main sever, for debugging
Server* Server = nullptr;

int main(int argc, char** argv)
{
    std::cout<<"wang"<<std::endl;
    error err = SUCCESS;

    if ((err = GlobalInitialize()) != SUCCESS) {
        std::cerr<<"global initialize failed"<<std::endl;
        return -1;
    }

    if ((err = ThreadPool::SetupThreadLocals()) != SUCCESS) {
        std::cerr<<"setup thread locals"<<std::endl;
        return -1;
    }


    Context->SetId(Context->GenerateId());

    Assert(IsLittleEndian());

    if ((err = config->ParseOptions(argc, argv)) != SUCCESS) {
        std::cerr<<"config parse options"<<std::endl;
        return -1;
    }

    int r0 = 0;
    std::string cwd = config->GetWorkDir();
    if (!cwd.empty() && cwd != "./" && (r0 = chdir(cwd.c_str())) == -1) {
        std::cerr<<"chdir to "<<cwd<<",r0="<<r0<<std::endl;
        return -1;
    }
    if ((err = config->InitializeCwd()) != SUCCESS) {
        std::cerr<<"config cwd"<<std::endl;
        return -1;
    }

    // config parsed, initialize log.
    if ((err = Log->Initialize()) != SUCCESS) {
        std::cerr<<"log initialize"<<std::endl;
        return -1;
    }

    if ((err = config->CheckConfig()) != SUCCESS) {
        std::cerr<<"check config"<<std::endl;
        return -1;
    }

    bool run_as_daemon = config->GetDaemon();
    if(!run_as_daemon)
    {
        ThreadRun();
        return 0;
    }
    trace("start daemon mode...");

    int pid = fork();

    if(pid < 0) {
        return -1;
    }

    // grandpa
    if(pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        trace("grandpa process exit.");
        exit(0);
    }

    // father
    pid = fork();

    if(pid < 0) {
        return -1;
    }

    if(pid > 0) {
        trace("father process exit");
        exit(0);
    }

    ThreadRun();

    return 0;
}

error ThreadRun()
{
    error err = SUCCESS;
    // son
    trace("son(daemon) process running.");
    if ((err = thread_pool->Initialize()) != SUCCESS) {
        std::cerr<<"init thread pool"<<std::endl;
        return err;
    }

    if ((err = thread_pool->Execute("hybrid", run_hybrid_server, (void*)NULL)) != SUCCESS) {
        std::cerr<<"start hybrid server thread"<<std::endl;
        return err;
    }
    thread_pool->Run();

    return err;
}

error run_hybrid_server(void* /*arg*/)
{
    error err = SUCCESS;

    // Create servers and register them.
    hybrid->RegisterServer(new ServerAdapter());

#ifdef SRS_SRT
    _srs_hybrid->register_server(new SrsSrtServerAdapter());
#endif

#ifdef SRS_RTC
    _srs_hybrid->register_server(new RtcServerAdapter());
#endif

    // Do some system initialize.
    if ((err = hybrid->Initialize()) != SUCCESS) {
        return ERRORWRAP(err, "hybrid initialize");
    }

    // Circuit breaker to protect server, which depends on hybrid.
    if ((err = circuit_breaker->Initialize()) != SUCCESS) {
        return ERRORWRAP(err, "init circuit breaker");
    }

    // When startup, create a span for server information.
//    ISrsApmSpan* span = _srs_apm->span("main")->set_kind(SrsApmKindServer);
//    srs_freep(span);

    // Should run util hybrid servers all done.
    if ((err = hybrid->Run()) != SUCCESS) {
        return ERRORWRAP(err, "hybrid run");
    }

    // After all done, stop and cleanup.
    hybrid->Stop();

    return err;
}





