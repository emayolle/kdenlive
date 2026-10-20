# SPDX-FileCopyrightText: 2026 Erwan Mayolle <erwan.mayolle.pro@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

# Python is optional, the parent directory has already looked for it.
if(NOT kdenlive_SCRIPTING_FOUND)
    return()
endif()

set(kdenlive_SRCS
  ${kdenlive_SRCS}
  scripting/kdenliveapi.cpp
  scripting/pythonengine.cpp
  scripting/scriptmanager.cpp
  PARENT_SCOPE)
