#include "database.h"
#include "error.h"
#include "log.h"
#include "protocol_utility.h"
#include "core_autofree.h"
#include <cstring>

Database* Database::_instance = nullptr;

Database::Database()
{
    m_mysql = mysql_init(nullptr);
    if(m_mysql == nullptr)
    {
        ERROR("mysql init failed");
    }

    error err = SUCCESS;
    if((err = InitDb("localhost", "root", "~Wang43456364", "test")) != SUCCESS ){
        ERROR("init db failed: %s", mysql_error(m_mysql));
    }

}

Database::~Database()
{

}

Database *Database::Instance()
{
    if(_instance == nullptr)
    {
        _instance = new Database();
    }

    return _instance;
}

error Database::InitDb(std::string host, std::string user, std::string pwd, std::string dbname)
{
    error err = SUCCESS;

    trace("init db by host: %s, user: %s, password: %s, database name: %s",host.c_str(), user.c_str(), pwd.c_str(), dbname.c_str());
    m_mysql = mysql_real_connect(m_mysql, host.c_str(), user.c_str(), pwd.c_str(), dbname.c_str(), 0, nullptr, 0);
    if (m_mysql == nullptr)
    {
        trace("mysql link error");
        //        return ERROR("init db failed: %s", mysql_error(m_mysql));

    }

    std::string sql = "CREATE TABLE IF NOT EXISTS USERINFO  ( \
            uid varchar(30),\
            uname varchar(30),\
            upassword varchar(64),\
            salt varchar(17),\
            status varchar(20),\
            identification varchar(20),\
            PRIMARY KEY(uid)\
            ) ";

            trace("sql %s", sql.c_str());
    int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("create table userinfo failed: %s", mysql_error(m_mysql));
        //        return false;
    }

    sql = "CREATE TABLE IF NOT EXISTS COURSEINFO  ( \
            cid varchar(30),\
            cname varchar(30),\
            tid varchar(30),\
            tname varchar(30),\
            status varchar(20),\
            PRIMARY KEY(cid)\
            ) ";

            re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("create table courseinfo failed: %s", mysql_error(m_mysql));
        //        return false;
    }

    sql = "CREATE TABLE IF NOT EXISTS UC  ( \
            uid varchar(30),\
            cid varchar(30),\
            PRIMARY KEY(uid,cid)\
            ) ";

            re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("create table uc failed: %s", mysql_error(m_mysql));
        //        return false;
    }

    sql = "CREATE TABLE IF NOT EXISTS CLASSINFO  ( \
            cid varchar(30),\
            classid varchar(30),\
            classname varchar(100)\
            ) ";

            re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("create table CLASSINFO failed: %s", mysql_error(m_mysql));
        //        return false;
    }

    sql = "CREATE TABLE IF NOT EXISTS FILEINFO  ( \
            cid varchar(30),\
            uid varchar(30),\
            time BIGINT(20),\
            password varchar(70),\
            savename varchar(30),\
            filename varchar(100)\
            ) ";

            re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("create table FILEINFO failed: %s", mysql_error(m_mysql));
        //        return false;
    }

    sql = "CREATE TABLE IF NOT EXISTS SIGNINFO  ( \
            cid varchar(30),\
            qid varchar(30),\
            createtime BIGINT(20),\
            opentime BIGINT(20),\
            endtime BIGINT(20)\
            ) ";

            re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("create table FILEINFO failed: %s", mysql_error(m_mysql));
        //        return false;
    }

    sql = "CREATE TABLE IF NOT EXISTS SIGNPERSON  ( \
            qid varchar(30),\
            uid varchar(30),\
            time BIGINT(20),\
            PRIMARY KEY(qid,uid)\
            ) ";

            re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("create table FILEINFO failed: %s", mysql_error(m_mysql));
        //        return false;
    }


    ChangeState();

    return err;
}

