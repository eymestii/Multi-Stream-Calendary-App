# Interfaz gráfica Qt6 (Linux: AppImage / Flatpak). Solo depende de sp_core.
# Nada de Qt entra en src/core, y nada de src/core se modifica desde aquí.
find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Network)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(stream-planner
  main.cpp
  mainwindow.cpp
  mainwindow.h
  qtutil.h
  integration/qt_http_client.cpp
  integration/qt_http_client.h
  integration/qt_platform_services.cpp
  integration/qt_platform_services.h
  integration/integration_controller.cpp
  integration/integration_controller.h
  integration/credentials_dialog.cpp
  integration/credentials_dialog.h
  integration/publishpanel.cpp
  integration/publishpanel.h
  resources/app.qrc)
target_link_libraries(stream-planner PRIVATE sp_core Qt6::Widgets Qt6::Network)
target_compile_definitions(stream-planner PRIVATE
  QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII QT_USE_QSTRINGBUILDER
  SP_VERSION="${PROJECT_VERSION}")
target_compile_options(stream-planner PRIVATE -Wall -Wextra
  $<$<CONFIG:Release>:-O2 -ffunction-sections -fdata-sections>)
target_link_options(stream-planner PRIVATE $<$<CONFIG:Release>:-Wl,--gc-sections>)

# ---- Instalación (la usan AppImage, Flatpak y paquetes de distro) -------------------------
include(GNUInstallDirs)
set(SP_APP_ID io.github.mestisexo.StreamPlanner)
set(SP_PKG_DIR ${PROJECT_SOURCE_DIR}/packaging/linux)
install(TARGETS stream-planner RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${SP_PKG_DIR}/${SP_APP_ID}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${SP_PKG_DIR}/${SP_APP_ID}.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES ${SP_PKG_DIR}/${SP_APP_ID}.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES ${SP_PKG_DIR}/${SP_APP_ID}.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
# Plantilla de credenciales (el usuario la copia a ~/.config/StreamPlanner/integrations.json).
install(FILES ${PROJECT_SOURCE_DIR}/config/integrations.example.json
        DESTINATION ${CMAKE_INSTALL_DATADIR}/stream-planner)
