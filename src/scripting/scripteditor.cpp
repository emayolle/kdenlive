/*
    SPDX-FileCopyrightText: 2026 Erwan Mayolle <erwan.mayolle.pro@gmail.com>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "scripteditor.h"
#include "pythonengine.h"

#include <KTextEditor/Application>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <KActionCategory>
#include <KActionCollection>
#include <KLocalizedString>
#include <KXmlGuiWindow>
#include <QApplication>
#include <QCoreApplication>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

namespace {
/** @brief The script the editor starts with, it shows what the api can do. */
const char *const DEFAULT_SCRIPT = "# A script works on the project that is loaded\n"
                                   "import kdenlive\n"
                                   "\n"
                                   "project = kdenlive.project()\n"
                                   "print('Project:', project.name)\n"
                                   "for track in project.tracks():\n"
                                   "    print(track['tag'], track['name'])\n";
} // namespace

namespace {
/** @brief Tells whether a key combination is one of the editing shortcuts. */
bool isEditingShortcut(const QKeyCombination &combination)
{
    static const QList<QKeySequence> sequences{QKeySequence::SelectAll, QKeySequence::Copy, QKeySequence::Cut,
                                               QKeySequence::Paste,     QKeySequence::Undo, QKeySequence::Redo};
    for (const QKeySequence &sequence : sequences) {
        if (sequence.matches(combination)) {
            return true;
        }
    }
    return false;
}
} // namespace

namespace ScriptEditorInternal {
/**
 * @brief The text widget of the editor.
 *
 * The text widget of Kate is not a QTextEdit, it is a widget of its own that
 * holds one. It is the one that handles the keys, the view only inserts the
 * text.
 */
QWidget *textWidgetOf(KTextEditor::View *view)
{
    if (!view) {
        return nullptr;
    }
    for (QWidget *child : view->findChildren<QWidget *>()) {
        if (child->metaObject()->className() == QLatin1String("KateViewInternal")) {
            return child;
        }
    }
    return nullptr;
}

QList<QAction *> windowActions(QWidget *window)
{
    QList<QAction *> actions = window->actions();
    // The actions of Kdenlive are in the action categories, they are not in the
    // collection of the window
    const auto categories = window->findChildren<KActionCategory *>();
    for (KActionCategory *category : categories) {
        actions.append(category->actions());
    }
    if (auto *xmlWindow = qobject_cast<KXmlGuiWindow *>(window)) {
        actions.append(xmlWindow->actionCollection()->actions());
    }
    return actions;
}

ShortcutHandling shortcutHandling(const QKeyEvent *keyEvent)
{
    if (isEditingShortcut(keyEvent->keyCombination())) {
        // The editor has an action for this key, the window of Kdenlive must
        // not handle it. The event is ignored so that the action of the editor
        // is triggered.
        return ShortcutHandling::Ignore;
    }
    if (keyEvent->modifiers() == Qt::NoModifier || keyEvent->modifiers() == Qt::KeypadModifier) {
        // The key is typed in the editor. Kdenlive has shortcuts without
        // modifier, they would swallow the keys.
        return ShortcutHandling::Accept;
    }
    return ShortcutHandling::Pass;
}
bool isConflictingShortcut(const QKeySequence &shortcut)
{
    if (shortcut.isEmpty()) {
        return false;
    }
    for (int i = 0; i < shortcut.count(); i++) {
        const QKeyCombination combination = shortcut[i];
        // The editing shortcuts of the editor
        if (isEditingShortcut(combination)) {
            return true;
        }
        // The shortcuts without modifier are the ones that are typed in the
        // editor
        if (combination.keyboardModifiers() == Qt::NoModifier) {
            return true;
        }
    }
    return false;
}
} // namespace ScriptEditorInternal

