#include "app_service_conn.h"
#include "app_config.h"
#include "app_server.h"
#include "buffer.h"
#include "database.h"
#include "utility.h"
#include "core_autofree.h"
#include "protocol_json.h"
#include "types.h"

// set the max packet size.
#define MAX_PACKET_SIZE 65535

ServiceConn::ServiceConn(Server *svr, netfd_t c, std::string cip, int port)
{
    // Create a identify for this client.
    Context->SetId(Context->GenerateId());

    m_server = svr;
    m_nbBuf = MAX_PACKET_SIZE;
    m_buf = new char[m_nbBuf];
    m_stfd = c;
    m_skt = new TcpConnection(c);
    m_manager = svr;
    m_ip = cip;
    m_port = port;
    m_createTime = u2ms(GetSystemTime());
    m_trd = new STCoroutine("service", this, Context->GetId());
    m_netizen = new Netizen(this);
    m_cacheBuffer = new Buffer(m_buf, 0);
    m_delta = new NetworkDelta();
    m_delta->SetIo(m_skt, m_skt);
    config->Subscribe(this);
}

ServiceConn::~ServiceConn()
{
    config->Unsubscribe(this);
    m_trd->Interrupt();
    Freep(m_trd);

    Freep(m_skt);
    Freep(m_netizen);
    Freepa(m_buf);
    Freep(m_cacheBuffer);
    Freep(m_delta);
}

error ServiceConn::ReadPacket(std::string &packet)
{
    error err = SUCCESS;
    ssize_t nread = 0;
    uint32_t len = 0;
    std::string tpacket = "";
    packet = tpacket;
    int relen = m_cacheBuffer->Remain();
    int pos = m_cacheBuffer->Pos();
    if(relen > 0)
    {
        m_cacheBuffer->Skip((pos * -1));
        m_cacheBuffer->WriteBytes(m_buf + pos, relen);
    } else {
        m_cacheBuffer->Skip(((m_cacheBuffer->Pos()) * -1));
    }
    if((err = m_skt->Read(m_buf + relen, m_nbBuf - relen, &nread)) != SUCCESS){
        trace("read data error");
        return ERRORWRAP(err, "read");
    }

    if(nread == 0)
    {
        return err;
    }
    m_cacheBuffer->SetSize(relen + nread);

    if(m_cacheBuffer->Require(4))
    {
        len = m_cacheBuffer->Read4Bytes();
    } else {
        return err;
    }

    tpacket.resize(len);
    if(m_cacheBuffer->Require(len))
    {
        m_cacheBuffer->ReadBytes(tpacket.data(), len);
        packet = tpacket;
    } else {
        int rlen = m_cacheBuffer->Remain();
        m_cacheBuffer->ReadBytes(tpacket.data(), rlen);
        m_cacheBuffer->Skip(((m_cacheBuffer->Pos()) * -1));
        len -= rlen;
        int blen = 0;
        while(len > 0){
            if((err = m_skt->Read(m_buf, m_nbBuf, &nread)) != SUCCESS){
                trace("read data error");
                return ERRORWRAP(err, "read");
            }
            m_cacheBuffer->SetSize(nread);
            if(nread == 0)
            {
                return err;
            }
            blen = m_cacheBuffer->Remain();
            if(len > blen)
            {
                m_cacheBuffer->ReadBytes(tpacket.data() + rlen, blen);
                m_cacheBuffer->Skip((blen * -1));
                len -= blen;
                rlen += blen;
            } else {
                m_cacheBuffer->ReadBytes(tpacket.data() + rlen, len);
                break;
            }

        }
        packet = tpacket;
    }

    return err;
}

error ServiceConn::SendPacket(std::string &packet)
{
    error err = SUCCESS;
    uint32_t len = packet.size();
    char* buf = new char[len + 4];
    Buffer* buffer = new Buffer(buf, len + 4);
    AutoFree(Buffer, buffer);

    buffer->Write4Bytes(len);
    buffer->WriteString(packet);

    ssize_t nwrite = 0;
    if((err = m_skt->Write(buf, len + 4, &nwrite)) != SUCCESS)
    {
        trace("send message failed");
    }
    return err;
}

