# console.exe
add_executable(console console.cpp ../Recording.cpp console_res.rc)
target_link_libraries(console comctl32 winmm ole32 avrt ksuser)
