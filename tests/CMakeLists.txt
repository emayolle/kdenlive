# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: Jean-Baptiste Mardelle <jb@kdenlive.org>, Julius Künzel <julius.kuenzel@kde.org>, Eric Jiang <erjiang@alumni.iu.edu>, Vincent Pinon <vincent.pinon@asygn.com>

include_directories(..)
set(TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR})
configure_file(tests_definitions.h.in tests_definitions.h)
kde_enable_exceptions()

set(KdenliveTest_SOURCES
    avcurvetest.cpp
    audiolevelstasktest.cpp
    bintest.cpp
    cachetest.cpp
    colorscopestest.cpp
    compositiontest.cpp
    dataqueuetest.cpp
    documenttest.cpp
    effectstest.cpp
    effectsgrouptest.cpp
    filetest.cpp # codespell:ignore filetest
    gradienteditwidgettest.cpp
    groupstest.cpp
    hidetest.cpp
    keyframetest.cpp
    markertest.cpp
    mixtest.cpp
    modeltest.cpp
    movetest.cpp
    nestingtest.cpp
    projectpathstest.cpp
    otiotest.cpp
    regressions.cpp
    rendermodeltest.cpp
    renderservertest.cpp
    replacetest.cpp
    selectionstatetest.cpp
    sequencetest.cpp
    snaptest.cpp
    spacertest.cpp
    subtitlestest.cpp
    timelinepreviewtest.cpp
    timelinepreviewtestb.cpp
    timewarptest.cpp
    titlertest.cpp
    treetest.cpp
    trimmingtest.cpp
    utilstest.cpp
)

# The scripting api is only built when the python interpreter is available
if(kdenlive_SCRIPTING_FOUND)
    list(APPEND KdenliveTest_SOURCES scriptingtest.cpp)
endif()

# The script editor needs the text editor widget
if(kdenlive_SCRIPT_EDITOR_FOUND)
    list(APPEND KdenliveTest_SOURCES scripteditortest.cpp)
    find_package(Qt${QT_MAJOR_VERSION} REQUIRED COMPONENTS Test)
endif()

include(ECMAddTests)

# Shared test library
add_library(kdenliveTestSupport OBJECT TestMain.cpp test_utils.cpp abortutil.cpp)
target_link_libraries(kdenliveTestSupport PRIVATE kdenliveLib)
set_property(TARGET kdenliveTestSupport PROPERTY CXX_STANDARD 14)
target_compile_definitions(kdenliveTestSupport PRIVATE QT_FORCE_ASSERTS)

foreach(_source ${KdenliveTest_SOURCES})
  get_filename_component(_targetname ${_source} NAME_WE)
  ecm_add_test(
      ${_source}
      TEST_NAME ${_targetname}
      LINK_LIBRARIES kdenliveTestSupport kdenliveLib
  )
  set_property(TARGET ${_targetname} PROPERTY CXX_STANDARD 14)
endforeach()

# The test of the script editor sends keys to the editor
if(kdenlive_SCRIPT_EDITOR_FOUND)
  target_link_libraries(scripteditortest Qt${QT_MAJOR_VERSION}::Test)
  # The test needs a window, it does not need a display
  set_tests_properties(scripteditortest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

target_sources(timelinepreviewtest PRIVATE timelinepreviewmonitor.cpp)
target_sources(timelinepreviewtestb PRIVATE timelinepreviewmonitor.cpp)