error ServiceConn::Process(std::string &packet)
{
    error err = SUCCESS;
    JsonAny* message = JsonAny::Loads(packet);
    if(!message){
        return ERRORNEW(ERROR_SYSTEM_FORWARD_LOOP, "load json from %s", packet.c_str());
    }
    AutoFree(JsonAny, message);

    if(!message->IsObject()){
        return ERRORNEW(ERROR_SYSTEM_FORWARD_LOOP, "load json from %s", packet.c_str());
    }

    JsonAny* prop = nullptr;
    JsonObject* info = message->ToObject();
    trace("json object count:%d",info->Count());

    for(int i = 0; i < info->Count(); i++)
    {
        if(info->KeyAt(i) == "type")
        {
            prop = info->ValueAt(i);
        }
    }
    int type = prop->ToInteger();
    for(int i = 0; i < info->Count(); i++)
    {
        if(info->KeyAt(i) == "data")
        {
            prop = info->ValueAt(i);
        }
    }
    switch(type)
    {
    case MessageType::Register :
    {
        info = prop->ToObject();
        std::string name = "";
        std::string password = "";
        std::string identification = "";
        for(int i = 0; i < info->Count(); i++)
        {
            if(info->KeyAt(i) == "user_name")
            {
                prop = info->ValueAt(i);
                name = prop->ToStr();
            }
            if(info->KeyAt(i) == "password")
            {
                prop = info->ValueAt(i);
                password = prop->ToStr();
            }
            if(info->KeyAt(i) == "identification")
            {
                prop = info->ValueAt(i);
                identification = prop->ToStr();
            }
        }

        m_netizen->Register(name, password, identification);
    }
        break;
    case  MessageType::Login:
    {
        info = prop->ToObject();
        std::string id = "";
        std::string password = "";
        for(int i = 0; i < info->Count(); i++)
        {
            if(info->KeyAt(i) == "user_id")
            {
                prop = info->ValueAt(i);
                id = prop->ToStr();
            }
            if(info->KeyAt(i) == "password")
            {
                prop = info->ValueAt(i);
                password = prop->ToStr();
            }
        }

        m_netizen->Login(id, password);
    }
        break;
    case MessageType::UpdateCourseList :
    {
        m_netizen->UpdateCourseList();
    }
        break;
    case MessageType::JoinCourse :
    {
        info = prop->ToObject();
        std::string cid = "";
        prop = info->ValueAt(0);
        cid = prop->ToStr();
        m_netizen->JoinCourse(cid);
    }
        break;
    case MessageType::CreateCourse :
    {
        info = prop->ToObject();
        std::string cname = "";
        prop = info->ValueAt(0);
        cname = prop->ToStr();
        m_netizen->CreateCourse(cname);
    }
        break;
    case MessageType::DeselectionCourse :
    {
        info = prop->ToObject();
        std::string cid = "";
        prop = info->ValueAt(0);
        cid = prop->ToStr();
        m_netizen->DeselectionCourse(cid);
    }
        break;
    case MessageType::DeleteCourse :
    {
        info = prop->ToObject();
        std::string cid = "";
        prop = info->ValueAt(0);
        cid = prop->ToStr();
        m_netizen->DeleteCourse(cid);
    }
        break;
    case MessageType::UpdateClassList :
    {
        info = prop->ToObject();
        std::string cid = "";
        prop = info->ValueAt(0);
        cid = prop->ToStr();
        m_netizen->UpdateClassList(cid);
    }
        break;
        //    case MessageType::SendChatMessage :
        //    {
        //        info = prop->ToObject();
        //        std::string cid = "";
        //        std::string message = "";
        //        for(int i = 0; i < info->Count(); i++)
        //        {
        //            if(info->KeyAt(i) == "course_id")
        //            {
        //                prop = info->ValueAt(i);
        //                cid = prop->ToStr();
        //            }
        //            if(info->KeyAt(i) == "message")
        //            {
        //                prop = info->ValueAt(i);
        //                message = prop->ToStr();
        //            }
        //        }

        //        m_netizen->SendChatMessage(message, cid);
        //    }
        //        break;
    case MessageType::JoinClass :
    {
        info = prop->ToObject();
        std::string cid = "";
        std::string uid = "";
        for(int i = 0; i < info->Count(); i++)
        {
            if(info->KeyAt(i) == "course_id")
            {
                prop = info->ValueAt(i);
                cid = prop->ToStr();
            }
            if(info->KeyAt(i) == "user_id")
            {
                prop = info->ValueAt(i);
                uid = prop->ToStr();
            }
        }

        m_netizen->JoinClass(cid);
    }
        break;
    case MessageType::ExitClass :
    {
        info = prop->ToObject();
        std::string cid = "";
        std::string uid = "";
        for(int i = 0; i < info->Count(); i++)
        {
            if(info->KeyAt(i) == "course_id")
            {
                prop = info->ValueAt(i);
                cid = prop->ToStr();
            }
            if(info->KeyAt(i) == "user_id")
            {
                prop = info->ValueAt(i);
                uid = prop->ToStr();
            }
        }
        m_netizen->ExitClass(cid);
    }
        break;
        //    case MessageType::CloseClassroom :
        //    {
        //        info = prop->ToObject();
        //        std::string cid = "";
        //        prop = info->ValueAt(0);
        //        cid = prop->ToStr();
        //        m_netizen->CloseClassroom(cid);
        //    }
        break;
        //    case MessageType::GetCoursesAbstract :
        //    {

        //    }
        //        break;
        //    case MessageType::GetClassInfo :
        //    {

        //    }
        //        break;
        //    case MessageType::SetStudentInfo :
        //    {

        //    }
        //        break;
        //    case MessageType::AddClassInfo :
        //    {

        //    }
        //        break;
    }

    return err;
}

std::string ServiceConn::Desc()
{
    return "SERVICE";
}

error ServiceConn::DoCycle()
{
    error err = SUCCESS;

    trace("peer connected ip: %s ", RemoteIp().c_str());
    std::string data = "";
    while (true) {
        ReadPacket(data);
        if(data.size() == 0)
        {
            m_netizen->Logout();
            break;
        }
        trace("msg: %s", data.c_str());
        Process(data);
    }
    error r0 = SUCCESS;
    if((r0 = OnDisconnect()) != SUCCESS){
        err = ERRORWRAP(err, "on disconnect %s", ERRORDESC(r0).c_str());
        Freep(r0);
    }

    return err;
}

