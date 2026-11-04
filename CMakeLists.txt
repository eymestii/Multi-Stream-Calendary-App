cmake_minimum_required(VERSION 3.24)
project(StreamPlanner VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(MSVC)
  # Evita exigir que el usuario final instale el Visual C++ Redistributable.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---- Opciones de plataforma ----------------------------------------------------------
# Windows: interfaz Win32 (por defecto). Linux/otros: interfaz Qt6.
option(SP_BUILD_WIN32 "Compilar la interfaz Win32" ${WIN32})
if(WIN32)
  option(SP_BUILD_QT6 "Compilar la interfaz Qt6" OFF)
else()
  option(SP_BUILD_QT6 "Compilar la interfaz Qt6" ON)
endif()
# Linux: usa el SQLite del sistema (Flatpak/AppImage/paquetes de distro compilan sin red).
# Windows: amalgamación oficial integrada como código fuente (comportamiento original).
if(WIN32)
  option(SP_SYSTEM_SQLITE "Usar SQLite del sistema en vez de la amalgamación" OFF)
else()
  option(SP_SYSTEM_SQLITE "Usar SQLite del sistema en vez de la amalgamación" ON)
endif()
if(NOT WIN32)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(SP_SYSTEM_SQLITE)
  find_package(SQLite3 REQUIRED)
  add_library(sqlite3 INTERFACE)
  if(TARGET SQLite3::SQLite3)
    target_link_libraries(sqlite3 INTERFACE SQLite3::SQLite3)
  else()
    target_link_libraries(sqlite3 INTERFACE SQLite::SQLite3)
  endif()
else()
  # SQLite oficial integrado como código fuente; CMake descarga la amalgamación
  # primera vez que se configura el proyecto desde el dominio oficial.
  include(FetchContent)
  if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
  endif()
  set(SQLITE3_SRC_URL "https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip")
  FetchContent_Declare(sqlite3_source
    URL "${SQLITE3_SRC_URL}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  # Descarga desde el dominio oficial sqlite.org; el archivo contiene sqlite3.c y sqlite3.h.
  # No se usa una DLL SQLite instalada por el usuario.
  FetchContent_GetProperties(sqlite3_source)
  if(NOT sqlite3_source_POPULATED)
    FetchContent_Populate(sqlite3_source)
  endif()
  file(GLOB_RECURSE SQLITE3_C_SOURCE CONFIGURE_DEPENDS "${sqlite3_source_SOURCE_DIR}/sqlite3.c")
  file(GLOB_RECURSE SQLITE3_HEADER CONFIGURE_DEPENDS "${sqlite3_source_SOURCE_DIR}/sqlite3.h")
  if(NOT SQLITE3_C_SOURCE OR NOT SQLITE3_HEADER)
    message(FATAL_ERROR "No se encontraron sqlite3.c y sqlite3.h en la amalgamación descargada.")
  endif()
  get_filename_component(SQLITE3_INCLUDE_DIR "${SQLITE3_HEADER}" DIRECTORY)
  add_library(sqlite3 STATIC ${SQLITE3_C_SOURCE})
  target_include_directories(sqlite3 PUBLIC "${SQLITE3_INCLUDE_DIR}")
  target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_DEFAULT_FOREIGN_KEYS=1 SQLITE_DQS=0)
  if(MSVC)
    target_compile_options(sqlite3 PRIVATE /W3 /utf-8)
  else()
    target_compile_options(sqlite3 PRIVATE -w)
  endif()
endif()

add_library(sp_core STATIC
  src/core/text.cpp src/core/store.cpp src/core/persistence.cpp
  src/core/paths.cpp src/core/planner.cpp
  src/core/integrations/http.cpp
  src/core/integrations/api_util.cpp
  src/core/integrations/config.cpp
  src/core/integrations/credentials.cpp
  src/core/integrations/crypto.cpp
  src/core/integrations/secure_file.cpp
  src/core/integrations/url.cpp
  src/core/integrations/token_store.cpp
  src/core/integrations/twitch_client.cpp
  src/core/integrations/kick_client.cpp)
target_include_directories(sp_core PUBLIC src)
target_include_directories(sp_core PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
target_link_libraries(sp_core PRIVATE sqlite3)
if(MSVC)
  target_compile_options(sp_core PRIVATE /utf-8 /W4)
else()
  target_compile_options(sp_core PRIVATE -Wall -Wextra)
endif()

if(WIN32 AND SP_BUILD_WIN32)
  add_executable(StreamPlanner WIN32
    src/platform/win32/main.cpp
    src/platform/win32/winutil.cpp
    src/platform/win32/controls.cpp
    src/platform/win32/view.cpp
    src/platform/win32/actions.cpp
    src/platform/win32/integration/win_async.cpp
    src/platform/win32/integration/winhttp_client.cpp
    src/platform/win32/integration/win_platform_services.cpp
    src/platform/win32/integration/credentials_dialog.cpp
    src/platform/win32/integration/publish_dialog.cpp)
  target_link_libraries(StreamPlanner PRIVATE sp_core sqlite3 user32 gdi32 comctl32 winhttp ws2_32 shell32)
  target_compile_definitions(StreamPlanner PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0601)
  if(MSVC)
    target_compile_options(StreamPlanner PRIVATE /utf-8 /W4 $<$<CONFIG:Release>:/O1 /GL>)
    target_link_options(StreamPlanner PRIVATE $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)
  else()
    target_compile_options(StreamPlanner PRIVATE -Wall -Wextra $<$<CONFIG:Release>:-Os -ffunction-sections -fdata-sections>)
    target_link_options(StreamPlanner PRIVATE -municode -mwindows $<$<CONFIG:Release>:-s -Wl,--gc-sections>)
  endif()
endif()

if(SP_BUILD_QT6)
  add_subdirectory(src/platform/qt6)
endif()

# ---- Pruebas del core (portables; sin UI) ----------------------------------------------------
option(SP_BUILD_TESTS "Compilar las pruebas del core" OFF)
if(SP_BUILD_TESTS)
  enable_testing()
  add_executable(core_test tests/core_test.cpp)
  target_link_libraries(core_test PRIVATE sp_core)
  add_test(NAME core COMMAND core_test)
  if(NOT WIN32)
    add_test(NAME decoupling COMMAND ${CMAKE_SOURCE_DIR}/tests/check_decoupling.sh ${CMAKE_SOURCE_DIR})
  endif()
endif()
