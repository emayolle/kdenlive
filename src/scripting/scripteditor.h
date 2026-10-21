/*
    SPDX-FileCopyrightText: 2026 Erwan Mayolle <erwan.mayolle.pro@gmail.com>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QWidget>

class QKeyEvent;

namespace KTextEditor {
class Application;
class Document;
class View;
} // namespace KTextEditor
class QPlainTextEdit;
class QPushButton;

namespace ScriptEditorInternal {
/** @brief What to do with a key that is pressed in the editor. */
enum class ShortcutHandling {
    Pass,   /**< The window of Kdenlive handles the shortcut. */
    Accept, /**< The key is typed in the editor. */
    Ignore, /**< The editor has its own shortcut for this key. */
};

/**
 * @brief Tells what to do with a key that is pressed in the editor.
 *
 * Kdenlive has shortcuts for the keys that are used in the editor, they must
 * not steal them.
 */
ShortcutHandling shortcutHandling(const QKeyEvent *keyEvent);
} // namespace ScriptEditorInternal

/**
 * @brief A panel to write and run a python script.
 *
 * The script is run in the embedded interpreter, on the project that is
 * currently loaded. The output of the script is shown in a console below
 * the editor.
 */
class ScriptEditor : public QWidget
{
    Q_OBJECT
public:
    explicit ScriptEditor(QWidget *parent = nullptr);
    ~ScriptEditor() override;

    /** @brief Runs the content of the editor. */
    void runScript();
    /** @brief Puts some text in the editor, replacing its content. */
    void setScript(const QString &code);
    /** @brief The content of the editor. */
    QString script() const;
    /** @brief Gives the keyboard focus to the text editor. */
    void focusEditor();

protected:
    /** @brief Keeps the editing shortcuts in the editor.
     *
     * Kdenlive has its own shortcuts for the same keys, they must not be
     * triggered while the user writes a script.
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

Q_SIGNALS:
    /** @brief The script was run, the output is in the console. */
    void scriptFinished(bool success);

private:
    /** @brief Adds the output of the script to the console. */
    void appendOutput(const QString &text);
    /** @brief Adds the error of the script to the console. */
    void appendError(const QString &text);
    /** @brief Gives the editing shortcuts to the editor. */
    void installEditingShortcuts();

    KTextEditor::Application *m_application{nullptr};
    KTextEditor::Document *m_document{nullptr};
    KTextEditor::View *m_view{nullptr};
    QPlainTextEdit *m_console{nullptr};
    QPushButton *m_runButton{nullptr};
};