error ServiceConn::OnDisconnect()
{
    error err = SUCCESS;

    return err;
}

error ServiceConn::Start()
{
    error err = SUCCESS;

    if((err = m_trd->Start()) != SUCCESS){
        return ERRORWRAP(err, "coroutine");
    }

    return err;
}

error ServiceConn::Cycle()
{
    error err = SUCCESS;

    err = DoCycle();

    m_manager->Remove(this);

    //success
    if(err == SUCCESS){
        trace("client finished.");
        return err;
    }

    //it maybe success with message.
    if(ERRORCODE(err) == ERROR_SUCCESS){
        trace("client finished%s.", ERRORSUMMARY(err).c_str());
        Freep(err);
        return err;
    }

    // client close peer.
    // TODO: FIXME: Only reset the error when client closed it.
    if (IsClientGracefullyClose(err)) {
        warn("client disconnect peer. ret=%d", ERRORCODE(err));
    } else if (IsServerGracefullyClose(err)) {
        warn("server disconnect. ret=%d", ERRORCODE(err));
    } else {
        ERROR("serve error %s", ERRORDESC(err).c_str());
    }

    return err;
}

std::string ServiceConn::RemoteIp()
{
    return m_ip;
}

const ContextId &ServiceConn::GetId()
{
    return m_trd->Cid();
}

IKbpsDelta *ServiceConn::Delta()
{
    return m_delta;
}

Netizen::Netizen(ServiceConn* conn) : m_conn{conn}
{
    m_database = Database::Instance();
    m_course = nullptr;
}

Netizen::~Netizen()
{

}

error Netizen::Register(std::string name, std::string password, std::string identification)
{
    error err = SUCCESS;

    std::string result = "";
    result = m_database->Register(name, password, identification);
    JsonObject* obj = JsonAny::Object();
    obj->Set("type", JsonAny::Integer(MessageType::RegisterResult));
    if(result == ""){
        JsonObject* data = JsonAny::Object();
        data->Set("result", JsonAny::Str("failed"));
        obj->Set("data", data);
    } else {
        JsonObject* data = JsonAny::Object();
        data->Set("result", JsonAny::Str("success"));
        data->Set("user_id", JsonAny::Str(result.c_str()));
        obj->Set("data", data);
    }

    result = obj->Dumps();
    m_conn->SendPacket(result);

    return err;
}

error Netizen::Login(std::string id, std::string password)
{
    error err = SUCCESS;

    std::string result = "";
    result = m_database->Login(id, password, m_role, m_name);
    JsonObject* obj = JsonAny::Object();
    obj->Set("type", JsonAny::Integer(MessageType::LoginResult));
    if(result == "success"){
        m_account = id;
        std::vector<std::vector<std::string>> result;
        result = m_database->QueryCourses(id);

        Course* course = nullptr;
        for(auto &m : result){
            courses->FetchOrCreate(m[0], &course);
            if(course)
            {
                if(course->Logins() == 0)
                {
                    std::vector<std::vector<std::string>> classinfo = m_database->GetClassInfo(m[0]);
                    course->Init(m[1], m[2], m[3], classinfo);
                }
                course->Login(this);
                m_courses.emplace(m[0], course);
            }
        }
        JsonObject* data = JsonAny::Object();
        data->Set("result", JsonAny::Str("success"));
        obj->Set("data", data);
    } else {
        JsonObject* data = JsonAny::Object();
        data->Set("result", JsonAny::Str("failed"));
        data->Set("reason", JsonAny::Str(result.c_str()));
        obj->Set("data", data);
    }

    result = obj->Dumps();
    m_conn->SendPacket(result);

    return err;
}

error Netizen::Logout()
{
    error err = SUCCESS;

    m_database->Logout(m_account);
    for(auto &m : m_courses)
    {
        m.second->Logout(this);
    }

    return err;
}

error Netizen::UpdateCourseList()
{
    error err = SUCCESS;

    JsonObject* obj = JsonAny::Object();
    obj->Set("type", JsonAny::Integer(MessageType::UpdateCourseListResult));
    JsonObject* data = JsonAny::Object();
    JsonArray* courses_info = JsonAny::Array();
    std::vector<std::string> message;
    for(auto& m: m_courses)
    {
        message = m.second->BaseMessage();
        JsonObject* course_info = JsonAny::Object();
        course_info->Set("id", JsonAny::Str(message[0].c_str()));
        course_info->Set("name", JsonAny::Str(message[1].c_str()));
        course_info->Set("status", JsonAny::Boolean(message[2] == "on"));
        course_info->Set("master", JsonAny::Str(message[3].c_str()));
        course_info->Set("stream_address", JsonAny::Str(message[4].c_str()));

        courses_info->Add(course_info);
    }
    data->Set("courses", courses_info);
    obj->Set("data", data);

    std::string result = obj->Dumps();
    m_conn->SendPacket(result);
    return err;
}

