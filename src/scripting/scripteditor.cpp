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
ShortcutHandling shortcutHandling(const QKeyEvent *keyEvent)
{
    if (isEditingShortcut(keyEvent->keyCombination())) {
        // The editor handles the key itself, the window of Kdenlive must not
        // see the shortcut
        return ShortcutHandling::Accept;
    }
    if (keyEvent->modifiers() == Qt::NoModifier || keyEvent->modifiers() == Qt::KeypadModifier) {
        // The key is handled by the editor. Kdenlive has shortcuts without
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

void ScriptEditor::updateWindowShortcuts(bool inEditor)
{
    QWidget *top = window();
    if (!top) {
        return;
    }
    // The actions of the window lose their shortcut while the editor has the
    // focus, they would steal the keys. The actions of the editor are not in
    // the collection of the window, they are left alone.
    QList<QAction *> actions;
    if (auto *xmlWindow = qobject_cast<KXmlGuiWindow *>(top)) {
        actions = xmlWindow->actionCollection()->actions();
    } else {
        actions = top->actions();
    }
    for (QAction *action : actions) {
        if (inEditor) {
            if (m_savedShortcuts.contains(action)) {
                continue;
            }
            if (!ScriptEditorInternal::isConflictingShortcut(action->shortcut())) {
                continue;
            }
            m_savedShortcuts.insert(action, action->shortcut());
            action->setShortcut(QKeySequence());
        } else if (m_savedShortcuts.contains(action)) {
            action->setShortcut(m_savedShortcuts.take(action));
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
    // The shortcuts of the window must be given back
    updateWindowShortcuts(false);
    qApp->removeEventFilter(this);
}

bool ScriptEditor::eventFilter(QObject *watched, QEvent *event)
{
    if (m_view) {
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
            // The focus can be on the text widget of the editor, not on the view
            QWidget *focus = m_view->focusWidget();
            const bool inEditor = focus && (focus == m_view || m_view->isAncestorOf(focus));
            updateWindowShortcuts(inEditor);
            if (inEditor != m_lastFocusInEditor) {
                m_lastFocusInEditor = inEditor;
                appendOutput(QStringLiteral("[focus] %1 -> %2")
                                 .arg(inEditor ? i18n("in the editor") : i18n("outside"))
                                 .arg(focus ? focus->metaObject()->className() : i18n("none")));
            }
        }
        if (event->type() == QEvent::ShortcutOverride) {
            // The focus can be on the text widget of the editor, not on the view
            QWidget *focus = m_view->focusWidget();
            const bool inEditor = focus && (focus == m_view || m_view->isAncestorOf(focus));
            const auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (inEditor) {
                switch (ScriptEditorInternal::shortcutHandling(keyEvent)) {
                case ScriptEditorInternal::ShortcutHandling::Accept:
                    // The key is handled by the editor, the window of Kdenlive
                    // must not see it
                    appendOutput(
                        QStringLiteral("[key] %1 : handled by the editor").arg(QKeySequence(keyEvent->keyCombination()).toString(QKeySequence::NativeText)));
                    event->accept();
                    return true;
                case ScriptEditorInternal::ShortcutHandling::Pass:
                    break;
                }
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
    // The text widget of the editor is the one that handles the keys, the view
    // only inserts the text
    if (auto *textWidget = m_view->findChild<QTextEdit *>()) {
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