ScriptEditor::ScriptEditor(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *toolbar = new QHBoxLayout;
    m_runButton = new QPushButton(i18n("Run script"), this);
    m_runButton->setIcon(QIcon::fromTheme(QStringLiteral("system-run")));
    m_runButton->setToolTip(i18n("Run the script on the current project"));
    toolbar->addWidget(m_runButton);
    toolbar->addStretch(1);
    layout->addLayout(toolbar);

    auto *splitter = new QSplitter(Qt::Vertical, this);

    m_application = new KTextEditor::Application(this);
    auto *editor = KTextEditor::Editor::instance();
    if (editor) {
        // The editor needs to know the application it is embedded in
        editor->setApplication(m_application);
        m_document = editor->createDocument(this);
    }
    if (m_document) {
        // Python highlighting, the syntax definitions come with the syntax
        // highlighting package
        m_document->setMode(QStringLiteral("Python"));
        m_document->setText(QString::fromUtf8(DEFAULT_SCRIPT));
    }
    m_view = m_document ? m_document->createView(splitter) : nullptr;
    if (m_view) {
        m_view->setStatusBarEnabled(false);
        splitter->addWidget(m_view);
        // The text widget of the editor is the one that handles the keys
        m_textWidget = m_textWidget;
    }

    m_console = new QPlainTextEdit(splitter);
    m_console->setReadOnly(true);
    m_console->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_console->setPlaceholderText(i18n("The output of the script appears here"));
    splitter->addWidget(m_console);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    layout->addWidget(splitter);

    // The version of the binary that runs, it is useful to know which build is
    // used when something does not work
    auto *version = new QLabel(QCoreApplication::applicationFilePath(), this);
    version->setTextInteractionFlags(Qt::TextSelectableByMouse);
    version->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 8pt;"));
    layout->addWidget(version);

    connect(m_runButton, &QPushButton::clicked, this, &ScriptEditor::runScript);

    // The keys that are pressed in the editor are watched, Kdenlive has
    // shortcuts for the same keys and they would be triggered instead of the
    // ones of the editor
    qApp->installEventFilter(this);
}

bool ScriptEditor::isEditorAction(QAction *action) const
{
    // The actions of the editor are the ones of the view and the ones of its
    // text widget. They have the same names as the actions of the window, they
    // can only be told apart by the widget they belong to.
    if (m_view && m_view->actions().contains(action)) {
        return true;
    }
    if (m_textWidget && m_textWidget->actions().contains(action)) {
        return true;
    }
    return false;
}

bool ScriptEditor::hasSavedShortcut(QAction *action) const
{
    for (const auto &saved : m_savedShortcuts) {
        if (saved.first.data() == action) {
            return true;
        }
    }
    return false;
}

void ScriptEditor::updateWindowShortcuts(bool inEditor)
{
    QWidget *top = window();
    if (!top) {
        return;
    }
    // The actions of the window lose the shortcuts that would steal the keys
    // of the editor. The actions of the editor are left alone, they are the
    // ones that handle the keys.
    const QList<QAction *> actions = ScriptEditorInternal::windowActions(top);
    for (QAction *action : actions) {
        if (isEditorAction(action)) {
            continue;
        }
        if (inEditor) {
            if (hasSavedShortcut(action)) {
                continue;
            }
            if (!ScriptEditorInternal::isConflictingShortcut(action->shortcut())) {
                continue;
            }
            m_savedShortcuts.append({QPointer<QAction>(action), action->shortcut()});
            action->setShortcut(QKeySequence());
        } else {
            for (int i = 0; i < m_savedShortcuts.size(); i++) {
                if (m_savedShortcuts.at(i).first.data() != action) {
                    continue;
                }
                action->setShortcut(m_savedShortcuts.at(i).second);
                m_savedShortcuts.removeAt(i);
                break;
            }
        }
    }
    // The same for the shortcuts that are not actions
    const QList<QShortcut *> shortcuts = top->findChildren<QShortcut *>();
    for (QShortcut *shortcut : shortcuts) {
        // The shortcuts of the editor are left alone
        auto *parent = qobject_cast<QWidget *>(shortcut->parent());
        if (parent && m_view->isAncestorOf(parent)) {
            continue;
        }
        if (inEditor) {
            if (m_disabledShortcuts.contains(shortcut)) {
                continue;
            }
            if (!ScriptEditorInternal::isConflictingShortcut(shortcut->key())) {
                continue;
            }
            m_disabledShortcuts.append(shortcut);
            shortcut->setEnabled(false);
        } else if (m_disabledShortcuts.contains(shortcut)) {
            m_disabledShortcuts.removeOne(shortcut);
            shortcut->setEnabled(true);
        }
    }
}

