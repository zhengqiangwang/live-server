#ifndef TYPES_H
#define TYPES_H

namespace MessageType {
typedef enum {
  // Client 类型:
  Login,  // 进行登录
  Register,  // 进行注册
  UpdateCourseList, // 更新课程列表
  CreateCourse, // 教师创建课程
  DeleteCourse, // 教师删除课程
  JoinCourse,  // 学生加入课程
  DeselectionCourse, // 学生退选课程
  UpdateClassList,  // 更新课堂成员列表
  JoinClass,  // 进入课堂
  ExitClass,  // 退出课堂

  // Server 类型:
  LoginResult,  // 返回登录结果
  RegisterResult,  // 返回注册结果
  UpdateCourseListResult, // 更新课程列表结果
  CreateCourseResult,  // 教师创建课程返回结果
  DeleteCourseResult, // 教师删除课程返回结果
  JoinCourseResult,  // 学生加入课程返回结果
  DeselectionCourseResult, // 学生退选课程返回结果
  JoinClassResult,  // 进入课堂结果
  UpdateClassListResult,  // 更新课堂成员列表
};
}

#endif
