#ifndef APP_SERVICE_CONN_H
#define APP_SERVICE_CONN_H

#include "app_conn.h"
#include "app_hourglass.h"
#include "protocol_conn.h"
#include "app_st.h"
#include "app_reload.h"
#include "protocol_st.h"
#include "protocol_kbps.h"
#include <queue>
#include <set>

class Server;
class Course;
class Database;
class Netizen;
class NetworkDelta;

class ServiceConn : public IConnection, public IStartable, public IReloadHandler, public ICoroutineHandler
{
private:
    Server* m_server;
    netfd_t m_stfd;
    TcpConnection* m_skt;
    //each connection start a green thread,
    //when thread stop, the connection will be delete by server
    Coroutine* m_trd;
    // The manager object to manage the connection.
    IResourceManager* m_manager;
    Netizen* m_netizen;
    //the ip and port of client
    std::string m_ip;
    int m_port;
    //the create time in milliseconds.
    //for current connection to log self create time and calculate the living time
    int64_t m_createTime;
    Buffer* m_cacheBuffer;
private:
    char* m_buf;
    int m_nbBuf;
    // The delta for statistic.
    NetworkDelta* m_delta;
public:
    ServiceConn(Server* svr, netfd_t c, std::string cip, int port);
    virtual ~ServiceConn();
public:
    error ReadPacket(std::string &packet);
    error SendPacket(std::string &packet);
    error Process(std::string &packet);
public:
    virtual std::string Desc();
protected:
    virtual error DoCycle();
private:
    //when the connection disconnect, call this method.
    //e.g. log msg of connection and report to other system.
    virtual error OnDisconnect();
public:
    // Start the client green thread.
    // when server get a client from listener,
    // 1. server will create an concrete connection(for instance, service connection),
    // 2. then add connection to its connection manager,
    // 3. start the client thread by invoke this start()
    // when client cycle thread stop, invoke the OnThreadStop(), which will use server
    // To remove the client by server->remove(this).
    virtual error Start();
    //interface IOneCycleThreadHandler
public:
    // The thread cycle function,
    // when serve connection completed, terminate the loop which will terminate the thread,
    // thread will invoke the OnThreadStop() when it terminated.
    virtual error Cycle();
    // Interface IConnection.
public:
    virtual std::string RemoteIp();
    virtual const ContextId& GetId();
public:
    IKbpsDelta* Delta();
};

class Netizen
{
private:
    std::string m_name = "";
    std::string m_account = "";
    std::string m_role = "";
    std::map<std::string, Course*> m_courses;
    Course* m_course = nullptr;
    Database* m_database = nullptr;
    ServiceConn* m_conn = nullptr;
public:
    Netizen(ServiceConn *conn);
    virtual ~Netizen();
public:
    //用户注册
    error Register(std::string name, std::string password, std::string identification);
    //用户登录
    error Login(std::string id, std::string password);
    //用户下线
    error Logout();
    //获取基本信息
    error UpdateCourseList();
    //教师创建课程
    error CreateCourse(std::string courseName);
    //学生加入课程
    error JoinCourse(std::string courseId);
    //学生退选课程
    error DeselectionCourse(std::string courseId);
    //教师删除课程
    error DeleteCourse(std::string courseId);
    //查询课程细节
    error UpdateClassList(std::string courseId);
    //刷新课程状态
    error RefreshCourseStatus();
    //教师开始上课
    error InitClassroom(std::string courseId);
    //发送聊天信息
    error SendChatMessage(std::string message, std::string cid);
    //学生进入课堂
    error JoinClass(std::string courseId);
    //学生退出课堂
    error ExitClass(std::string courseId);
    //教师关闭课堂
    error CloseClassroom(std::string courseId);
public:
    error SendChat(std::vector<std::string> message);
    error EnterClassNotify(std::string cid, std::string uid, std::string uname);
    error ExitClassNotify(std::string cid, std::string uid, std::string uname);
    error OpenClassNotify(std::string cid, std::string cname);
    error CloseClassNotify(std::string cid, std::string cname);
    error DeleteCourseNotify(std::string cid, Course *course);
    error SendClassNotification(std::string courseId, std::string state);
public:
    std::string GetName();
    std::string GetRole();
    std::string GetId();
};

class CourseManager : public IHourGlass
{
private:
    mutex_t m_lock;
    std::map<std::string, Course*> m_pool;
    HourGlass* m_timer;
public:
    CourseManager();
    virtual ~CourseManager();
public:
    virtual error Initialize();
    //  create source when fetch from cache failed.
    // @param r the client request.
    // @param h the event handler for source.
    // @param pps the matched source, if success never be NULL.
    virtual error FetchOrCreate(std::string courseId, Course** course);
public:
    // Get the exists source, NULL when not exists.
    virtual Course* Fetch(std::string courseId);
public:
    // dispose and cycle all sources.
    virtual void Dispose();
// interface IHourGlass
private:
    virtual error SetupTicks();
    virtual error Notify(int event, utime_t interval, utime_t tick);
public:
    // when system exit, destroy th`e sources,
    // For gmc to analysis mem leaks.
    virtual void Destroy();
};

extern CourseManager* courses;

class Course : public ICoroutineHandler
{
private:
    mutex_t m_ulock;
    mutex_t m_mlock;
    mutex_t m_llock;
    std::vector<Netizen*> m_logon; //登录系统的用户
    std::vector<Netizen*> m_netizens; //进入课堂的用户
    std::queue<std::vector<std::string>> m_message;
    std::string m_id = "";
    Coroutine* m_trd = nullptr;
    std::string m_state = "off";
    std::string m_tid = "";
    std::string m_tname = "";
    std::string m_name = "";
    std::string m_streamAddress = "";
    std::string m_classId = "";
    std::vector<std::vector<std::string>> m_classInfo;
public:
    Course(std::string id);
    virtual ~Course();
public:
    error Init(std::string cname, std::string tid, std::string tname, std::vector<std::vector<std::string>> classinfo);
    error Enqueue(std::vector<std::string>);
    error DistributeChat();
    error EnterCourse(Netizen* netizen);
    error ExitCourse(Netizen* netizen);
    error Login(Netizen* netizen);
    error Logout(Netizen* netizen);
    error DeleteCourse();
    error ClassBegin(Netizen* netizen);
    error ClassEnd(Netizen* netizen);
    //返回在线人数
    int Logins();
    //返回课程的id, state, tid, tname, name, classid, streamAddress
    std::vector<std::string> BaseMessage();
    std::string GetStreamAddress();
    std::string GetName();
    std::string GetTid();
    std::string GetTname();
    std::string GetClassId();
    std::string GetId();
    std::string GetState();
    std::vector<std::vector<std::string> > GetClassInfo();
    std::vector<std::vector<std::string>> GetClassMembers();
public:
    // Start the client green thread.
    // when class begin,
    // 1. server will create an concrete connection(for instance, service connection),
    // 2. then add connection to its connection manager,
    // 3. start the client thread by invoke this start()
    // when client cycle thread stop, invoke the OnThreadStop(), which will use server
    // To remove the client by server->remove(this).
    virtual error Start();
    // Stop the client green thread.
    // when class end,
    virtual void Stop();
    //interface IOneCycleThreadHandler
public:
    // The thread cycle function,
    // when serve connection completed, terminate the loop which will terminate the thread,
    // thread will invoke the OnThreadStop() when it terminated.
    virtual error Cycle();
    virtual void Dispose();
};

#endif // APP_SERVICE_CONN_H
