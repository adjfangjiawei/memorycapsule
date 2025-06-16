cmake_minimum_required(VERSION 3.16)

# 在 project() 之前写死构建 Debug 版本
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (Debug, Release, etc.)" FORCE)
endif()

# 设置 vcpkg 工具链文件路径 (请根据你的实际路径修改)
# !! 注意：这里使用了你提供的路径 !!
set(CMAKE_TOOLCHAIN_FILE "/media/root/newvolumn/fjw/vcpkg/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "Vcpkg toolchain file")
    
set(VCPKG_HOST_TRIPLET "x64-linux-dynamic")
set(VCPKG_TARGET_TRIPLET "x64-linux-dynamic")

# 写死 EXPORT_COMPILE_COMMANDS
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 设置 C++26 标准
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(cpporm LANGUAGES CXX)

# 写死 EXPORT_COMPILE_COMMANDS
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 设置 C++26 标准
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置 vcpkg 工具链文件路径 (请根据你的实际路径修改)
set(CMAKE_TOOLCHAIN_FILE "/media/root/newvolumn/fjw/vcpkg/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "Vcpkg toolchain file")

# 假设使用 Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Sql)

add_subdirectory(Base)

# # Glob 规则来包含所有 src 目录下的 .cpp 文件
# file(GLOB_RECURSE cpporm_SOURCES
#     CONFIGURE_DEPENDS
#     "src/*.cpp"
#     "src/**/*.cpp"
# )

# # 添加可执行文件 (用于开发和测试我们的 ORM 库)
# add_executable(${PROJECT_NAME}_runner ${cpporm_SOURCES})

# # 链接 Qt 库
# # 对于 Qt6:
# target_link_libraries(${PROJECT_NAME}_runner PRIVATE Qt6::Core Qt6::Sql)
# # 对于 Qt5:
# # target_link_libraries(${PROJECT_NAME}_runner PRIVATE Qt5::Core Qt5::Sql)

# # Qt 需要的额外步骤 (通常 find_package 会处理，但有时需要显式设置)
# # set(CMAKE_AUTOMOC ON) # 如果有 Q_OBJECT 宏
# # set(CMAKE_AUTORCC ON) # 如果有 .qrc 资源文件
# # set(CMAKE_AUTOUIC ON) # 如果有 .ui 界面文件
# # 我们目前主要用 QtSql，这些可能不是必需的，但加上也无妨。
# # 为了简化，如果 QtSql 不需要 Q_OBJECT 等，可以不加。
# # 暂时我们先不加，如果编译 QtSql 部分时提示缺少 MOC 文件等再添加。

# # 为头文件设置包含目录
# target_include_directories(${PROJECT_NAME}_runner PRIVATE
#     ${CMAKE_CURRENT_SOURCE_DIR}/src
#     # Qt 的包含目录应该由 find_package 自动处理
# )

# # 打印一些信息以供调试
# message(STATUS "Project Name: ${PROJECT_NAME}_runner")
# message(STATUS "CMAKE_CXX_STANDARD: ${CMAKE_CXX_STANDARD}")
# message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
# message(STATUS "CMAKE_TOOLCHAIN_FILE: ${CMAKE_TOOLCHAIN_FILE}")
# message(STATUS "Found Qt6 Core: ${Qt6_Core_FOUND}")
# message(STATUS "Found Qt6 Sql: ${Qt6_Sql_FOUND}")
# # message(STATUS "Qt6 Core include dirs: ${Qt6_Core_INCLUDE_DIRS}") # 调试用
# # message(STATUS "Qt6 Sql include dirs: ${Qt6_Sql_INCLUDE_DIRS}") # 调试用
# message(STATUS "Source files (will be populated during generation): ${cpporm_SOURCES}")