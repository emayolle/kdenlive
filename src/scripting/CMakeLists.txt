# SPDX-FileCopyrightText: 2026 Erwan Mayolle <erwan.mayolle.pro@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

# Python is optional, the parent directory has already looked for it.
if(NOT kdenlive_SCRIPTING_FOUND)
    return()
endif()

list(APPEND kdenlive_SRCS
  scripting/kdenliveapi.cpp
  scripting/pythonengine.cpp
  scripting/scriptmanager.cpp)

# The script editor needs a text editor widget
if(kdenlive_SCRIPT_EDITOR_FOUND)
    list(APPEND kdenlive_SRCS
      scripting/scripteditor.cpp
      scripting/scripteditor.h)
endif()

# The variable is propagated in a single call, a second set() with PARENT_SCOPE
# would replace the result of the first one
set(kdenlive_SRCS ${kdenlive_SRCS} PARENT_SCOPE)