error Database::ChangeState()
{
    error err = SUCCESS;
    std::string statues = "online";
    std::string sql = "SELECT uid FROM USERINFO where status = '" + statues + "';";
    int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("mysql_query failed!", mysql_error(m_mysql));
        return err;
    }
    m_result = mysql_store_result(m_mysql);  //获取结果集
    if (m_result)  // 返回了结果集
    {
        int  num_rows = mysql_num_rows(m_result);       //获取结果集中总共的行数
        for (int i = 0; i < num_rows; i++)
        {
            //获取下一行数据
            m_row = mysql_fetch_row(m_result);
            if (m_row == nullptr) break;
            std::string statues = "offline";
            std::string sql = "UPDATE USERINFO SET status = '" + statues + "' where uid = '" + m_row[0] + "';";
            int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
            if (re != 0)
            {
                ERROR("change status failed:%s; reason: %s", m_row[0], mysql_error(m_mysql));
                return err;
            }
        }
        mysql_free_result(m_result);
    }
    m_result = nullptr;

    statues = "on";
    sql = "SELECT cid FROM COURSEINFO where status = '" + statues + "';";
    re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("mysql_query failed!", mysql_error(m_mysql));
        return err;
    }
    m_result = mysql_store_result(m_mysql);  //获取结果集
    if (m_result)  // 返回了结果集
    {
        int  num_rows = mysql_num_rows(m_result);       //获取结果集中总共的行数
        for (int i = 0; i < num_rows; i++)
        {
            //获取下一行数据
            m_row = mysql_fetch_row(m_result);
            if (m_row == nullptr) break;
            std::string statues = "off";
            std::string sql = "UPDATE COURSEINFO SET status = '" + statues + "' where cid = '" + m_row[0] + "';";
            int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
            if (re != 0)
            {
                ERROR("change status failed:%s; reason: %s", m_row[0], mysql_error(m_mysql));
                return err;
            }
        }
        mysql_free_result(m_result);
    }
    m_result = nullptr;

    return err;
}

std::string Database::Register(std::string name, std::string password, std::string identification)
{
    trace("register account name:%s, password:%s, identification:%s", name.c_str(), password.c_str(), identification.c_str());

    std::string salt = RandomStr(16);

    password += salt;

    std::string buf;
    std::string shadata;
    int len = Sha256Encrypt(password, shadata);

    Base64Encode(shadata.c_str(), len, buf);
    password = buf;

    int account = 0;
    long mid = 0;
    std::string sql = "SELECT uid FROM USERINFO;";
    int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("mysql_query failed!", mysql_error(m_mysql));
        return "";
    }
    m_result = mysql_store_result(m_mysql);  //获取结果集
    if (m_result)  // 返回了结果集
    {
        int  num_rows = mysql_num_rows(m_result);       //获取结果集中总共的行数
        for (int i = 0; i < num_rows; i++)
        {
            //获取下一行数据
            m_row = mysql_fetch_row(m_result);
            if (m_row == nullptr) break;
            mid = atoi(m_row[0]);
            if (mid > account)
            {
                account = mid;
            }
        }
        mysql_free_result(m_result);
    }
    m_result = nullptr;
    account++;
    std::string status = "offline";

    sql = "INSERT INTO USERINFO ( uid, uname, upassword, salt, status, identification) VALUES  ( '" + std::to_string(account) + "', '" + name + "', '" + password + "', '" + salt +"', '" + status + "', '" + identification +  "');";
    re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("insert account failed:%s, reason: %s",std::to_string(account).c_str(), mysql_error(m_mysql));
        return "";
    }
    return std::to_string(account);
}