error Netizen::CreateCourse(std::string courseName)
{
    error err = SUCCESS;

    JsonObject* obj = JsonAny::Object();
    obj->Set("type", JsonAny::Integer(MessageType::CreateCourseResult));
    JsonObject* data = JsonAny::Object();
    if(m_role == "teacher")
    {
        std::string result = "";
        result = m_database->CreateCourse(m_account, m_name, courseName);
        if(result != ""){
            Course* course = nullptr;
            courses->FetchOrCreate(result, &course);
            if(course){
                std::vector<std::vector<std::string>> classinfo;
                course->Init(courseName, m_account, m_name, classinfo);
                course->Login(this);
                m_courses[result] = course;
            }
            data->Set("result", JsonAny::Str("success"));
            JsonArray* courses_info = JsonAny::Array();
            std::vector<std::string> message;
            for(auto& m: m_courses)
            {
                message = m.second->BaseMessage();
                JsonObject* course_info = JsonAny::Object();
                course_info->Set("id", JsonAny::Str(message[0].c_str()));
                course_info->Set("name", JsonAny::Str(message[1].c_str()));
                course_info->Set("status", JsonAny::Boolean(message[2] == "on"));
                course_info->Set("master", JsonAny::Str(message[3].c_str()));
                course_info->Set("stream_address", JsonAny::Str(message[4].c_str()));

                courses_info->Add(course_info);
            }
            data->Set("courses", courses_info);
        } else {
            data->Set("result", JsonAny::Str("failed"));
            data->Set("reason", JsonAny::Str("data error"));
        }
    } else {
        data->Set("result", JsonAny::Str("failed"));
        data->Set("reason", JsonAny::Str("identification is error"));
    }

    obj->Set("data", data);
    std::string result = obj->Dumps();
    m_conn->SendPacket(result);
    return err;
}

error Netizen::JoinCourse(std::string courseId)
{
    error err = SUCCESS;

    JsonObject* obj = JsonAny::Object();
    obj->Set("type", JsonAny::Integer(MessageType::JoinCourseResult));
    JsonObject* data = JsonAny::Object();

    if(m_courses.count(courseId) == 0){
        std::string result = "";
        std::vector<std::string> coursemessage;
        result = m_database->AddCourse(m_account, courseId, coursemessage);
        if(result == "success")
        {
            Course* course = nullptr;
            courses->FetchOrCreate(courseId, &course);
            if(course){
                std::vector<std::vector<std::string>> classinfo;
                classinfo = m_database->GetClassInfo(courseId);
                m_courses[courseId] = course;
                course->Login(this);
                course->Init(coursemessage[0], coursemessage[1], coursemessage[2], classinfo);
            }
            JsonArray* courses_info = JsonAny::Array();
            std::vector<std::string> message;
            for(auto& m: m_courses)
            {
                message = m.second->BaseMessage();
                JsonObject* course_info = JsonAny::Object();
                course_info->Set("id", JsonAny::Str(message[0].c_str()));
                course_info->Set("name", JsonAny::Str(message[1].c_str()));
                course_info->Set("status", JsonAny::Boolean(message[2] == "on"));
                course_info->Set("master", JsonAny::Str(message[3].c_str()));
                course_info->Set("stream_address", JsonAny::Str(message[4].c_str()));

                courses_info->Add(course_info);
            }
            data->Set("courses", courses_info);
            data->Set("result", JsonAny::Str("success"));
        } else {
            data->Set("result", JsonAny::Str("failed"));
            data->Set("reason", JsonAny::Str(result.c_str()));
        }
    } else {

    }
    obj->Set("data", data);
    std::string result = obj->Dumps();
    m_conn->SendPacket(result);

    return err;
}

error Netizen::DeselectionCourse(std::string courseId)
{
    error err = SUCCESS;
    JsonObject* obj = JsonAny::Object();
    obj->Set("type", JsonAny::Integer(MessageType::DeselectionCourseResult));
    JsonObject* data = JsonAny::Object();

    if(m_courses[courseId]->GetTid() != m_account){
        m_database->DeselectionCourse(m_account, courseId);
        Course* course = nullptr;
        course = m_courses[courseId];
        course->Logout(this);
        JsonArray* courses_info = JsonAny::Array();
        std::vector<std::string> message;
        for(auto& m: m_courses)
        {
            message = m.second->BaseMessage();
            JsonObject* course_info = JsonAny::Object();
            course_info->Set("id", JsonAny::Str(message[0].c_str()));
            course_info->Set("name", JsonAny::Str(message[1].c_str()));
            course_info->Set("status", JsonAny::Boolean(message[2] == "on"));
            course_info->Set("master", JsonAny::Str(message[3].c_str()));
            course_info->Set("stream_address", JsonAny::Str(message[4].c_str()));

            courses_info->Add(course_info);
        }
        data->Set("courses", courses_info);
        data->Set("result", JsonAny::Str("success"));
    } else {

    }
    obj->Set("data", data);
    std::string result = obj->Dumps();
    m_conn->SendPacket(result);
    return err;
}

