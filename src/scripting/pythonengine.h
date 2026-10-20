/*
    SPDX-FileCopyrightText: 2026 Erwan Mayolle <erwan.mayolle.pro@gmail.com>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class KdenliveApi;

/**
 * @brief The embedded Python interpreter and the kdenlive module it exposes.
 *
 * There is a single interpreter, shared by the two hosts:
 * - the command line, where a script runs against a project loaded from disk
 * - the graphical interface, where a script runs against the running project
 *
 * The module is the same in both cases, so a script written for one works with
 * the other.
 *
 * The module exposes one type, kdenlive.Project. The data it returns (bin
 * clips, tracks, timeline clips) are plain dictionaries, so that adding a field
 * does not mean adding a type.
 */
class PythonEngine : public QObject
{
    Q_OBJECT
public:
    static PythonEngine &instance();

    /** @brief Starts the interpreter and creates the kdenlive module. */
    bool initialize();
    /** @brief Tells whether the interpreter is running. */
    bool isInitialized() const;

    /**
     * @brief Runs a script file.
     * @param path The script to run.
     * @param errorOutput Set with the error message if the script failed.
     * @return true if the script ran without error.
     */
    bool runFile(const QString &path, QString *errorOutput = nullptr);
    /**
     * @brief Runs a script given as a string.
     * @param code The script source.
     * @param name A name for the script, used in error messages.
     * @param errorOutput Set with the error message if the script failed.
     * @return true if the script ran without error.
     */
    bool runString(const QString &code, const QString &name, QString *errorOutput = nullptr);
    /**
     * @brief Imports a script file as a module and runs its plugin entry point.
     *
     * The file is executed in its own namespace, with __name__ set to the file
     * name, so that it behaves like a module. If it defines PLUGIN_ENTRY, that
     * function is then called, which is how a script registers its actions.
     *
     * @param path The script to import.
     * @param errorOutput Set with the error message if the import failed.
     * @return true if the script was imported without error.
     */
    bool importFile(const QString &path, QString *errorOutput = nullptr);

    /** @brief The api exposed to the scripts, may be null. */
    void setApi(KdenliveApi *api);

    /**
     * @brief Registers an action, callable from a script.
     *
     * A script calls kdenlive.register_action(id, label, callback) to add an
     * entry to the Scripts menu of the graphical interface.
     *
     * @param id A unique identifier for the action.
     * @param label The text shown in the menu.
     * @param callback The Python callable, it is called with the project as
     *                its only argument.
     */
    void registerAction(const QString &id, const QString &label, void *callback);

    /** @brief The ids of the registered actions, in registration order. */
    QStringList actionIds() const;
    /** @brief The label of a registered action. */
    QString actionLabel(const QString &id) const;
    /**
     * @brief Calls a registered action.
     * @param id The id of the action.
     * @return true if the action was found and ran without error.
     */
    bool triggerAction(const QString &id);

    /** @brief The directory where the user scripts are stored. */
    static QString userScriptsDirectory();
    /** @brief The directories where the scripts shipped with Kdenlive are stored. */
    static QStringList systemScriptsDirectories();

private:
    explicit PythonEngine(QObject *parent = nullptr);
    ~PythonEngine() override;

    class Private;
    Private *const d;
};
