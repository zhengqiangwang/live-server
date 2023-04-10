#ifndef APP_FFMPEG_H
#define APP_FFMPEG_H



#include "log.h"
#include <vector>
#include <string>

class ConfDirective;
class PithyPrint;
class Process;

// A transcode engine: ffmepg, used to transcode a stream to another.
class FFMPEG
{
private:
    Process* m_process;
    std::vector<std::string> m_params;
private:
    std::string m_logFile;
private:
    std::string                 m_ffmpeg;
    std::vector<std::string>    m_iparams;
    std::vector<std::string>    m_perfile;
    std::string                 m_iformat;
    std::string                 m_input;
    std::vector<std::string>    m_vfilter;
    std::string                 m_vcodec;
    int                         m_vbitrate;
    double                      m_vfps;
    int                         m_vwidth;
    int                         m_vheight;
    int                         m_vthreads;
    std::string                 m_vprofile;
    std::string                 m_vpreset;
    std::vector<std::string>    m_vparams;
    std::string                 m_acodec;
    int                         m_abitrate;
    int                         m_asampleRate;
    int                         m_achannels;
    std::vector<std::string>    m_aparams;
    std::string                 m_oformat;
    std::string                 m_output;
public:
    FFMPEG(std::string ffmpeg_bin);
    virtual ~FFMPEG();
public:
    virtual void AppendIparam(std::string iparam);
    virtual void SetOformat(std::string format);
    virtual std::string Output();
public:
    virtual error Initialize(std::string in, std::string out, std::string log);
    virtual error InitializeTranscode(ConfDirective* engine);
    virtual error InitializeCopy();
public:
    virtual error Start();
    virtual error Cycle();
    virtual void Stop();
public:
    virtual void FastStop();
    virtual void FastKill();
};


#endif // APP_FFMPEG_H
