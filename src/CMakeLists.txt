CMAKE_MINIMUM_REQUIRED(VERSION 3.18) #cmake版本要求


message("Building rdma_comm_core") #打印消息


set(SETTINGENV done)

SET(CMAKE_C_COMPILER "gcc")
SET(CMAKE_CXX_COMPILER "g++")


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE debug)
#set(CMAKE_BUILD_TYPE release)



# 设置debug或者release的编译参数
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -W -Wall -Wextra -Wreturn-type -g -ggdb -fPIC -std=c++2a")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -Os -W -Wall -Wextra -Wreturn-type -fPIC -std=c++2a")





#分别指定编译include和链接link的搜索目录
include_directories(../include)

aux_source_directory(. DIR_LIB_SRCS)
MESSAGE("building rdma_comm_core ... ")
add_library(rdma_comm_core SHARED ${DIR_LIB_SRCS} )
target_link_libraries(rdma_comm_core mlx5 ibverbs)
target_include_directories(rdma_comm_core PUBLIC ${PROJECT_SOURCE_DIR}/include)