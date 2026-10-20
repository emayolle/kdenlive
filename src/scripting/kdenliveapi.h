/*
    SPDX-FileCopyrightText: 2026 Erwan Mayolle <erwan.mayolle.pro@gmail.com>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class Core;

/** @brief Description of a bin clip, as exposed to scripts. */
struct ScriptBinClip
{
    QString id;
    QString name;
    QString type;
    QString producer;
    int duration{0};
    bool isSequence{false};
};

/** @brief Description of a timeline track, as exposed to scripts. */
struct ScriptTrack
{
    int id{-1};
    QString name;
    bool isAudio{false};
    bool isVideo{false};
    int clipCount{0};
    int duration{0};
};

/** @brief Description of a timeline clip, as exposed to scripts. */
struct ScriptClip
{
    int id{-1};
    int trackId{-1};
    int position{0};
    int duration{0};
    QString binId;
    QString name;
    bool isAudio{false};
    bool isVideo{false};
};

/**
 * @brief The scripting interface to the currently loaded project.
 *
 * This is the seam between the scripting engine and the application. Everything
 * that a script is allowed to do goes through this interface, which keeps the
 * knowledge of the application internals (KdenliveDoc, ProjectItemModel,
 * TimelineItemModel) in one place.
 *
 * The interface is meant to be usable in two hosts:
 * - in the GUI, where it exposes the running project
 * - headless, where it exposes a project loaded from disk
 *
 * All the methods work without a MainWindow, so the same code drives both.
 */
class KdenliveApi : public QObject
{
    Q_OBJECT
public:
    explicit KdenliveApi(Core *core, QObject *parent = nullptr);

    /** @brief Returns true if a project is loaded. */
    bool hasProject() const;

    /** @brief The project file path, empty if the project was never saved. */
    QString projectPath() const;
    /** @brief The project display name. */
    QString projectName() const;
    /** @brief The video profile of the project, for instance atsc_1080p_25. */
    QString profileName() const;
    /** @brief The project duration, in frames. */
    int projectDuration() const;
    /** @brief The number of frames per second of the project. */
    double projectFps() const;

    /** @brief The clips of the bin. */
    QList<ScriptBinClip> binClips() const;
    /** @brief The clip of the bin with the given id, if any. */
    ScriptBinClip binClip(const QString &binId) const;
    /**
     * @brief Imports a file into the bin.
     * @param url A local file path or url.
     * @param name An optional name for the clip.
     * @return The id of the new bin clip, empty on failure.
     */
    QString addClipToBin(const QString &url, const QString &name = {});
    /** @brief Removes a clip from the bin. */
    bool removeClipFromBin(const QString &binId);
    /** @brief Creates a subclip of a bin clip and returns the new bin id. */
    QString addSubClip(const QString &binId, int in, int out);

    /** @brief The tracks of the timeline. */
    QList<ScriptTrack> tracks() const;
    /** @brief Adds a track to the timeline. */
    int addTrack(const QString &name = {});
    /** @brief The clips of a track. */
    QList<ScriptClip> clips(int trackId) const;
    /** @brief The clip of a track at the given position, if any. */
    ScriptClip clipAt(int trackId, int position) const;
    /**
     * @brief Inserts a bin clip in the timeline.
     * @return The id of the new timeline clip, -1 on failure.
     */
    int insertClip(const QString &binId, int trackId, int position);
    /** @brief Removes the clip of a track at the given position. */
    bool removeClip(int trackId, int position);
    /** @brief The duration of a track, in frames. */
    int trackDuration(int trackId) const;
    /** @brief The playhead position, in frames. */
    int playheadPosition() const;
    /** @brief Moves the playhead. */
    void setPlayheadPosition(int position);

    /** @brief Saves the project. */
    bool save(const QString &path = {});

    /** @brief The last error message, empty if the last operation succeeded. */
    QString lastError() const;

private:
    void setError(const QString &message);

    Core *m_core;
    QString m_lastError;
};