std::string Database::Login(std::string id, std::string password, std::string& identification, std::string& name)
{
    std::string result = "";
    trace("user login id:%s",id.c_str());
    std::string sql = "SELECT salt, upassword, identification, uname, status  FROM USERINFO where uid = '" + id + "';";
    int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("query failed account:%s is not find; reason: %s", id.c_str(), mysql_error(m_mysql));
        result = "account does not exist";
        return result;
    }

    std::string status = "";
    m_result = mysql_store_result(m_mysql);
    if (m_result)
    {
        m_row = mysql_fetch_row(m_result);
        if (m_row == nullptr)
        {
            mysql_free_result(m_result);
            result = "account does not exist";
            return result;
        }
        std::string salt = m_row[0];
        std::string str = m_row[1];
        identification = m_row[2];
        name = m_row[3];
        status = m_row[4];

        mysql_free_result(m_result);
        m_result = nullptr;

        if(status == "online")
        {
            result = "logged in";
            return result;
        }

        password += salt;

        std::string shadata;
        std::string buf;
        int len = Sha256Encrypt(password, shadata);

        Base64Encode(shadata.c_str(), len, buf);
        password = buf;

        if(str == password)
        {
            std::string statues = "online";
            sql = "UPDATE USERINFO SET status = '" + statues + "' where uid = '" + id + "';";
            re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
            if (re != 0)
            {
                ERROR("change status failed:%s ; reason:%s", id.c_str(), mysql_error(m_mysql));
                result = "server error";
                return result;
            }

            result = "success";
            return result;
        }
        else
        {
            result = "password error";
            return result;
        }
    }


    result = "account is not found";
    return result;
}

std::string Database::Logout(std::string id)
{
    std::string result = "";
    trace("user logout id:%s",id.c_str());
    std::string statues = "offline";
    std::string sql = "UPDATE USERINFO SET status = '" + statues + "' where uid = '" + id + "';";
    int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("change status failed:%s; reason: %s", id.c_str(), mysql_error(m_mysql));
        result = "server error";
        return result;
    }

    result = "success";
    return result;
}

std::vector<std::vector<std::string>> Database::QueryCourses(std::string id)
{
    std::vector<std::vector<std::string>> result;
    std::string sql = "SELECT cid FROM UC where uid = '" + id + "';";
    int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("query course list failed:%s ; reason: %s", id.c_str(), mysql_error(m_mysql));
        return result;
    }

    std::vector<std::string> courseid;
    m_result = mysql_store_result(m_mysql);  //获取结果集
    if (m_result)  // 返回了结果集
    {
        int  num_rows = mysql_num_rows(m_result);       //获取结果集中总共的行数
        for (int i = 0; i < num_rows; i++)
        {
            //获取下一行数据
            m_row = mysql_fetch_row(m_result);
            if (m_row == nullptr) break;
            courseid.emplace_back(m_row[0]);
        }
        mysql_free_result(m_result);
    }
    m_result = nullptr;

    std::vector<std::string> course;
    for(auto &m : courseid){
        sql = "SELECT cname, tid, tname, status FROM COURSEINFO where cid = '" + m + "';";
        re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
        if (re != 0)
        {
            ERROR("query course message failed:%s ; reason: %s", m.c_str(), mysql_error(m_mysql));
            return result;
        }
        m_result = mysql_store_result(m_mysql);  //获取结果集
        if (m_result)  // 返回了结果集
        {

            m_row = mysql_fetch_row(m_result);
            if (m_row == nullptr) continue;
            course.emplace_back(m);
            course.emplace_back(m_row[0]);
            course.emplace_back(m_row[1]);
            course.emplace_back(m_row[2]);
            course.emplace_back(m_row[3]);
            mysql_free_result(m_result);
        }
        m_result = nullptr;

        result.emplace_back(course);
        course.clear();
    }

    return result;
}

