/*
    SPDX-FileCopyrightText: 2026 Erwan Mayolle <erwan.mayolle.pro@gmail.com>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "scriptmanager.h"
#include "pythonengine.h"

#include <KLocalizedString>
#include <QDir>

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
    for (const auto &directory : scriptDirectories()) {
        QDir dir(directory);
        if (!dir.exists()) {
            continue;
        }
        const auto entries = dir.entryList({QStringLiteral("*.py")}, QDir::Files, QDir::Name);
        for (const auto &entry : entries) {
            scripts.append(dir.absoluteFilePath(entry));
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

void ScriptManager::loadPlugins()
{
    if (!PythonEngine::instance().isInitialized()) {
        return;
    }
    for (const auto &script : discoverScripts()) {
        QString error;
        if (!PythonEngine::instance().importFile(script, &error)) {
            Q_EMIT scriptFailed(script, error);
        }
    }
}
