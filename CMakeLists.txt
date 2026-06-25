cmake_minimum_required(VERSION 3.20)
project(SimpleCoder VERSION 0.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# --- Dependencies (system packages) ---
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# ncurses (wide-char) via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(NCURSESW REQUIRED IMPORTED_TARGET ncursesw)

# nlohmann/json — vendored single header in third_party/
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_SOURCE_DIR}/third_party)

# Version macro for --version
add_compile_definitions(SIMPLECODER_VERSION="${PROJECT_VERSION}")

# --- Core library (everything except main(), so tests can link it) ---
# Add new source files here as Phase 1 progresses.
add_library(llm_core STATIC
  src/app/App.cpp
  src/app/Attachments.cpp
  src/app/Command.cpp
  src/app/Config.cpp
  src/app/Skill.cpp
  src/app/SystemPrompt.cpp
  src/agent/Agent.cpp
  src/agent/Confirm.cpp
  src/agent/Tool.hpp
  src/agent/tools/ReadFileTool.cpp
  src/agent/tools/WriteFileTool.cpp
  src/agent/tools/RunBashTool.cpp
  src/agent/tools/ListDirTool.cpp
  src/agent/tools/GrepSearchTool.cpp
  src/agent/tools/StrReplaceTool.cpp
  src/agent/tools/MakeDirTool.cpp
  src/agent/tools/FetchUrlTool.cpp
  src/agent/tools/WebSearchTool.cpp
  src/agent/tools/McpTool.cpp
  src/agent/tools/Tools.cpp
  src/mcp/Transport.cpp
  src/mcp/McpClient.cpp
  src/net/HttpClient.cpp
  src/net/Html.cpp
  src/llm/ChatRequest.cpp
  src/llm/ChatResponse.cpp
  src/llm/ApiClient.cpp
  src/util/Log.cpp
  src/util/Utf8.cpp
  src/util/Base64.cpp
  src/util/Diff.cpp
  src/ui/TextWrap.cpp
  src/ui/Markdown.cpp
  src/ui/ChatView.cpp
  src/ui/InputBar.cpp
  src/ui/ConfirmDialog.cpp
  src/ui/Tui.cpp
  src/ui/Theme.cpp
  src/ui/Banner.cpp
)

target_include_directories(llm_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(llm_core PUBLIC
  CURL::libcurl
  SQLite::SQLite3
  PkgConfig::NCURSESW
  nlohmann_json
  Threads::Threads
)

# --- Executable ---
add_executable(SimpleCoder src/main.cpp)
target_link_libraries(SimpleCoder PRIVATE llm_core)

# --- Tests ---
option(BUILD_TESTS "Build unit tests" OFF)

if(BUILD_TESTS)
  enable_testing()
  find_package(Catch2 3 REQUIRED)
  include(Catch)

  add_executable(unit_tests
    tests/test_config.cpp
    tests/test_message.cpp
    tests/test_chat_request.cpp
    tests/test_chat_response.cpp
    tests/test_api_client.cpp
    tests/test_thread_queue.cpp
    tests/test_log.cpp
    tests/test_chat_view.cpp
    tests/test_agent_chat.cpp
    tests/test_tools.cpp
    tests/test_confirm_policy.cpp
    tests/test_agent_tool_loop.cpp
    tests/test_banner.cpp
    tests/test_command.cpp
    tests/test_attachments.cpp
    tests/test_input.cpp
    tests/test_utf8.cpp
    tests/test_markdown.cpp
    tests/test_system_prompt.cpp
    tests/test_skills.cpp
    tests/test_web_tools.cpp
    tests/test_mcp.cpp
    tests/test_base64.cpp
    tests/test_diff.cpp
  )
  target_link_libraries(unit_tests PRIVATE llm_core Catch2::Catch2WithMain)
  # Catch2 exits 4 when a case only SKIPs (e.g. an [integration] test whose server
  # is unreachable); tell CTest to treat that as skipped, not failed, so the suite
  # stays green offline under `ctest` as well as the whole-binary run.
  catch_discover_tests(unit_tests PROPERTIES SKIP_RETURN_CODE 4)
endif()
