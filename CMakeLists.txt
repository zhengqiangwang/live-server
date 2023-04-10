cmake_minimum_required( VERSION 3.16)

project( server_media 
         VERSION 0.1.0
         DESCRIPTION "Description of project"
         HOMEPAGE_URL "http://www.abc.com"
         LANGUAGES CXX )
         
#execute_process(COMMAND g++ -std=c++20 -xc++-system-header -fmodules-ts
#         iostream
#         string
#)
         
add_executable( server_media )

add_definitions("-Wall -g")

target_sources( server_media PRIVATE 
                buffer.cpp
                buffer.h
                core.cpp
                core.h
                error.cpp
                error.h
                log.cpp
                log.h
                protocol_log.cpp
                protocol_log.h
                protocol_utility.cpp
                protocol_utility.h
                utility.cpp
                utility.h
                core_time.cpp
                core_time.h
                consts.h
                performance.h
                kbps.cpp
                kbps.h
                app_log.cpp
                app_log.h
                app_reload.cpp
                app_reload.h
                app_thread.cpp
                app_thread.h
                app_hourglass.cpp
                app_hourglass.h
                app_st.cpp
                app_st.h
                app_hybrid.cpp
                app_hybrid.h
                app_utility.cpp
                app_utility.h
                protocol_json.cpp
                protocol_json.h
                protocol_amf0.cpp
                protocol_amf0.h
                app_server.cpp
                app_server.h
                protocol_st.cpp
                protocol_st.h
                protocol_io.cpp
                protocol_io.h
                io.cpp
                io.h
                core_autofree.h
                app_source.cpp
                app_source.h
                flv.cpp
                flv.h
                codec.cpp
                codec.h
                file.cpp
                file.h
                protocol_format.cpp
                protocol_format.h
                app_config.cpp
                app_config.h
                protocol_rtmp_stack.cpp
                protocol_rtmp_stack.h
                core_performance.cpp
                core_performance.h
                protocol_stream.cpp
                protocol_stream.h
                protocol_rtmp_handshak.cpp
                protocol_rtmp_handshak.h
                app_listener.cpp
                app_listener.h
                app_conn.cpp
                app_conn.h
                protocol_conn.cpp
                protocol_conn.h
                app_heartbeat.cpp
                app_heartbeat.h
                protocol_http_stack.cpp
                protocol_http_stack.h
                protocol_http_client.cpp
                protocol_http_client.h
                protocol_kbps.cpp
                protocol_kbps.h
                protocol_http_conn.cpp
                protocol_http_conn.h
                protocol_rtmp_conn.cpp
                protocol_rtmp_conn.h
                app_rtmp_conn.cpp
                app_rtmp_conn.h
                app_security.cpp
                app_security.h
                app_refer.cpp
                app_refer.h
                app_statistic.cpp
                app_statistic.h
                app_recv_thread.cpp
                app_recv_thread.h
                app_http_conn.cpp
                app_http_conn.h
                app_http_hooks.cpp
                app_http_hooks.h
                protocol_rtmp_msg_array.cpp
                protocol_rtmp_msg_array.h
                app_pithy_print.cpp
                app_pithy_print.h
                app_async_call.cpp
                app_async_call.h
                app_coworkers.cpp
                app_coworkers.h
                app_encoder.cpp
                app_encoder.h
                app_ffmpeg.cpp
                app_ffmpeg.h
                app_process.cpp
                app_process.h
                app_forward.cpp
                app_forward.h
                app_dvr.cpp
                app_dvr.h
                app_fragment.cpp
                app_fragment.h
                app_dash.cpp
                app_dash.h
                app_http_stream.cpp
                app_http_stream.h
                database.cpp
                database.h
                app_service_conn.cpp
                app_service_conn.h
                types.h
                main.cpp )
                
target_compile_features( server_media PRIVATE cxx_std_20 )

target_include_directories( server_media PRIVATE ./openssl/include ./srtp2/include ./srt/include ./opus/include ./ffmpeg/include ./include)

target_link_directories( server_media PRIVATE ./openssl/lib ./srtp2/lib ./srt/lib ./opus/lib ./ffmpeg/lib ./lib)

target_link_libraries( server_media PRIVATE mysqlclient pthread libst.a libssl.a libcrypto.a libsrtp2.a libsrt.a libopus.a libavcodec.a libavutil.a libswresample.a dl)

#target_compile_options( server_media PRIVATE -fmodules-ts )

#target_include_directories( server_media PRIVATE /usr/include )

#target_link_directories( server_media PRIVATE /usr/lib )

#target_link_libraries( server_media PRIVATE m )

#set_target_properties( server_media PROPERTIES
            #CXX_STANDARD 20
            #CXX_EXTENSIONS OFF
            #etc..
            #)
# 设置指定的C++编译器版本是必须的，如果不设置，或者为OFF，则指定版本不可用时，会使用上一版本。
#set_target_properties(server_media PROPERTIES  CXX_STANDARD_REQUIRED on)


## --------------------------------------------------------------------------
## Adding build-requirements
## 源文件列表
#target_sources( <target> PRIVATE <source-file>... )
#
#target_precompile_headers( <target> PRIVATE <header-file>... )
#
## 预处理头文件搜索路径列表，等价参数：-I
#target_include_directories( <target> PRIVATE <include-search-dir>... )  
#
## 预编译宏 自动在指定的宏前添加，等价参数：-D
#target_compile_definitions( <target> PRIVATE <macro-definitions>... )  
#
## 编译选项和特性
#target_compile_options( <target> PRIVATE <compiler-option>... )
#target_compile_features( <target> PRIVATE <feature>... )
#
## 链接库
#target_link_libraries( <target> PRIVATE <dependency>... )
#
## 链接选项
#target_link_options( <target> PRIVATE <linker-option>... )
#
## 链接库搜索路径，等价参数：-L
#target_link_directories( <target> PRIVATE <linker-search-dir>... )

#execute_process(COMMAND ln -s /opt/gcc/gcm.cache  ./)

## Modern Cmake建议围绕Target和Property进行项目配置，竭力避免出现下面变量variable的set定义。
## Variable横行是典型CMake2.8时期的风格, Modern CMake则遵循了OO方法。其中，target表示待构建的对象，
## target相关的语句用于定义来约束link、compile等相关属性的scope。
#set(CMAKE_CXX_STANDARD 20)
#set(CMAKE_CXX_STANDARD_REQUIRED ON)

#SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
