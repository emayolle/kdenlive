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

#include <KLocalizedString>
#include <QApplication>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
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

    connect(m_runButton, &QPushButton::clicked, this, &ScriptEditor::runScript);

    // The keys that are pressed in the editor are watched, Kdenlive has
    // shortcuts for the same keys and they would be triggered instead of the
    // ones of the editor
    qApp->installEventFilter(this);
}

ScriptEditor::~ScriptEditor()
{
    qApp->removeEventFilter(this);
}

bool ScriptEditor::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride && m_view) {
        // The focus can be on the text widget of the editor, not on the view
        QWidget *focus = QApplication::focusWidget();
        const bool inEditor = focus && (focus == m_view || m_view->isAncestorOf(focus));
        if (inEditor) {
            const auto *keyEvent = static_cast<QKeyEvent *>(event);
            switch (ScriptEditorInternal::shortcutHandling(keyEvent)) {
            case ScriptEditorInternal::ShortcutHandling::Accept:
                // The key is handled by the editor, the window of Kdenlive must
                // not see it
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
    if (m_view) {
        m_view->setFocus();
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
