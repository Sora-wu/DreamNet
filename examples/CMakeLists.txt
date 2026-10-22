add_executable(echoServer echoServer.cpp)
add_executable(echoClient echoClient.cpp)
target_link_libraries(echoServer PRIVATE Dream::Net)
target_link_libraries(echoClient PRIVATE Dream::Net)

if (WIN32)
    foreach(example IN ITEMS echoServer echoClient)
        add_custom_command(TARGET ${example} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:DreamNet> $<TARGET_FILE_DIR:${example}>
        )
    endforeach()
endif()