std::vector<std::vector<std::string>> Database::QueryCourseMember(std::string courseid)
{
    std::vector<std::vector<std::string>> result;
    std::string sql = "SELECT uid FROM UC where cid = '" + courseid + "';";
    int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("query course list failed:%s; reason: %s", courseid.c_str(), mysql_error(m_mysql));
        return result;
    }

    std::vector<std::string> studentid;
    m_result = mysql_store_result(m_mysql);  //获取结果集
    if (m_result)  // 返回了结果集
    {
        int  num_rows = mysql_num_rows(m_result);       //获取结果集中总共的行数
        for (int i = 0; i < num_rows; i++)
        {
            //获取下一行数据
            m_row = mysql_fetch_row(m_result);
            if (m_row == nullptr) break;
            studentid.emplace_back(m_row[0]);
        }
        mysql_free_result(m_result);
    }
    m_result = nullptr;

    std::vector<std::string> course;
    for(auto &m : studentid){
        sql = "SELECT uname, status FROM USERINFO where uid = '" + m + "';";
        re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
        if (re != 0)
        {
            ERROR("query course message failed:%s, reason: %s", m.c_str(), mysql_error(m_mysql));
            return result;
        }
        m_result = mysql_store_result(m_mysql);  //获取结果集
        if (m_result)  // 返回了结果集
        {

            m_row = mysql_fetch_row(m_result);
            if (m_row == nullptr) continue;
            course.emplace_back(m);
            course.emplace_back(m_row[0]);
            course.emplace_back(m_row[1]);
            mysql_free_result(m_result);
        }
        m_result = nullptr;

        result.emplace_back(course);
        course.clear();
    }

    return result;
}

std::string Database::CreateCourse(std::string tid, std::string tname,std::string coursename)
{
    std::string result = "";
    std::string sql = "SELECT cid FROM COURSEINFO;";
    int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("mysql_query failed!", mysql_error(m_mysql));
        return "";
    }
    long mid = 0;
    long account = 0;
    m_result = mysql_store_result(m_mysql);  //获取结果集
    if (m_result)  // 返回了结果集
    {
        int  num_rows = mysql_num_rows(m_result);       //获取结果集中总共的行数
        for (int i = 0; i < num_rows; i++)
        {
            //获取下一行数据
            m_row = mysql_fetch_row(m_result);
            if (m_row == nullptr) break;
            mid = atoi(m_row[0]);
            if (mid > account)
            {
                account = mid;
            }
        }
        mysql_free_result(m_result);
    }
    m_result = nullptr;
    account++;
    std::string status = "off";
    sql = "INSERT INTO COURSEINFO ( cid, cname, tid, tname, status ) VALUES  ( '" + std::to_string(account) + "', '" + coursename + "', '" + tid + "', '" + tname +"', '" + status + "');";
    re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("insert account failed:%s ; reason: %s",std::to_string(account).c_str(), mysql_error(m_mysql));
        return "";
    }

    std::vector<std::string> message;
    AddCourse(tid, std::to_string(account), message);

    return std::to_string(account);
}

std::vector<std::vector<std::string>> Database::DeleteCourse(std::string courseid)
{
    std::vector<std::vector<std::string>> result;
    std::string sql = "SELECT uid FROM UC where cid = '" + courseid + "';";
    int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("query course list failed:%s; reason: %s", courseid.c_str(), mysql_error(m_mysql));
        return result;
    }

    std::vector<std::string> studentid;
    m_result = mysql_store_result(m_mysql);  //获取结果集
    if (m_result)  // 返回了结果集
    {
        int  num_rows = mysql_num_rows(m_result);       //获取结果集中总共的行数
        for (int i = 0; i < num_rows; i++)
        {
            //获取下一行数据
            m_row = mysql_fetch_row(m_result);
            if (m_row == nullptr) break;
            studentid.emplace_back(m_row[0]);
        }
        mysql_free_result(m_result);
    }
    m_result = nullptr;

    sql = "DELETE FROM UC where cid = '" + courseid + "';";
    re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("delete course member failed:%s; reason: %s", courseid.c_str(), mysql_error(m_mysql));
        return result;
    }

    sql = "DELETE FROM COURSEINFO where cid = '" + courseid + "';";
    re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("delete course member failed:%s ;reason: %s", courseid.c_str(), mysql_error(m_mysql));
        return result;
    }

    std::vector<std::string> course;
    for(auto &m : studentid){
        sql = "SELECT uname, status FROM USERINFO where uid = '" + m + "';";
        re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
        if (re != 0)
        {
            ERROR("query course message failed:%s ; reason: %s", m.c_str(), mysql_error(m_mysql));
            return result;
        }
        m_result = mysql_store_result(m_mysql);  //获取结果集
        if (m_result)  // 返回了结果集
        {

            m_row = mysql_fetch_row(m_result);
            if (m_row == nullptr) continue;
            course.emplace_back(m);
            course.emplace_back(m_row[0]);
            course.emplace_back(m_row[1]);
            mysql_free_result(m_result);
        }
        m_result = nullptr;

        result.emplace_back(course);
        course.clear();
    }

    return result;
}

