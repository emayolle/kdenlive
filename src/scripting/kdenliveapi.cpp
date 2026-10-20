/*
    SPDX-FileCopyrightText: 2026 Erwan Mayolle <erwan.mayolle.pro@gmail.com>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "kdenliveapi.h"

#include "bin/clipcreator.hpp"
#include "bin/projectclip.h"
#include "bin/projectfolder.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "profiles/profilemodel.hpp"
#include "project/projectmanager.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/model/trackmodel.hpp"

#include <KLocalizedString>

#include <QFileInfo>
#include <QUrl>

KdenliveApi::KdenliveApi(Core *core, QObject *parent)
    : QObject(parent)
    , m_core(core)
{
}

void KdenliveApi::setError(const QString &message)
{
    m_lastError = message;
}

QString KdenliveApi::lastError() const
{
    return m_lastError;
}

bool KdenliveApi::hasProject() const
{
    return m_core && m_core->currentDoc() && m_core->projectManager()->getTimeline();
}

QString KdenliveApi::projectPath() const
{
    if (!hasProject()) {
        return {};
    }
    return m_core->currentDoc()->url().toLocalFile();
}

QString KdenliveApi::projectName() const
{
    if (!hasProject()) {
        return {};
    }
    return QFileInfo(projectPath()).completeBaseName();
}

QString KdenliveApi::profileName() const
{
    if (!hasProject()) {
        return {};
    }
    return m_core->getCurrentProfile()->path();
}

int KdenliveApi::projectDuration() const
{
    if (!hasProject()) {
        return 0;
    }
    return m_core->projectManager()->getTimeline()->duration();
}

double KdenliveApi::projectFps() const
{
    if (!hasProject()) {
        return 0.;
    }
    return m_core->getCurrentProfile()->fps();
}

QList<ScriptBinClip> KdenliveApi::binClips() const
{
    QList<ScriptBinClip> result;
    if (!hasProject()) {
        return result;
    }
    const auto model = m_core->projectItemModel();
    const auto clipIds = model->getAllClipIds();
    for (const auto &binId : clipIds) {
        auto clip = model->getClipByBinID(binId);
        if (!clip) {
            continue;
        }
        ScriptBinClip info;
        info.id = clip->clipId();
        info.name = clip->name();
        info.producer = clip->getToolTip();
        info.duration = clip->frameDuration();
        info.isSequence = clip->clipType() == ClipType::Timeline || clip->clipType() == ClipType::Playlist;
        switch (clip->clipType()) {
        case ClipType::AV:
            info.type = QStringLiteral("av");
            break;
        case ClipType::Video:
            info.type = QStringLiteral("video");
            break;
        case ClipType::Audio:
            info.type = QStringLiteral("audio");
            break;
        case ClipType::Image:
            info.type = QStringLiteral("image");
            break;
        case ClipType::Playlist:
            info.type = QStringLiteral("playlist");
            break;
        case ClipType::Timeline:
            info.type = QStringLiteral("timeline");
            break;
        case ClipType::Color:
            info.type = QStringLiteral("color");
            break;
        default:
            info.type = QStringLiteral("unknown");
            break;
        }
        result.append(info);
    }
    return result;
}

ScriptBinClip KdenliveApi::binClip(const QString &binId) const
{
    const auto clips = binClips();
    for (const auto &clip : clips) {
        if (clip.id == binId) {
            return clip;
        }
    }
    return {};
}

QString KdenliveApi::addClipToBin(const QString &url, const QString &name)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return {};
    }
    const QUrl fileUrl = QUrl::fromUserInput(url);
    if (!fileUrl.isLocalFile() || !QFileInfo::exists(fileUrl.toLocalFile())) {
        setError(i18n("File does not exist: %1", url));
        return {};
    }
    const auto model = m_core->projectItemModel();
    const QString parentFolder = model->getRootFolder()->clipId();
    std::function<bool(void)> undo = []() { return true; };
    std::function<bool(void)> redo = []() { return true; };
    // createClipFromFile is used instead of createClipsFromList, the latter needs
    // the bin widget which does not exist when running without the interface
    const QString id = ClipCreator::createClipFromFile(fileUrl.toLocalFile(), parentFolder, model, undo, redo);
    if (id.isEmpty() || id == QStringLiteral("-1")) {
        setError(i18n("Could not import %1", url));
        return {};
    }
    if (!name.isEmpty()) {
        auto clip = model->getClipByBinID(id);
        if (clip) {
            clip->setName(name);
        }
    }
    return id;
}

bool KdenliveApi::removeClipFromBin(const QString &binId)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return false;
    }
    const auto model = m_core->projectItemModel();
    auto clip = model->getClipByBinID(binId);
    if (!clip) {
        setError(i18n("No such bin clip: %1", binId));
        return false;
    }
    std::function<bool(void)> undo = []() { return true; };
    std::function<bool(void)> redo = []() { return true; };
    return model->requestBinClipDeletion(clip, undo, redo);
}

QString KdenliveApi::addSubClip(const QString &binId, int in, int out)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return {};
    }
    const auto model = m_core->projectItemModel();
    if (!model->getClipByBinID(binId)) {
        setError(i18n("No such bin clip: %1", binId));
        return {};
    }
    QString id;
    std::function<bool(void)> undo = []() { return true; };
    std::function<bool(void)> redo = []() { return true; };
    if (!model->requestAddBinSubClip(id, in, out, {}, QStringLiteral("-1"), undo, redo)) {
        setError(i18n("Could not create subclip of %1", binId));
        return {};
    }
    return id;
}

QList<ScriptTrack> KdenliveApi::tracks() const
{
    QList<ScriptTrack> result;
    if (!hasProject()) {
        return result;
    }
    const auto model = m_core->projectManager()->getTimeline();
    const auto trackIds = model->getAllTracksIds();
    for (const auto &trackId : trackIds) {
        ScriptTrack info;
        info.id = trackId;
        info.name = model->getTrackFullName(trackId);
        if (info.name.isEmpty()) {
            info.name = model->getTrackTagById(trackId);
        }
        info.isAudio = model->isAudioTrack(trackId);
        info.isVideo = !info.isAudio;
        info.clipCount = static_cast<int>(model->getTrackClipIds(trackId).size());
        info.duration = model->getTrackDuration(trackId);
        result.append(info);
    }
    return result;
}

int KdenliveApi::addTrack(const QString &name)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return -1;
    }
    const auto model = m_core->projectManager()->getTimeline();
    int id = -1;
    std::function<bool(void)> undo = []() { return true; };
    std::function<bool(void)> redo = []() { return true; };
    if (!model->requestTrackInsertion(0, id, name, false, undo, redo)) {
        setError(i18n("Could not add track"));
        return -1;
    }
    return id;
}

QList<ScriptClip> KdenliveApi::clips(int trackId) const
{
    QList<ScriptClip> result;
    if (!hasProject()) {
        return result;
    }
    const auto model = m_core->projectManager()->getTimeline();
    const auto clipIds = model->getTrackClipIds(trackId);
    for (const auto &clipId : clipIds) {
        ScriptClip info;
        info.id = clipId;
        info.trackId = trackId;
        info.position = model->getItemPosition(clipId);
        info.duration = model->getItemPlaytime(clipId);
        info.binId = model->getClipBinId(clipId);
        info.isAudio = model->isAudioTrack(trackId);
        info.isVideo = !info.isAudio;
        if (model->isClip(clipId)) {
            info.name = model->getClipName(clipId);
        }
        result.append(info);
    }
    return result;
}

ScriptClip KdenliveApi::clipAt(int trackId, int position) const
{
    if (!hasProject()) {
        return {};
    }
    const auto model = m_core->projectManager()->getTimeline();
    const int clipId = model->getClipByPosition(trackId, position);
    if (clipId < 0) {
        return {};
    }
    ScriptClip info;
    info.id = clipId;
    info.trackId = trackId;
    info.position = model->getItemPosition(clipId);
    info.duration = model->getItemPlaytime(clipId);
    info.binId = model->getClipBinId(clipId);
    info.isAudio = model->isAudioTrack(trackId);
    info.isVideo = !info.isAudio;
    if (model->isClip(clipId)) {
        info.name = model->getClipName(clipId);
    }
    return info;
}

int KdenliveApi::insertClip(const QString &binId, int trackId, int position)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return -1;
    }
    const auto model = m_core->projectManager()->getTimeline();
    if (!model->isTrack(trackId)) {
        setError(i18n("No such track: %1", trackId));
        return -1;
    }
    int id = -1;
    // useTargets is false so the clip lands on the requested track, not on the
    // current audio/video targets
    if (!model->requestClipInsertion(binId, trackId, position, id, true, false, false)) {
        setError(i18n("Could not insert clip %1", binId));
        return -1;
    }
    return id;
}

bool KdenliveApi::removeClip(int trackId, int position)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return false;
    }
    const auto model = m_core->projectManager()->getTimeline();
    const int clipId = model->getClipByPosition(trackId, position);
    if (clipId < 0) {
        setError(i18n("No clip at position %1 on track %2", position, trackId));
        return false;
    }
    std::function<bool(void)> undo = []() { return true; };
    std::function<bool(void)> redo = []() { return true; };
    return model->requestItemDeletion(clipId, undo, redo, true);
}

int KdenliveApi::trackDuration(int trackId) const
{
    if (!hasProject()) {
        return 0;
    }
    return m_core->projectManager()->getTimeline()->getTrackDuration(trackId);
}

int KdenliveApi::playheadPosition() const
{
    if (!m_core) {
        return 0;
    }
    return m_core->getMonitorPosition();
}

void KdenliveApi::setPlayheadPosition(int position)
{
    if (!m_core) {
        return;
    }
    m_core->seekMonitor(Kdenlive::ProjectMonitor, position);
}

bool KdenliveApi::save(const QString &path)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return false;
    }
    const auto projectManager = m_core->projectManager();
    const QString target = path.isEmpty() ? projectPath() : path;
    if (target.isEmpty()) {
        setError(i18n("The project was never saved, a path is required"));
        return false;
    }
    if (m_core->window()) {
        // In the GUI, reuse the regular save path, it handles backups and thumbnails
        return projectManager->saveFileAs(target, true);
    }
    // Headless: build the scene list ourselves, the GUI save path needs a MainWindow
    const QString saveFolder = QFileInfo(target).absolutePath();
    const QString scene = projectManager->projectSceneList(saveFolder).first;
    if (!scene.contains(QLatin1String("<track "))) {
        setError(i18n("The timeline is corrupted, refusing to save"));
        return false;
    }
    if (!m_core->currentDoc()->saveSceneList(target, scene, true)) {
        setError(i18n("Could not save project to %1", target));
        return false;
    }
    return true;
}
