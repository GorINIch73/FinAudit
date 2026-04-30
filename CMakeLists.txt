# Задаём минимальную требуемую версию CMake
cmake_minimum_required(VERSION 3.20)

# Определяем проект
project(FinancialAudit LANGUAGES C CXX)

# Устанавливаем префикс установки по умолчанию, если он не задан
# Это гарантирует, что CMAKE_INSTALL_DATADIR будет абсолютным путем
if(CMAKE_HOST_UNIX AND NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Default install path")
endif()


# Устанавливаем стандарт C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Включаем FetchContent для управления зависимостями
include(FetchContent)

# --- Зависимости ---

# 1. ImGui
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG docking
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
  FetchContent_Populate(imgui)
endif()

# 2. Поиск системных библиотек
find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(X11 REQUIRED)

# --- Исходные файлы ---

# Create a static library for ImGui
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)

target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp
)

set(APP_SOURCES
    src/main.cpp
    src/UIManager.cpp
    src/DatabaseManager.cpp
    src/ImGuiFileDialog.cpp
    src/ImportManager.cpp
    src/ExportManager.cpp
    src/PdfReporter.cpp
    src/pdfgen.c
    src/CustomWidgets.cpp
    src/views/PaymentsView.cpp
    src/views/KosguView.cpp
    src/views/CounterpartiesView.cpp
    src/views/ContractsView.cpp
    src/views/BasePaymentsView.cpp
    src/views/JO4ImportMapView.cpp
    src/views/ReconciliationView.cpp
    src/views/SqlQueryView.cpp
    src/views/SettingsView.cpp
    src/views/ImportMapView.cpp
    src/views/RegexesView.cpp
    src/views/SelectiveCleanView.cpp
    src/views/SuspiciousWordsView.cpp
    src/views/SpecialQueryView.cpp
    src/views/ContractRegistryNumbersView.cpp
    src/views/ExpenseStructureView.cpp
)

set_source_files_properties(src/pdfgen.c PROPERTIES LANGUAGE C)

# Добавляем исполняемый файл
add_executable(${PROJECT_NAME} ${APP_SOURCES})

# --- Подключение библиотек ---

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    src/views
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# Подключаем все необходимые библиотеки к исполняемому файлу
target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    OpenGL::GL
    SQLite::SQLite3
    X11::X11
    imgui_lib
)

# --- Установка ---

include(GNUInstallDirs)

# Определяем путь для установки данных приложения (шрифтов)
set(INSTALL_DATA_DIR ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME})

# Установка исполняемого файла
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Установка каталога с данными (шрифтами, иконкой)
install(DIRECTORY data/
    DESTINATION ${INSTALL_DATA_DIR}
)

# Установка иконки в стандартную директорию иконок
install(FILES data/FnAudit.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME ${PROJECT_NAME}.svg
)

# Установка .desktop файла
install(FILES FnAudit.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

# Передаём путь к данным в C++ код через макрос препроцессора
set(INSTALL_DATA_DIR_DEFINE "INSTALL_DATA_DIR=\"/usr/local/share/FinancialAudit\"")
target_compile_definitions(${PROJECT_NAME} PRIVATE ${INSTALL_DATA_DIR_DEFINE})
