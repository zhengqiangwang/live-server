#ifndef DATABASE_H
#define DATABASE_H

#include "core.h"
#include <mysql/mysql.h>
#include <string>
#include <vector>

class Database
{
private:
    static Database *_instance;

    MYSQL* m_mysql = nullptr;
    MYSQL_RES* m_result = nullptr;
    MYSQL_ROW m_row;

private:
    Database();
    virtual ~Database();

public:
    static Database* Instance();

    error InitDb(std::string host, std::string user, std::string pwd, std::string dbname);
    error ChangeState();
    std::string Register(std::string name, std::string password, std::string identification);
    std::string Login(std::string id, std::string password, std::string &identification, std::string &name);
    std::string Logout(std::string id);
    std::vector<std::vector<std::string> > QueryCourses(std::string id);
    std::vector<std::vector<std::string> > QueryCourseMember(std::string courseid);
    std::string CreateCourse(std::string tid, std::string tname, std::string coursename);
    std::vector<std::vector<std::string> > DeleteCourse(std::string courseid);
    std::string AddCourse(std::string id, std::string courseid, std::vector<std::string> &coursemessage);
    std::string DeselectionCourse(std::string id, std::string courseid);
    std::vector<std::string> QueryCourse(std::string courseid);
    std::string InitClassroom(std::string courseid);
    std::string CloseClassroom(std::string courseid);
    std::vector<std::vector<std::string>> GetClassInfo(std::string cid);
    std::string UploadFile(std::string courseid, std::string userid, std::string filename, long time, std::string password);
    std::vector<std::string> DownloadFile(std::string courseid, std::string userid, std::string filename, long time);
    std::string LaunchSign(std::string courseid, long createtime, long opentime, long endtime);
    std::string SettingSigners(std::string qid, std::vector<std::string> signers);
    std::string Signing(std::string qid, std::string uid, long signtime);
    std::vector<std::string> QuerySignTime(std::string qid);
};

#endif // DATABASE_H