error Netizen::DeleteCourse(std::string courseId)
{
    error err = SUCCESS;

    JsonObject* obj = JsonAny::Object();
    obj->Set("type", JsonAny::Integer(MessageType::DeleteCourseResult));
    JsonObject* data = JsonAny::Object();

    if(m_courses.count(courseId) && m_courses[courseId]->GetTid() == m_account)
    {
        m_database->DeleteCourse(courseId);
        Course* course = nullptr;
        course = m_courses[courseId];
        course->DeleteCourse();
//        auto iter = m_courses.find(courseId);
//        m_courses.erase(iter);

        data->Set("result", JsonAny::Str("success"));
//        JsonArray* courses_info = JsonAny::Array();
//        std::vector<std::string> message;
//        for(auto& m: m_courses)
//        {
//            message = m.second->BaseMessage();
//            JsonObject* course_info = JsonAny::Object();
//            course_info->Set("id", JsonAny::Str(message[0].c_str()));
//            course_info->Set("name", JsonAny::Str(message[1].c_str()));
//            course_info->Set("status", JsonAny::Boolean(message[2] == "on"));
//            course_info->Set("master", JsonAny::Str(message[3].c_str()));
//            course_info->Set("stream_address", JsonAny::Str(message[4].c_str()));

//            courses_info->Add(course_info);
//        }
//        data->Set("courses", courses_info);
    } else {
        data->Set("result", JsonAny::Str("failed"));
        data->Set("reason", JsonAny::Str("identification is error"));
    }

    obj->Set("data", data);
    std::string result = obj->Dumps();
    m_conn->SendPacket(result);
    return err;
}

error Netizen::UpdateClassList(std::string courseId)
{
    error err = SUCCESS;
    JsonObject* obj = JsonAny::Object();
    obj->Set("type", JsonAny::Integer(MessageType::UpdateClassListResult));
    JsonObject* data = JsonAny::Object();
    if(m_course != nullptr)
    {
        data->Set("result", JsonAny::Str("success"));
        std::vector<std::vector<std::string>> members;
        members = m_course->GetClassMembers();
        JsonArray* member_abstract = JsonAny::Array();
        JsonObject* member = JsonAny::Object();
        for(auto& m : members)
        {
            member->Set("identification", JsonAny::Str(m[0].c_str()));
            member->Set("name", JsonAny::Str(m[1].c_str()));
            member_abstract->Add(member);
        }
        data->Set("members", member_abstract);
    } else {
        data->Set("result", JsonAny::Str("failed"));
    }

    obj->Set("data", data);
    std::string result = obj->Dumps();
    m_conn->SendPacket(result);
    return err;
}

error Netizen::RefreshCourseStatus()
{
    error err = SUCCESS;
    JsonObject* obj = JsonAny::Object();
    //    obj->Set("type", JsonAny::Integer(MessageType::RfreshCourseStatusResult));
    JsonObject* data = JsonAny::Object();
    JsonArray* course_info = JsonAny::Array();
    JsonObject* course = JsonAny::Object();

    for(auto& m: m_courses)
    {
        course->Set("id", JsonAny::Str(m.first.c_str()));
        course->Set("state", JsonAny::Str(m.second->GetState().c_str()));
        course_info->Add(course);
    }

    data->Set("course_info", course_info);
    obj->Set("data", data);
    std::string result = obj->Dumps();
    m_conn->SendPacket(result);
    return err;
}

error Netizen::InitClassroom(std::string courseId)
{
    error err = SUCCESS;
    JsonObject* obj = JsonAny::Object();
    //    obj->Set("type", JsonAny::Integer(MessageType::InitClassroomResult));
    JsonObject* data = JsonAny::Object();

    if(m_courses[courseId]->GetTid() == m_account){
        std::string result = "";
        result = m_database->InitClassroom(courseId);
        m_database->QueryCourseMember(courseId);

        if(result == "success"){
            JsonObject* course_info = JsonAny::Object();
            course_info->Set("id", JsonAny::Str(courseId.c_str()));
            course_info->Set("stream_address", JsonAny::Str(m_courses[courseId]->GetStreamAddress().c_str()));
            JsonObject* class_info = JsonAny::Object();
            class_info->Set("id", JsonAny::Str(m_courses[courseId]->GetClassId().c_str()));
            std::vector<std::vector<std::string>> members = m_courses[courseId]->GetClassMembers();
            JsonArray* member_abstract = JsonAny::Array();
            JsonObject* member = JsonAny::Object();
            for(auto& m : members)
            {
                member->Set("identification", JsonAny::Str(m[0].c_str()));
                member->Set("name", JsonAny::Str(m[1].c_str()));
                member_abstract->Add(member);
            }
            class_info->Set("member_abstract", member_abstract);
            course_info->Set("class_info", class_info);
            data->Set("course_info", course_info);

            m_course = m_courses[courseId];
            m_course->ClassBegin(this);
        }
    } else {

    }
    obj->Set("data", data);
    std::string result = obj->Dumps();
    m_conn->SendPacket(result);
    return err;
}

error Netizen::SendChatMessage(std::string content, std::string cid)
{
    error err = SUCCESS;

    std::vector<std::string> message;
    message.emplace_back(m_account);
    message.emplace_back(m_name);
    message.emplace_back(m_role);
    message.emplace_back(content);
    if(m_course){
        m_course->Enqueue(message);
    }

    return err;
}