std::string Database::AddCourse(std::string id, std::string courseid, std::vector<std::string>& coursemessage)
{
    std::string result = "";
    std::string sql = "SELECT cname, tid, tname, status FROM COURSEINFO where cid = '" + courseid + "';";
    int  re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("query course message failed:%s; reason:%s", courseid.c_str(), mysql_error(m_mysql));
        result = "No current course";
        return result;
    }

    std::string tid = "";
    std::string tname = "";
    std::string status = "";

    m_result = mysql_store_result(m_mysql);  //获取结果集
    if (m_result)  // 返回了结果集
    {
        m_row = mysql_fetch_row(m_result);
        if (m_row == nullptr){
            result = "No current course";
            return result;
        }
        coursemessage.emplace_back(m_row[0]);
        coursemessage.emplace_back(m_row[1]);
        coursemessage.emplace_back(m_row[2]);
        coursemessage.emplace_back(m_row[3]);
        mysql_free_result(m_result);
    }
    m_result = nullptr;

    sql = "SELECT uid FROM UC where uid = '" + id + "' and cid = '" + courseid + "';";
    re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("query course id failed:%s; reason: %s", courseid.c_str(), mysql_error(m_mysql));
        result = "No current course";
        return result;
    }

    m_result = mysql_store_result(m_mysql);  //获取结果集
    if (m_result)  // 返回了结果集
    {
        m_row = mysql_fetch_row(m_result);
        if (m_row != nullptr){
            result = "This course has been selected";
            return result;
        }
    }
    mysql_free_result(m_result);
    m_result = nullptr;

    sql = "INSERT INTO UC ( uid, cid ) VALUES  ( '" + id + "', '" + courseid + "');";
    re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("add course failed:%s; reason: %s",id.c_str(), mysql_error(m_mysql));
        result = "error";
        return result;
    }

    result = "success";
    return result;
}

std::string Database::DeselectionCourse(std::string id, std::string courseid)
{
    std::string result = "";
    std::string sql = "DELETE FROM UC where uid = '" + id + "' and cid = '" + courseid + "';";
    int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("deselect course failed:%s; reason: %s",id.c_str(), mysql_error(m_mysql));
        return result;
    }

    result = "success";
    return result;
}

std::vector<std::string> Database::QueryCourse(std::string courseid)
{
    std::vector<std::string> result;
    std::string sql = "SELECT cname, tid, tname, status FROM COURSEINFO where cid = '" + courseid + "';";
    int  re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("query course message failed:%s; reason:%s", courseid.c_str(), mysql_error(m_mysql));
        return result;
    }

    m_result = mysql_store_result(m_mysql);  //获取结果集
    if (m_result)  // 返回了结果集
    {
        m_row = mysql_fetch_row(m_result);
        if (m_row == nullptr){
            return result;
        }
        result.emplace_back(m_row[0]);
        result.emplace_back(m_row[1]);
        result.emplace_back(m_row[2]);
        result.emplace_back(m_row[3]);
    }
    mysql_free_result(m_result);
    m_result = nullptr;

    return result;
}

std::string Database::InitClassroom(std::string courseid)
{
    std::string result = "";
    trace("teacher init class id:%s",courseid.c_str());
    std::string statues = "on";
    std::string sql = "UPDATE COURSEINFO SET status = " + statues + " where cid = '" + courseid + "';";
    int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("change status failed:%s; reason: %s", courseid.c_str(), mysql_error(m_mysql));
        result = "change status failed";
        return result;
    }

    result = "success";
    return result;
}

