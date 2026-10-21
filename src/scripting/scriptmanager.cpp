/*
    SPDX-FileCopyrightText: 2026 Erwan Mayolle <erwan.mayolle.pro@gmail.com>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "scriptmanager.h"
#include "pythonengine.h"

#include <KActionCategory>
#include <KLocalizedString>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>

ScriptManager::ScriptManager(QObject *parent)
    : QObject(parent)
{
}

ScriptManager &ScriptManager::instance()
{
    static ScriptManager manager;
    return manager;
}

QStringList ScriptManager::scriptDirectories() const
{
    QStringList directories;
    // The user scripts come first, so that they can override a shipped script
    const QString userDirectory = PythonEngine::userScriptsDirectory();
    if (!userDirectory.isEmpty()) {
        directories.append(userDirectory);
    }
    directories.append(PythonEngine::systemScriptsDirectories());
    return directories;
}

QStringList ScriptManager::discoverScripts() const
{
    QStringList scripts;
    QSet<QString> seen;
    for (const auto &directory : scriptDirectories()) {
        QDir dir(directory);
        if (!dir.exists()) {
            continue;
        }
        const auto entries = dir.entryList({QStringLiteral("*.py")}, QDir::Files, QDir::Name);
        for (const auto &entry : entries) {
            const QString path = dir.absoluteFilePath(entry);
            // The same directory can be found twice, for instance when the user
            // directory is the installation one
            if (seen.contains(path)) {
                continue;
            }
            seen.insert(path);
            scripts.append(path);
        }
    }
    return scripts;
}

bool ScriptManager::runScript(const QString &path, QString *errorOutput)
{
    if (!PythonEngine::instance().isInitialized()) {
        if (errorOutput) {
            *errorOutput = i18n("The Python interpreter is not running");
        }
        return false;
    }
    return PythonEngine::instance().runFile(path, errorOutput);
}

bool ScriptManager::isPlugin(const QString &path)
{
    // The scripts of the directories are not all plugins, some of them are
    // command line tools. Only the ones that register actions are loaded, the
    // others would fail on their optional dependencies.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QByteArray content = file.readAll();
    file.close();
    return content.contains("PLUGIN_ENTRY");
}

void ScriptManager::loadPlugins()
{
    if (!PythonEngine::instance().isInitialized()) {
        return;
    }
    for (const auto &script : discoverScripts()) {
        if (!isPlugin(script)) {
            continue;
        }
        QString error;
        if (!PythonEngine::instance().importFile(script, &error)) {
            Q_EMIT scriptFailed(script, error);
        }
    }
}

QList<QAction *> ScriptManager::buildScriptActions(KActionCategory *category)
{
    QList<QAction *> actions;
    const auto &engine = PythonEngine::instance();
    for (const auto &id : engine.actionIds()) {
        auto *action = new QAction(engine.actionLabel(id), this);
        action->setData(id);
        connect(action, &QAction::triggered, this, [this, id]() {
            if (!PythonEngine::instance().triggerAction(id)) {
                Q_EMIT scriptFailed(id, i18n("The script action failed"));
            }
        });
        if (category) {
            category->addAction(QStringLiteral("script_") + id, action);
        }
        actions.append(action);
    }
    return actions;
}
