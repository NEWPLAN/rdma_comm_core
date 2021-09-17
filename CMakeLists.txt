CMAKE_MINIMUM_REQUIRED(VERSION 3.18) #cmake版本要求
project(rdma_comm_core VERSION "0.0.0.0")

file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.cxx" "src/*.cc")
file(GLOB_RECURSE HEADERS "include/*.h")

set(SRCS ${SOURCES} ${HEADERS} ${RESOURCES} ${FORMS})

set(CMAKE_CXX_STANDARD 17)

#分别指定编译include和链接link的搜索目录
#include_directories(../include)

MESSAGE("building ${PROJECT_NAME} ... ")
add_library(${PROJECT_NAME} SHARED ${SRCS} )
target_link_libraries(${PROJECT_NAME} ibverbs)

aux_source_directory(./example SRC_LIST)

foreach(FILE_PATH IN LISTS SRC_LIST)
    STRING(REGEX REPLACE ".+/(.+)\\..*" "\\1" TAEGET_NAME ${FILE_PATH})
    MESSAGE("building example for ${TAEGET_NAME} ")
    #SET(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} 
    #${PROJECT_SOURCE_DIR}/install)
    #find_package(${PROJECT_NAME} REQUIRED)
    #设置编译目标
    add_executable(${TAEGET_NAME}  ${FILE_PATH})
    #添加编译目标依赖
    target_link_libraries(${TAEGET_NAME}
        glog 
        pthread 
        mlx5
        ibverbs 
        ${PROJECT_NAME})
    install(TARGETS ${TAEGET_NAME}
        EXPORT ${TAEGET_NAME}-targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)
endforeach()



# #设置编译目标
# add_executable(${PROJECT_NAME}.connection_test  ${SRC_LIST})
# #添加编译目标依赖
# target_link_libraries(${PROJECT_NAME}.connection_test 
#      glog 
#      pthread 
#      ibverbs 
#      ${PROJECT_NAME})


SET(CMAKE_INSTALL_PREFIX ${PROJECT_SOURCE_DIR}/install)
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
# set_target_properties(${PROJECT_NAME}
#     PROPERTIES PUBLIC_HEADER
#     "${HEADERS}")
install(TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}-targets
    PUBLIC_HEADER DESTINATION include
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)

install(EXPORT ${PROJECT_NAME}-targets
#    NAMESPACE mylib::
    FILE ${PROJECT_NAME}-config.cmake
    DESTINATION lib/cmake/${PROJECT_NAME})

install(DIRECTORY "${CMAKE_SOURCE_DIR}/include" # source directory
    DESTINATION "${CMAKE_SOURCE_DIR}/install" # target directory
    FILES_MATCHING # install only matched files
    PATTERN "*.h" # select header files
)