error Netizen::JoinClass(std::string courseId)
{
    error err = SUCCESS;
    JsonObject* obj = JsonAny::Object();
    obj->Set("type", JsonAny::Integer(MessageType::JoinClassResult));
    JsonObject* data = JsonAny::Object();

    if(m_courses.count(courseId)){
        if(m_courses[courseId]->GetTid() != m_account){
            m_course = m_courses[courseId];
            m_course->EnterCourse(this);
            data->Set("result", JsonAny::Str("success"));
            std::vector<std::vector<std::string>> members;
            members = m_course->GetClassMembers();
            JsonArray* member_abstract = JsonAny::Array();
            JsonObject* member = JsonAny::Object();
            for(auto& m : members)
            {
                member->Set("identification", JsonAny::Str(m[0].c_str()));
                member->Set("name", JsonAny::Str(m[1].c_str()));
                member_abstract->Add(member);
            }
            data->Set("members", member_abstract);
        } else {
            m_course = m_courses[courseId];
            m_course->ClassBegin(this);
            data->Set("result", JsonAny::Str("success"));
            std::vector<std::vector<std::string>> members;
            members = m_course->GetClassMembers();
            JsonArray* member_abstract = JsonAny::Array();
            JsonObject* member = JsonAny::Object();
            for(auto& m : members)
            {
                member->Set("identification", JsonAny::Str(m[0].c_str()));
                member->Set("name", JsonAny::Str(m[1].c_str()));
                member_abstract->Add(member);
            }
            data->Set("members", member_abstract);

        }
    } else {
        data->Set("result", JsonAny::Str("failed"));
    }


    obj->Set("data", data);
    std::string result = obj->Dumps();
    m_conn->SendPacket(result);
    return err;
}

error Netizen::ExitClass(std::string courseId)
{
    error err = SUCCESS;
    JsonObject* obj = JsonAny::Object();
    obj->Set("type", JsonAny::Integer(MessageType::JoinClassResult));
    JsonObject* data = JsonAny::Object();

    if(m_courses.count(courseId)){
        if(m_courses[courseId]->GetTid() != m_account)
        {
            if(m_course != nullptr){
                m_course->ExitCourse(this);
                m_course = nullptr;
                data->Set("result", JsonAny::Str("success"));
            } else {
                data->Set("result", JsonAny::Str("failed"));
            }
        } else {
            if(m_course != nullptr){
                m_course->ClassEnd(this);
                m_course = nullptr;
                data->Set("result", JsonAny::Str("success"));
            } else {
                data->Set("result", JsonAny::Str("failed"));
            }
        }

    } else {
        data->Set("result", JsonAny::Str("failed"));
    }

    obj->Set("data", data);
    std::string result = obj->Dumps();
    m_conn->SendPacket(result);
    return err;
}

error Netizen::CloseClassroom(std::string courseId)
{
    error err = SUCCESS;

    if(m_courses[courseId]->GetTid() == m_account){
        std::string result = "";
        result = m_database->CloseClassroom(courseId);

        if(result == "success"){
            m_course->ClassEnd(this);
            m_course = nullptr;
        }
    } else {

    }


    return err;
}

error Netizen::SendChat(std::vector<std::string> message)
{
    error err = SUCCESS;

    JsonObject* obj = JsonAny::Object();
    //    obj->Set("type", JsonAny::Integer(MessageType::UpdateChatMessage));
    JsonObject* data = JsonAny::Object();

    JsonObject* chat_info = JsonAny::Object();
    chat_info->Set("netizen_id", JsonAny::Str(message[0].c_str()));
    chat_info->Set("name", JsonAny::Str(message[1].c_str()));
    chat_info->Set("message", JsonAny::Str(message[3].c_str()));

    data->Set("chat_info", chat_info);
    obj->Set("data", data);
    std::string result = obj->Dumps();
    m_conn->SendPacket(result);
    return err;
}

error Netizen::EnterClassNotify(std::string cid, std::string uid, std::string uname)
{
    error err = SUCCESS;
//    JsonObject* obj = JsonAny::Object();
//    //    obj->Set("type", JsonAny::Integer(MessageType::EnterClassNotify));
//    JsonObject* data = JsonAny::Object();

//    data->Set("id", JsonAny::Str(uid.c_str()));
//    data->Set("name", JsonAny::Str(uname.c_str()));

//    obj->Set("data", data);
//    std::string result = obj->Dumps();
//    m_conn->SendPacket(result);
    UpdateClassList(cid);
    return err;
}

error Netizen::ExitClassNotify(std::string cid, std::string uid, std::string uname)
{
    error err = SUCCESS;
    //    JsonObject* obj = JsonAny::Object();
    //    obj->Set("type", JsonAny::Integer(MessageType::ExitClassNotify));
    //    JsonObject* data = JsonAny::Object();

    //    data->Set("id", JsonAny::Str(uid.c_str()));
    //    data->Set("name", JsonAny::Str(uname.c_str()));

    //    obj->Set("data", data);
    //    std::string result = obj->Dumps();
    //    m_conn->SendPacket(result);

    UpdateClassList(cid);
    return err;
}

error Netizen::OpenClassNotify(std::string cid, std::string cname)
{
    error err = SUCCESS;
    JsonObject* obj = JsonAny::Object();
    //    obj->Set("type", JsonAny::Integer(MessageType::OpenClassNotify));
    JsonObject* data = JsonAny::Object();

    data->Set("id", JsonAny::Str(cid.c_str()));
    data->Set("name", JsonAny::Str(cname.c_str()));

    obj->Set("data", data);
    std::string result = obj->Dumps();
    m_conn->SendPacket(result);
    return err;
}

