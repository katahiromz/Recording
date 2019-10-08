# win.exe
add_executable(win WIN32 win.cpp ../Recording.cpp win_res.rc)
target_link_libraries(win comctl32 winmm ole32 avrt ksuser)
