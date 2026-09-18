cmake_minimum_required(VERSION 3.12)
project(UncivNotifier)

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)
set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE ALWAYS)
FetchContent_Declare(Td URL https://github.com/tdlib/td/archive/refs/heads/master.tar.gz DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(Td)
FetchContent_Declare(nlohmann_json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)
FetchContent_Declare(tomlplusplus URL https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(tomlplusplus)
add_executable(main src/main.cpp)
target_link_libraries(main PRIVATE Td::TdStatic curl nlohmann_json::nlohmann_json tomlplusplus::tomlplusplus)
set_target_properties(main PROPERTIES
	CXX_STANDARD 20
)