error Netizen::CloseClassNotify(std::string cid, std::string cname)
{
    error err = SUCCESS;
    JsonObject* obj = JsonAny::Object();
    //    obj->Set("type", JsonAny::Integer(MessageType::CloseClassNotify));
    JsonObject* data = JsonAny::Object();

    data->Set("id", JsonAny::Str(cid.c_str()));
    data->Set("name", JsonAny::Str(cname.c_str()));

    obj->Set("data", data);
    std::string result = obj->Dumps();
    m_conn->SendPacket(result);
    return err;
}

error Netizen::DeleteCourseNotify(std::string cid, Course* course)
{
    error err = SUCCESS;
    auto iter = m_courses.find(cid);
    m_courses.erase(iter);
    UpdateCourseList();
    course->Logout(this);

    return err;
}

error Netizen::SendClassNotification(std::string courseId, std::string state)
{
    error err = SUCCESS;
    JsonObject* obj = JsonAny::Object();
    //    obj->Set("type", JsonAny::Integer(MessageType::UpdateChatMessage));
    JsonObject* data = JsonAny::Object();

    obj->Set("data", data);
    std::string result = obj->Dumps();
    m_conn->SendPacket(result);
    return err;
}

std::string Netizen::GetName()
{
    return m_name;
}

std::string Netizen::GetRole()
{
    return m_role;
}

std::string Netizen::GetId()
{
    return m_account;
}

Course::Course(std::string id)
{
    m_mlock = MutexNew();
    m_llock = MutexNew();
    m_ulock = MutexNew();
    m_id = id;
    m_state = "off";
    m_trd = new STCoroutine("Course", this, Context->GetId());
}

Course::~Course()
{
    MutexDestroy(m_mlock);
    MutexDestroy(m_llock);
    MutexDestroy(m_ulock);
    Stop();
    Freep(m_trd);
}

error Course::Init(std::string cname, std::string tid, std::string tname, std::vector<std::vector<std::string>> classinfo)
{
    error err = SUCCESS;

    m_name = cname;
    m_tid = tid;
    m_tname = tname;
    m_classInfo = classinfo;
    m_classId = "0";
    for(auto& m: m_classInfo)
    {
        if(m[0].size() > m_classId.size() || (m[0].size() == m_classId.size() && m[0] > m_classId))
        {
            m_classId = m[0];
        }
    }
    uint32_t id = atoi(m_classId.c_str());
    id++;
    m_classId = std::to_string(id);
    m_streamAddress = "rtmp://120.78.82.230:1935/" + m_id + m_classId;

    return err;
}

error Course::Enqueue(std::vector<std::string> message)
{
    error err = SUCCESS;

    Locker(m_mlock);
    m_message.emplace(message);

    return err;
}

error Course::DistributeChat()
{
    error err = SUCCESS;

    std::vector<Netizen*> netizens;
    {
        Locker(m_ulock);
        netizens = m_netizens;
    }

    if(netizens.size() == 0)
    {
        return err;
    }

    std::vector<std::string> message;
    {
        Locker(m_mlock);
        if(m_message.size() != 0)
        {
            message = m_message.front();
            m_message.pop();
        }
    }

    for(auto &m : netizens)
    {
        if((err = m->SendChat(message)) != SUCCESS){
            return ERRORWRAP(err, "distribute chat");
        }
    }

    return err;
}

error Course::EnterCourse(Netizen *netizen)
{
    error err = SUCCESS;
    std::string name = netizen->GetName();
    std::string id = netizen->GetId();
    std::vector<Netizen*> tmp;
    {
        Locker(m_ulock);
        if(std::find(m_netizens.begin(), m_netizens.end(), netizen) == m_netizens.end())
        {
            tmp = m_netizens;
            m_netizens.emplace_back(netizen);
        }
    }

    for(auto& m: tmp)
    {
        m->EnterClassNotify(m_id, id, name);
    }

    return err;
}

error Course::ExitCourse(Netizen *netizen)
{
    error err = SUCCESS;

    std::string name = "";
    std::string id = "";
    std::vector<Netizen*> tmp;
    {
        Locker(m_ulock);
        auto iter = std::find(m_netizens.begin(), m_netizens.end(), netizen);
        if(iter != m_netizens.end())
        {
            name = (*iter)->GetName();
            id = (*iter)->GetId();
            m_netizens.erase(iter);
            tmp = m_netizens;
        }
    }

    for(auto& m : tmp)
    {
        m->ExitClassNotify(m_id, id, name);
    }


    return err;
}

error Course::Login(Netizen *netizen)
{
    error err = SUCCESS;

    Locker(m_llock);

    if(std::find(m_logon.begin(), m_logon.end(), netizen) != m_logon.end())
    {
        m_logon.emplace_back(netizen);
    }

    return err;
}

error Course::Logout(Netizen *netizen)
{
    error err = SUCCESS;

    {
        Locker(m_llock);
        auto iter = std::find(m_logon.begin(), m_logon.end(), netizen);
        if(iter != m_logon.end())
        {
            m_logon.erase(iter);
        }
    }

    {
        Locker(m_ulock);
        auto iter = std::find(m_netizens.begin(), m_netizens.end(), netizen);
        if(iter != m_netizens.end())
        {
            if(netizen->GetId() == m_tid)
            {
                ClassEnd(netizen);
            } else {
                ExitCourse(netizen);
            }
        }
    }

    return err;
}