std::string Database::CloseClassroom(std::string courseid)
{
    std::string result = "";
    trace("teacher close class id:%s",courseid.c_str());
    std::string statues = "off";
    std::string sql = "UPDATE COURSEINFO SET status = '" + statues + "' where cid = '" + courseid + "';";
    int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("change status failed:%s; reason:%s", courseid.c_str(), mysql_error(m_mysql));
        result = "change status failed";
        return result;
    }

    result = "success";
    return result;
}

std::vector<std::vector<std::string> > Database::GetClassInfo(std::string cid)
{
    std::vector<std::vector<std::string>> result;
    std::vector<std::string> classinfo;
    std::string sql = "SELECT classid, classname FROM CLASSINFO where cid = '" + cid + "';";
    int  re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("query class message failed:%s; reason:%s", cid.c_str(), mysql_error(m_mysql));
        return result;
    }

    m_result = mysql_store_result(m_mysql);  //获取结果集
    if (m_result)  // 返回了结果集
    {
        int  num_rows = mysql_num_rows(m_result);       //获取结果集中总共的行数
        for (int i = 0; i < num_rows; i++)
        {
            //获取下一行数据
            m_row = mysql_fetch_row(m_result);
            if (m_row == nullptr){
                return result;
            }
            classinfo.emplace_back(m_row[0]);
            classinfo.emplace_back(m_row[1]);
            result.emplace_back(classinfo);
        }
    }
    mysql_free_result(m_result);
    m_result = nullptr;

    return result;
}

std::string Database::UploadFile(std::string courseid, std::string userid, std::string filename, long time, std::string password)
{
    std::string result = "";

    std::string sql = "SELECT uid FROM UC where uid = '" + userid + "' and cid = '" + courseid + "';";
    int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("query course id failed:%s; reason: %s", courseid.c_str(), mysql_error(m_mysql));
        result = "server error";
        return result;
    }

    m_result = mysql_store_result(m_mysql);  //获取结果集
    if (mysql_num_rows(m_result) == 1)  // 返回了结果集
    {
        int savename = 0;
        long mid = 0;
        std::string sql = "SELECT savename FROM FILEINFO where cid = '" + courseid + "';";
        int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
        if (re != 0)
        {
            ERROR("mysql_query failed!", mysql_error(m_mysql));
            result = "server error";
            return result;
        }
        m_result = mysql_store_result(m_mysql);  //获取结果集
        if (m_result)  // 返回了结果集
        {
            int  num_rows = mysql_num_rows(m_result);       //获取结果集中总共的行数
            for (int i = 0; i < num_rows; i++)
            {
                //获取下一行数据
                m_row = mysql_fetch_row(m_result);
                if (m_row == nullptr) break;
                mid = atoi(m_row[0]);
                if (mid > savename)
                {
                    savename = mid;
                }
            }
            mysql_free_result(m_result);
        }
        m_result = nullptr;
        savename++;

        sql = "INSERT INTO FILEINFO ( cid, uid, time, password, savename, filename) VALUES  ( '" + courseid + "', '" + userid + "', '" + std::to_string(time) + "', '" + password +"', '" + std::to_string(savename) + "', '" + filename +  "');";
        re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
        if (re != 0)
        {
            ERROR("insert USERINFO failed:%s, reason: %s",std::to_string(savename).c_str(), mysql_error(m_mysql));
            return "server error";
        }

        result = std::to_string(savename);
        return result;
    } else {
        result = "don't select this course";
    }

    return result;
}

