#ifndef APP_ENCODER_H
#define APP_ENCODER_H


#include "app_st.h"
#include "log.h"
#include <vector>

class ConfDirective;
class Request;
class PithyPrint;
class FFMPEG;

// The encoder for a stream, may use multiple
// ffmpegs to transcode the specified stream.
class Encoder : public ICoroutineHandler
{
private:
    std::string m_inputStreamName;
    std::vector<FFMPEG*> m_ffmpegs;
private:
    Coroutine* m_trd;
    PithyPrint* m_pprint;
public:
    Encoder();
    virtual ~Encoder();
public:
    virtual error OnPublish(Request* req);
    virtual void OnUnpublish();
// Interface ISrsReusableThreadHandler.
public:
    virtual error Cycle();
private:
    virtual error DoCycle();
private:
    virtual void ClearEngines();
    virtual FFMPEG* At(int index);
    virtual error ParseScopeEngines(Request* req);
    virtual error ParseFfmpeg(Request* req, ConfDirective* conf);
    virtual error InitializeFfmpeg(FFMPEG* ffmpeg, Request* req, ConfDirective* engine);
    virtual void ShowEncodeLogMessage();
};
#endif // APP_ENCODER_H
