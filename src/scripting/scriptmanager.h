/*
    SPDX-FileCopyrightText: 2026 Erwan Mayolle <erwan.mayolle.pro@gmail.com>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class KActionCategory;
class QAction;

/**
 * @brief Finds and runs the user scripts.
 *
 * The scripts live in the scripts directory of the user data, and in the
 * scripts directories shipped with Kdenlive. Every python file found there is a
 * plugin: it is imported at startup, and if it defines PLUGIN_ENTRY that
 * function is called, which is how it registers its actions.
 *
 * This class only deals with the file system, the interpreter is driven
 * through PythonEngine.
 */
class ScriptManager : public QObject
{
    Q_OBJECT
public:
    static ScriptManager &instance();

    /** @brief The directories where the scripts are looked for, user first. */
    QStringList scriptDirectories() const;
    /** @brief The python files found in the script directories. */
    QStringList discoverScripts() const;

    /**
     * @brief Runs a single script file.
     * @param path The script to run.
     * @param errorOutput Set with the error message if the script failed.
     * @return true if the script ran without error.
     */
    bool runScript(const QString &path, QString *errorOutput = nullptr);

    /**
     * @brief Imports the scripts of the script directories.
     *
     * A script that fails to load does not prevent the others from loading.
     */
    void loadPlugins();

    /**
     * @brief Builds the actions of the scripts registered in the engine.
     *
     * The actions are added to the given category, they trigger the python
     * callback that the script registered.
     *
     * @param category The category the actions are added to.
     * @return The actions that were created.
     */
    QList<QAction *> buildScriptActions(KActionCategory *category);

Q_SIGNALS:
    /** @brief Emitted when a script could not be loaded. */
    void scriptFailed(const QString &file, const QString &message);

private:
    explicit ScriptManager(QObject *parent = nullptr);
};
