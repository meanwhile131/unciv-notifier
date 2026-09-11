cmake_minimum_required(VERSION 3.12)
project(UncivNotifier)

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)
FetchContent_Declare(
	td
	GIT_REPOSITORY https://github.com/tdlib/td.git
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(td)
add_executable(main main.cpp)
target_link_libraries(main PRIVATE Td::TdStatic)
