add_executable(echoServer echoServer.cpp)
add_executable(echoClient echoClient.cpp)
target_link_libraries(echoServer PRIVATE Dream::Net)
target_link_libraries(echoClient PRIVATE Dream::Net)
