// cpporm/session.h
#ifndef cpporm_SESSION_H
#define cpporm_SESSION_H

// 核心 Session 类定义（非模板成员，模板成员声明）
#include "cpporm/session_core.h"

// 简单模板化 CRUD 便捷函数的实现
#include "cpporm/session_crud_ops.h"

// CreateBatch 模板函数的实现
#include "cpporm/session_batch_ops.h"

// 如果将来有其他可分离的模板操作组，可以继续添加：
// #include "cpporm/session_preload_ops.h"
// #include "cpporm/session_advanced_query_ops.h"

#endif // cpporm_SESSION_H