ScriptEditor::~ScriptEditor()
{
    // The shortcuts of the window must be given back. The actions can be gone
    // already, the pointer tells it.
    for (const auto &saved : std::as_const(m_savedShortcuts)) {
        if (saved.first) {
            saved.first->setShortcut(saved.second);
        }
    }
    m_savedShortcuts.clear();
    qApp->removeEventFilter(this);
}

bool ScriptEditor::eventFilter(QObject *watched, QEvent *event)
{
    QWidget *textWidget = m_textWidget;
    if (textWidget) {
        // The focus is in the editor when the text widget has it
        const bool inEditor = textWidget->hasFocus() || (QApplication::focusWidget() == textWidget);
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
            updateWindowShortcuts(inEditor);
            if (inEditor != m_lastFocusInEditor) {
                m_lastFocusInEditor = inEditor;
                appendOutput(QStringLiteral("[focus] %1 -> %2")
                                 .arg(inEditor ? i18n("in the editor") : i18n("outside"))
                                 .arg(QApplication::focusWidget() ? QApplication::focusWidget()->metaObject()->className() : i18n("none")));
            }
        }
        if (event->type() == QEvent::ShortcutOverride && inEditor) {
            const auto *keyEvent = static_cast<QKeyEvent *>(event);
            switch (ScriptEditorInternal::shortcutHandling(keyEvent)) {
            case ScriptEditorInternal::ShortcutHandling::Ignore:
                // The editor has an action for this key, it must be
                // triggered. The event is ignored so that the shortcut
                // system looks for an action.
                appendOutput(
                    QStringLiteral("[key] %1 : the editor handles it").arg(QKeySequence(keyEvent->keyCombination()).toString(QKeySequence::NativeText)));
                event->ignore();
                return true;
            case ScriptEditorInternal::ShortcutHandling::Accept:
                // The key is typed in the editor, the window of Kdenlive
                // must not see it
                event->accept();
                return true;
            case ScriptEditorInternal::ShortcutHandling::Pass:
                break;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

QString ScriptEditor::script() const
{
    return m_document ? m_document->text() : QString();
}

void ScriptEditor::setScript(const QString &code)
{
    if (m_document) {
        m_document->setText(code);
    }
}

void ScriptEditor::focusEditor()
{
    if (!m_view) {
        return;
    }
    m_view->setFocus();
    // The focus must be in the text widget of the editor, it is the one that
    // handles the keys
    if (auto *textWidget = m_textWidget) {
        textWidget->setFocus();
    }
}

void ScriptEditor::appendOutput(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    // The console adds the line break itself, the trailing ones are dropped
    QString content = text;
    while (content.endsWith(QLatin1Char('\n'))) {
        content.chop(1);
    }
    if (!content.isEmpty()) {
        m_console->appendPlainText(content);
    }
}

void ScriptEditor::appendError(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    QString content = text;
    while (content.endsWith(QLatin1Char('\n'))) {
        content.chop(1);
    }
    if (!content.isEmpty()) {
        // The error is red, it is what the user has to look at
        m_console->appendHtml(QStringLiteral("<span style=\"color:#c00000;\">%1</span>").arg(content.toHtmlEscaped()));
    }
}

void ScriptEditor::runScript()
{
    const QString code = script();
    if (code.trimmed().isEmpty()) {
        return;
    }
    m_console->clear();
    QString output;
    QString error;
    const bool success = PythonEngine::instance().runStringCapturing(code, QStringLiteral("script"), &output, &error);
    appendOutput(output);
    if (!success) {
        appendError(error);
    }
    Q_EMIT scriptFinished(success);
}
