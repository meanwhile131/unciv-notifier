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
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(json)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)
add_executable(main main.cpp)
target_link_libraries(main PRIVATE Td::TdStatic curl nlohmann_json::nlohmann_json)
