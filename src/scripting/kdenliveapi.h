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
    bool isSubClip{false};
};

/** @brief Description of a timeline track, as exposed to scripts. */
struct ScriptTrack
{
    int id{-1};
    QString name;
    QString tag;
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
    /**
     * @brief Creates a subclip of a bin clip and returns the new bin id.
     *
     * A subclip is a zone of a bin clip, it can be inserted in the timeline
     * like any other bin clip.
     *
     * @param binId The id of the bin clip to take the zone from.
     * @param in The first frame of the zone.
     * @param out The last frame of the zone, included.
     * @return The id of the new subclip, empty on failure.
     */
    QString addSubClip(const QString &binId, int in, int out);

    /**
     * @brief Waits for a bin clip to be loaded.
     *
     * The clips are loaded in a task, this runs the event loop until the clip
     * has a producer, so that a script can use it right away.
     *
     * @param binId The id of the clip to wait for.
     * @return true if the clip is ready.
     */
    bool waitForClip(const QString &binId);

    /** @brief The tracks of the timeline. */
    QList<ScriptTrack> tracks() const;
    /**
     * @brief Adds a track to the timeline.
     * @param name The name of the track.
     * @param audio True for an audio track, false for a video track.
     * @return The id of the new track, -1 on failure.
     */
    int addTrack(const QString &name = {}, bool audio = false);
    /** @brief The clips of a track. */
    QList<ScriptClip> clips(int trackId) const;
    /** @brief The clip of a track at the given position, if any. */
    ScriptClip clipAt(int trackId, int position) const;
    /**
     * @brief Inserts a bin clip in the timeline.
     *
     * In the normal edit mode of Kdenlive, a clip cannot be dropped on top of
     * another one, the operation is refused. The overwrite mode replaces what is
     * there, the insert mode moves it to the right.
     *
     * @param binId The id of the bin clip to insert, a subclip id can be used.
     * @param trackId The id of the track to insert the clip on.
     * @param position The position of the clip on the track, in frames.
     * @param mode The edit mode to use, one of normal, overwrite or insert.
     * @return The id of the new timeline clip, -1 on failure.
     */
    int insertClip(const QString &binId, int trackId, int position, const QString &mode = QStringLiteral("overwrite"));
    /** @brief Removes the clip of a track at the given position. */
    bool removeClip(int trackId, int position);

    /**
     * @brief Moves a clip of the timeline.
     *
     * @param clipId The id of the clip to move.
     * @param trackId The id of the track to move the clip to.
     * @param position The new position on the track, in frames.
     * @return true if the clip was moved.
     */
    bool moveClip(int clipId, int trackId, int position);

    /**
     * @brief Groups clips together.
     *
     * The clips that are already in a group are taken out of it, so that the
     * group only contains the given clips.
     *
     * @param clipIds The ids of the clips to group.
     * @return The id of the group, -1 on failure.
     */
    int groupClips(const QList<int> &clipIds);

    /**
     * @brief Removes clips from their group.
     *
     * @param clipIds The ids of the clips to ungroup.
     * @return true if the operation succeeded.
     */
    bool ungroupClips(const QList<int> &clipIds);

    /**
     * @brief Mixes a clip with the one next to it.
     *
     * A mix is a transition between two clips, the second one is drawn over
     * the first one.
     *
     * @param clipId The id of the clip to mix.
     * @param mixId The type of mix, for instance luma.
     * @param position Where to mix, 0 for the end of the clip, -1 for its start.
     *        With 0, the mix is done at the start of the clip when it has no
     *        following clip, like the mix action of the interface does.
     * @return true if the mix was created.
     */
    bool mixClip(int clipId, const QString &mixId = QStringLiteral("luma"), int position = 0);

    /**
     * @brief The mix of a clip, if it has one.
     *
     * @param clipId The id of the clip.
     * @return A description of the mix, empty if the clip has no mix.
     */
    QVariantMap clipMix(int clipId) const;

    /**
     * @brief Removes the mix of a clip.
     *
     * The mix at the start of the clip is removed, or the mix at its end when it
     * has no mix at its start.
     *
     * @param clipId The id of the clip.
     * @return true if the mix was removed.
     */
    bool removeClipMix(int clipId);

    /**
     * @brief Makes a track the active one.
     *
     * Only the active tracks receive the timeline operations, this is what the
     * cursor of the interface does when it is moved.
     *
     * @param trackId The id of the track to activate.
     * @param active True to activate the track, false to deactivate it.
     */
    void setTrackActive(int trackId, bool active);
    /** @brief Tells whether a track is the active one. */
    bool isTrackActive(int trackId) const;

    /**
     * @brief Renames a track.
     *
     * @param trackId The id of the track to rename.
     * @param name The new name of the track.
     * @return true if the track was renamed.
     */
    bool setTrackName(int trackId, const QString &name);

    /** @brief Undoes the last operation on the project. */
    bool undo();
    /** @brief Redoes the last undone operation. */
    bool redo();

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