std::vector<std::string> Database::DownloadFile(std::string courseid, std::string userid, std::string filename, long time)
{
    std::vector<std::string> result;

    std::string sql = "SELECT uid FROM UC where uid = '" + userid + "' and cid = '" + courseid + "';";
    int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("query course id failed:%s; reason: %s", courseid.c_str(), mysql_error(m_mysql));
        return result;
    }

    m_result = mysql_store_result(m_mysql);  //获取结果集
    if (mysql_num_rows(m_result) == 1)  // 返回了结果集
    {
        std::string sql = "SELECT password, savename FROM FILEINFO where cid = '" + courseid + "' and uid = '" + userid + "' and time = '" + std::to_string(time) + "' and filename = '" + filename  + "';";
        int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
        if (re != 0)
        {
            ERROR("mysql_query failed!", mysql_error(m_mysql));
            return result;
        }
        m_result = mysql_store_result(m_mysql);  //获取结果集
        if (m_result)  // 返回了结果集
        {
            m_row = mysql_fetch_row(m_result);
            if (m_row == nullptr) return result;

            result.emplace_back(m_row[0]);
            result.emplace_back(m_row[1]);

            mysql_free_result(m_result);
        }
        m_result = nullptr;
    }

    return result;
}

std::string Database::LaunchSign(std::string courseid, long createtime, long opentime, long endtime)
{
    std::string result;

    int qid = 0;
    long mid = 0;
    std::string sql = "SELECT qid FROM SIGNINFO where cid = '" + courseid + "';";
    int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("mysql_query failed!", mysql_error(m_mysql));
        result = "server error";
        return result;
    }
    m_result = mysql_store_result(m_mysql);  //获取结果集
    if (m_result)  // 返回了结果集
    {
        int  num_rows = mysql_num_rows(m_result);       //获取结果集中总共的行数
        for (int i = 0; i < num_rows; i++)
        {
            //获取下一行数据
            m_row = mysql_fetch_row(m_result);
            if (m_row == nullptr) break;
            mid = atoi(m_row[0]);
            if (mid > qid)
            {
                qid = mid;
            }
        }
        mysql_free_result(m_result);
    }
    m_result = nullptr;
    qid++;

    sql = "INSERT INTO SIGNINFO ( cid, qid, createtime, opentime, endtime) VALUES  ( '" + courseid + "', '" + std::to_string(qid) + "', '" + std::to_string(createtime) + "', '" + std::to_string(opentime) +"', '" + std::to_string(endtime) +  "');";
    re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("insert USERINFO failed:%s, reason: %s",std::to_string(qid).c_str(), mysql_error(m_mysql));
        return "server error";
    }

    result = std::to_string(qid);
    return result;
}

std::string Database::SettingSigners(std::string qid, std::vector<std::string> signers)
{
    std::string result = "";

    for(auto& m: signers)
    {
        std::string sql = "INSERT INTO SIGNPERSON ( qid, uid, time) VALUES  ( '" + qid + "', '" + m + "', '" + std::to_string(-1) + "');";
        int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
        if (re != 0)
        {
            ERROR("insert USERINFO failed:%s, reason: %s",m.c_str(), mysql_error(m_mysql));
            return "server error";
        }
    }

    return result;
}

std::string Database::Signing(std::string qid, std::string uid, long signtime)
{
    std::string result = "";

    std::string sql = "UPDATE SIGNPERSON SET time = '" + std::to_string(signtime) + "' where uid = '" + uid + "' and qid = '" + qid + "';";
    int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("change status failed:%s; reason: %s", m_row[0], mysql_error(m_mysql));
        return "server error";
    }

    return result;
}

std::vector<std::string> Database::QuerySignTime(std::string qid)
{
    std::vector<std::string> result;

    std::string sql = "SELECT opentime, endtime FROM SIGNINFO where qid = '" + qid + "';";
    int re = mysql_query(m_mysql, sql.c_str());//从字符串换成const char*
    if (re != 0)
    {
        ERROR("mysql_query failed!", mysql_error(m_mysql));
        return result;
    }
    m_result = mysql_store_result(m_mysql);  //获取结果集
    if (m_result)  // 返回了结果集
    {

        m_row = mysql_fetch_row(m_result);
        if (m_row == nullptr) return result;
        result.emplace_back(m_row[0]);
        result.emplace_back(m_row[1]);

        mysql_free_result(m_result);
    }
    m_result = nullptr;

    return result;
}
