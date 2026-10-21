/*
    SPDX-FileCopyrightText: 2026 Erwan Mayolle <erwan.mayolle.pro@gmail.com>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "catch.hpp"
#include "test_utils.hpp"

#include "scripting/scripteditor.h"

using namespace ScriptEditorInternal;

#include <KActionCategory>
#include <KActionCollection>
#include <KTextEditor/View>
#include <KXmlGuiWindow>

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
    // handles the keys. It cannot be checked here, the window manager gives
    // the focus to a window.
    editor.focusEditor();
    QApplication::processEvents();
    auto *view = editor.findChild<KTextEditor::View *>();
    REQUIRE(view != nullptr);
    REQUIRE(ScriptEditorInternal::textWidgetOf(view) != nullptr);
}

TEST_CASE("Script editor shortcuts", "[Scripting]")
{
    // The editor is in a window, like it is in Kdenlive
    QWidget window;
    window.resize(600, 400);
    auto *editor = new ScriptEditor(&window);
    window.show();
    QApplication::processEvents();

    // The actions of Kdenlive are in the action categories, they must be found
    KActionCollection collection(&window);
    KActionCategory category(QStringLiteral("Test category"), &collection);
    QAction categoryAction(QStringLiteral("Category action"), &window);
    category.addAction(QStringLiteral("category_action"), &categoryAction);
    categoryAction.setShortcut(QKeySequence(QStringLiteral("A")));
    const auto actions = ScriptEditorInternal::windowActions(&window);
    REQUIRE(actions.contains(&categoryAction));

    // The actions of the editor keep their shortcut, they are the ones that
    // handle the keys
    auto *view = editor->findChild<KTextEditor::View *>();
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

    // The actions of the window lose the shortcuts that would steal the keys
    // of the editor
    editor->updateWindowShortcuts(true);
    REQUIRE(categoryAction.shortcut().isEmpty());
    // The action of the editor is left alone
    REQUIRE(editorCopy->shortcut() == QKeySequence(QKeySequence::Copy));

    // The editing shortcuts of the window are taken away too, they would be
    // ambiguous with the ones of the editor
    QAction selectAllAction(QStringLiteral("Select all action"), &window);
    window.addAction(&selectAllAction);
    selectAllAction.setShortcut(QKeySequence(QKeySequence::SelectAll));
    editor->updateWindowShortcuts(true);
    REQUIRE(selectAllAction.shortcut().isEmpty());
    REQUIRE(editorCopy->shortcut() == QKeySequence(QKeySequence::Copy));

    // The shortcuts that are not in conflict are left alone
    QAction otherAction(QStringLiteral("Other action"), &window);
    window.addAction(&otherAction);
    otherAction.setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
    editor->updateWindowShortcuts(true);
    REQUIRE(otherAction.shortcut() == QKeySequence(QStringLiteral("Ctrl+S")));

    // And everything is given back when the editor loses the focus
    editor->updateWindowShortcuts(false);
    REQUIRE(categoryAction.shortcut().toString() == QKeySequence(QStringLiteral("A")).toString());
    REQUIRE(selectAllAction.shortcut().toString() == QKeySequence(QKeySequence::SelectAll).toString());
}