error Course::DeleteCourse()
{
    error err = SUCCESS;

    std::vector<Netizen*> tmp;
    {
        Locker(m_llock);
        tmp = m_logon;
    }

    for(auto&m : tmp)
    {
        m->DeleteCourseNotify(m_id, this);
    }

    return err;
}

error Course::ClassBegin(Netizen *netizen)
{
    error err = SUCCESS;

    m_state = "on";

    int size = m_message.size();
    for(int i = 0; i < size; i++)
    {
        m_message.pop();
    }

    Start();

    std::vector<Netizen*> tmp;
    {
        Locker(m_llock);
        tmp = m_logon;
    }

    for(auto &m : tmp)
    {
        m->OpenClassNotify(m_id, m_name);
    }

    EnterCourse(netizen);

    return err;
}

error Course::ClassEnd(Netizen *netizen)
{
    error err = SUCCESS;

    m_state = "off";

    Stop();

    std::vector<Netizen*> tmp;
    {
        Locker(m_llock);
        tmp = m_logon;
    }

    for(auto &m : tmp)
    {
        m->CloseClassNotify(m_id, m_name);
    }

    {
        Locker(m_ulock);
        m_netizens.clear();
    }

    return err;
}

int Course::Logins()
{
    return m_logon.size();
}

std::vector<std::string> Course::BaseMessage()
{
    std::vector<std::string> message;
    message.emplace_back(m_id);
    message.emplace_back(m_name);
    message.emplace_back(m_state);
    message.emplace_back(m_tname);
    message.emplace_back(m_streamAddress);

    return message;
}

std::string Course::GetStreamAddress()
{
    return m_streamAddress;
}

std::string Course::GetName()
{
    return m_name;
}

std::string Course::GetTid()
{
    return m_tid;
}

std::string Course::GetTname()
{
    return m_tname;
}

std::string Course::GetClassId()
{
    return m_classId;
}

std::string Course::GetId()
{
    return m_id;
}

std::string Course::GetState()
{
    return m_state;
}

std::vector<std::vector<std::string>> Course::GetClassInfo()
{
    return m_classInfo;
}

std::vector<std::vector<std::string> > Course::GetClassMembers()
{
    std::vector<Netizen*> tmp = m_netizens;
    std::vector<std::vector<std::string>> members;
    std::vector<std::string> member;
    std::string role = "";
    for(auto &m: tmp)
    {
        member.clear();
        role = m->GetId() == m_tid ? "teacher" : "student";
        member.emplace_back(role);
        member.emplace_back(m->GetName());
        members.emplace_back(member);
    }

    return members;
}

error Course::Start()
{
    error err = SUCCESS;
    if(m_trd == nullptr)
    {
        m_trd = new STCoroutine("Course", this, Context->GetId());
    }
    if((err = m_trd->Start()) != SUCCESS){
        return ERRORWRAP(err, "coroutine");
    }

    return err;
}

void Course::Stop()
{
    m_trd->Stop();

    Freep(m_trd);
    m_trd = nullptr;
}

error Course::Cycle()
{
    error err = SUCCESS;

    while(true)
    {
        if(m_state == "off")
        {
            break;
        }
        DistributeChat();
    }

    return err;
}

void Course::Dispose()
{
    Stop();
}

CourseManager* courses = nullptr;

CourseManager::CourseManager()
{
    m_lock = MutexNew();
    m_timer = new HourGlass("course", this, 1 * UTIME_SECONDS);
}

CourseManager::~CourseManager()
{
    MutexDestroy(m_lock);
    Freep(m_timer);
}

error CourseManager::Initialize()
{
    return SetupTicks();
}

error CourseManager::FetchOrCreate(std::string courseId, Course **course)
{
    error err = SUCCESS;

    Locker(m_lock);

    Course* tcourse = nullptr;
    if((tcourse = Fetch(courseId)) != nullptr){
        *course = tcourse;
        return err;
    }

    Assert(m_pool.find(courseId) == m_pool.end());

    trace("new course, course id=%s", courseId.c_str());

    tcourse = new Course(courseId);

    m_pool[courseId] = tcourse;
    *course = tcourse;
    return err;
}

Course *CourseManager::Fetch(std::string courseId)
{
    Course* course = nullptr;

    if(m_pool.find(courseId) == m_pool.end())
    {
        return course;
    }

    course = m_pool[courseId];

    return course;
}

void CourseManager::Dispose()
{
    std::map<std::string, Course*>::iterator it;
    for(it = m_pool.begin(); it != m_pool.end(); ++it){
        Course* course = it->second;
        course->Dispose();
    }
    return;
}

error CourseManager::SetupTicks()
{
    error err = SUCCESS;

    if((err = m_timer->Tick(1, 1 * UTIME_SECONDS)) != SUCCESS){
        return ERRORWRAP(err, "tick");
    }

    if((err = m_timer->Start()) != SUCCESS){
        return ERRORWRAP(err, "timer");
    }

    return err;
}

error CourseManager::Notify(int event, utime_t interval, utime_t tick)
{
    error err = SUCCESS;

    return err;
}

void CourseManager::Destroy()
{
    std::map<std::string, Course*>::iterator it;
    for(it = m_pool.begin(); it != m_pool.end(); ++it)
    {
        Course* course = it->second;
        Freep(course);
    }
    m_pool.clear();
}
