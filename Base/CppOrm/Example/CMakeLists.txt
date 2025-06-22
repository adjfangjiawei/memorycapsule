add_executable(CppOrmMySqlExample main_cpporm_mysql_example.cpp user_model.h)

# 链接 Qt Core (用于 QDebug 等)
target_link_libraries(CppOrmMySqlExample PRIVATE Qt6::Core)

# 链接 CppOrm 库
# 假设 CppOrm 库的目标名是 "CppOrm" (在 CppOrm/CMakeLists.txt 中定义)
target_link_libraries(CppOrmMySqlExample PRIVATE CppOrm)