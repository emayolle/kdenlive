/*
    SPDX-FileCopyrightText: 2026 Erwan Mayolle <erwan.mayolle.pro@gmail.com>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "catch.hpp"
#include "test_utils.hpp"

#include "scripting/scripteditor.h"

#include <QApplication>
#include <QTest>

using namespace fakeit;

TEST_CASE("Script editor", "[Scripting]")
{
    ScriptEditor editor;
    editor.resize(600, 400);
    editor.show();
    QApplication::processEvents();

    // The editor has a document, it shows a script by default
    REQUIRE(editor.focusWidget() != nullptr);
    REQUIRE(!editor.script().isEmpty());

    // The script can be replaced
    editor.setScript(QStringLiteral("print(1)"));
    REQUIRE(editor.script() == QStringLiteral("print(1)"));

    // The focus goes to the text widget of the editor, it is the one that
    // handles the keys
    editor.focusEditor();
    QApplication::processEvents();
    REQUIRE(QApplication::focusWidget() == editor.focusWidget());
}
