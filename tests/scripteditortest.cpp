/*
    SPDX-FileCopyrightText: 2026 Erwan Mayolle <erwan.mayolle.pro@gmail.com>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "catch.hpp"
#include "test_utils.hpp"

#include "scripting/scripteditor.h"

using namespace ScriptEditorInternal;

#include <KActionCollection>
#include <KTextEditor/View>

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

TEST_CASE("Script editor shortcuts", "[Scripting]")
{
    ScriptEditor editor;
    editor.resize(600, 400);
    editor.show();
    QApplication::processEvents();

    // The actions of the editor keep their shortcut, they are the ones that
    // handle the keys
    auto *view = editor.findChild<KTextEditor::View *>();
    REQUIRE(view != nullptr);
    QWidget *textWidget = ScriptEditorInternal::textWidgetOf(view);
    REQUIRE(textWidget != nullptr);
    REQUIRE(textWidget->metaObject()->className() == QLatin1String("KateViewInternal"));
    QAction *editorCopy = nullptr;
    for (QAction *action : textWidget->actions()) {
        if (action->objectName() == QLatin1String("edit_copy")) {
            editorCopy = action;
        }
    }
    REQUIRE(editorCopy != nullptr);
    REQUIRE(editorCopy->shortcut() == QKeySequence(QKeySequence::Copy));

    // The actions of the window lose the shortcuts that would swallow the keys
    // of the editor
    QAction windowAction(QStringLiteral("Window action"), &editor);
    editor.addAction(&windowAction);
    windowAction.setShortcut(QKeySequence(QStringLiteral("A")));
    editor.updateWindowShortcuts(true);
    REQUIRE(windowAction.shortcut().isEmpty());
    // The action of the editor is left alone
    REQUIRE(editorCopy->shortcut() == QKeySequence(QKeySequence::Copy));

    // The shortcuts with a modifier are left alone, the editor has the
    // priority over the window for them
    QAction otherAction(QStringLiteral("Other action"), &editor);
    editor.addAction(&otherAction);
    otherAction.setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
    editor.updateWindowShortcuts(true);
    REQUIRE(otherAction.shortcut() == QKeySequence(QStringLiteral("Ctrl+S")));

    // And everything is given back when the editor loses the focus
    editor.updateWindowShortcuts(false);
    REQUIRE(windowAction.shortcut() == QKeySequence(QStringLiteral("A")));
}
