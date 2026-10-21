/*
    SPDX-FileCopyrightText: 2026 Erwan Mayolle <erwan.mayolle.pro@gmail.com>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "kdenliveapi.h"

#include "bin/clipcreator.hpp"
#include "bin/projectclip.h"
#include "bin/projectfolder.h"
#include "bin/projectitemmodel.h"
#include "bin/projectsubclip.h"
#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "effects/effectsrepository.hpp"
#include "profiles/profilemodel.hpp"
#include "project/projectmanager.h"
#include "timeline2/model/timelinefunctions.hpp"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/model/trackmodel.hpp"

#include <KLocalizedString>

#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>

#include <algorithm>

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
        // The sub clips of the clip are children of the clip in the bin
        for (int i = 0; i < clip->childCount(); i++) {
            const auto subClip = std::dynamic_pointer_cast<ProjectSubClip>(clip->child(i));
            if (!subClip) {
                continue;
            }
            ScriptBinClip subInfo;
            subInfo.id = subClip->clipId();
            subInfo.name = subClip->name();
            subInfo.type = info.type;
            subInfo.producer = subClip->getToolTip();
            // The zone of a sub clip is inclusive
            subInfo.duration = subClip->zone().y() - subClip->zone().x() + 1;
            subInfo.isSubClip = true;
            result.append(subInfo);
        }
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
    m_core->pushUndo(undo, redo, i18nc("@action", "Add clip"));
    // The clip is loaded in a task, wait for it, a script expects to be able to
    // use the clip as soon as it is added
    if (!waitForClip(id)) {
        setError(i18n("Could not load %1", url));
        return {};
    }
    if (!name.isEmpty()) {
        auto clip = model->getClipByBinID(id);
        if (clip) {
            // The name is stored in the properties of the producer, setName()
            // would not be saved. ProjectClip::rename() cannot be used, it
            // needs the bin widget that does not exist without the interface.
            clip->setProperties({{QStringLiteral("kdenlive:clipname"), name}}, false);
        }
    }
    return id;
}

bool KdenliveApi::waitForClip(const QString &binId)
{
    const auto model = m_core->projectItemModel();
    auto clip = model->getClipByBinID(binId);
    if (!clip) {
        return false;
    }
    // A clip that is already loaded has a valid producer
    if (clip->isValid()) {
        return true;
    }
    // Otherwise the clip is loaded in a task, run the event loop until it is ready
    QEventLoop loop;
    bool ready = false;
    const auto connection = connect(clip.get(), &ProjectClip::producerChanged, this, [&loop, &ready]() {
        ready = true;
        loop.quit();
    });
    // The clip may be ready already, in that case the signal was emitted before
    // we could connect to it
    if (clip->isValid()) {
        ready = true;
    } else {
        // A timeout avoids hanging forever if the file cannot be read
        QTimer::singleShot(30000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    disconnect(connection);
    return ready && clip->isValid();
}

bool KdenliveApi::removeClipFromBin(const QString &binId)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return false;
    }
    const auto model = m_core->projectItemModel();
    // A sub clip can be removed as well as a clip
    const auto item = model->getItemByBinId(binId);
    if (!item) {
        setError(i18n("No such bin clip: %1", binId));
        return false;
    }
    std::function<bool(void)> undo = []() { return true; };
    std::function<bool(void)> redo = []() { return true; };
    if (!model->requestBinClipDeletion(item, undo, redo)) {
        setError(i18n("Could not remove clip %1", binId));
        return false;
    }
    m_core->pushUndo(undo, redo, i18n("Delete bin Clips"));
    return true;
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
    // The zone must be a valid range, a reversed or negative zone would create
    // a clip with a negative duration
    if (in < 0) {
        setError(i18n("The start of the zone cannot be negative: %1", in));
        return {};
    }
    if (out < in) {
        setError(i18n("The end of the zone must be after its start: %1", out));
        return {};
    }
    QString id;
    // The parent is the clip the fragment is taken from
    if (!model->requestAddBinSubClip(id, in, out, {}, binId)) {
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
        // The name of the track is the one the user gave it, the tag is the
        // automatic name of the track, for instance V1
        info.name = model->getTrackProperty(trackId, QStringLiteral("kdenlive:track_name")).toString();
        info.tag = model->getTrackTagById(trackId);
        info.isAudio = model->isAudioTrack(trackId);
        info.isVideo = !info.isAudio;
        info.clipCount = static_cast<int>(model->getTrackClipIds(trackId).size());
        info.duration = model->getTrackDuration(trackId);
        result.append(info);
    }
    return result;
}

int KdenliveApi::addTrack(const QString &name, bool audio)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return -1;
    }
    const auto model = m_core->projectManager()->getTimeline();
    int id = -1;
    if (!model->requestTrackInsertion(0, id, name, audio)) {
        setError(i18n("Could not add track"));
        return -1;
    }
    // The clip of the bin that holds the sequence must point to the new version
    // of it, otherwise the project is saved with an outdated sequence
    m_core->projectItemModel()->updateSequenceClip(model->uuid(), model->durations(), -1, true);
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
    // The clips are sorted by position, this is the order they appear on the track
    std::sort(result.begin(), result.end(), [](const ScriptClip &a, const ScriptClip &b) { return a.position < b.position; });
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

int KdenliveApi::insertClip(const QString &binId, int trackId, int position, const QString &mode)
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
    const auto item = m_core->projectItemModel()->getItemByBinId(binId);
    if (!item) {
        setError(i18n("No such bin clip: %1", binId));
        return -1;
    }
    // A sub clip is inserted with the zone it defines, using the clip it comes from
    QString producerId = binId;
    QPoint zone;
    if (item->itemType() == AbstractProjectItem::SubClipItem) {
        const auto subClip = std::static_pointer_cast<ProjectSubClip>(item);
        const auto masterClip = subClip->getMasterClip();
        if (!masterClip) {
            setError(i18n("No such bin clip: %1", binId));
            return -1;
        }
        producerId = masterClip->clipId();
        // The zone of a sub clip is inclusive, the zone of an insertion is not
        zone = QPoint(subClip->zone().x(), subClip->zone().y() + 1);
    } else {
        const auto clip = std::static_pointer_cast<ProjectClip>(item);
        // The zone of the clip is inserted, the whole clip when it has no zone
        zone = clip->zone();
        if (zone.y() <= zone.x()) {
            zone = QPoint(0, clip->frameDuration());
        }
    }
    // In overwrite mode the content at the position is replaced, in insert mode
    // it is pushed to the right. In the normal mode of Kdenlive, a clip cannot
    // be dropped on top of another one, the operation is refused.
    if (mode != QLatin1String("overwrite") && mode != QLatin1String("insert") && mode != QLatin1String("normal")) {
        setError(i18n("Unknown edit mode: %1", mode));
        return -1;
    }
    bool overwrite = mode != QLatin1String("insert");
    if (mode == QLatin1String("normal")) {
        const int zoneEnd = position + (zone.y() - zone.x());
        for (const auto &existing : clips(trackId)) {
            if (existing.position < zoneEnd && position < existing.position + existing.duration) {
                setError(i18n("Cannot insert clip, the position %1 is occupied", position));
                return -1;
            }
        }
    }
    // useTargets is false so the clip lands on the requested track, not on the
    // current audio/video targets
    if (!TimelineFunctions::insertZone(model, {trackId}, producerId, position, zone, overwrite, false)) {
        setError(i18n("Could not insert clip %1", binId));
        return -1;
    }
    // Find the clip that was inserted
    const auto timelineClips = clips(trackId);
    for (const auto &inserted : timelineClips) {
        if (inserted.binId == producerId && inserted.position == position) {
            return inserted.id;
        }
    }
    return -1;
}

bool KdenliveApi::moveClip(int clipId, int trackId, int position)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return false;
    }
    const auto model = m_core->projectManager()->getTimeline();
    if (!model->isClip(clipId)) {
        setError(i18n("No such clip: %1", clipId));
        return false;
    }
    if (!model->isTrack(trackId)) {
        setError(i18n("No such track: %1", trackId));
        return false;
    }
    // The selection of the user would change the way the clip is moved, the
    // operation of a script must always be the same
    model->requestClearSelection();
    if (!model->requestClipMove(clipId, trackId, position, true, true, true)) {
        setError(i18n("Could not move clip %1", clipId));
        return false;
    }
    return true;
}

int KdenliveApi::groupClips(const QList<int> &clipIds)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return -1;
    }
    if (clipIds.size() < 2) {
        setError(i18n("Select at least 2 clips to group"));
        return -1;
    }
    const auto model = m_core->projectManager()->getTimeline();
    std::unordered_set<int> ids;
    for (const auto &clipId : clipIds) {
        if (!model->isClip(clipId)) {
            setError(i18n("No such clip: %1", clipId));
            return -1;
        }
        ids.insert(clipId);
    }
    const int groupId = model->requestClipsGroup(ids, true);
    if (groupId < 0) {
        setError(i18n("Could not group the clips"));
        return -1;
    }
    return groupId;
}

bool KdenliveApi::ungroupClips(const QList<int> &clipIds)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return false;
    }
    const auto model = m_core->projectManager()->getTimeline();
    std::unordered_set<int> ids;
    for (const auto &clipId : clipIds) {
        if (!model->isClip(clipId)) {
            setError(i18n("No such clip: %1", clipId));
            return false;
        }
        ids.insert(clipId);
    }
    if (ids.empty()) {
        return true;
    }
    if (!model->requestClipsUngroup(ids)) {
        setError(i18n("Could not ungroup the clips"));
        return false;
    }
    return true;
}

bool KdenliveApi::mixClip(int clipId, const QString &mixId, int position)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return false;
    }
    const auto model = m_core->projectManager()->getTimeline();
    if (!model->isClip(clipId)) {
        setError(i18n("No such clip: %1", clipId));
        return false;
    }
    // Only the active tracks receive the timeline operations
    if (!isTrackActive(model->getClipTrackId(clipId))) {
        setError(i18n("The track of clip %1 is not active", clipId));
        return false;
    }
    if (!model->mixClip(clipId, mixId, position)) {
        setError(i18n("Could not mix clip %1", clipId));
        return false;
    }
    return true;
}

QVariantMap KdenliveApi::clipMix(int clipId) const
{
    QVariantMap result;
    if (!hasProject()) {
        return result;
    }
    const auto model = m_core->projectManager()->getTimeline();
    return model->clipMixInfo(clipId);
}

bool KdenliveApi::removeClipMix(int clipId)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return false;
    }
    const auto model = m_core->projectManager()->getTimeline();
    if (!model->isClip(clipId)) {
        setError(i18n("No such clip: %1", clipId));
        return false;
    }
    const auto mix = model->clipMixInfo(clipId);
    if (mix.value(QStringLiteral("has_start_mix")).toBool()) {
        // The mix is at the start of the clip
        return model->removeMix(clipId);
    }
    if (mix.value(QStringLiteral("has_end_mix")).toBool()) {
        // The mix is at the end of the clip, the transition is on the next clip
        const int otherClipId = mix.value(QStringLiteral("other_clip_id")).toInt();
        if (otherClipId > -1) {
            return model->removeMix(otherClipId);
        }
    }
    setError(i18n("Clip %1 has no mix", clipId));
    return false;
}

void KdenliveApi::setTrackActive(int trackId, bool active)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return;
    }
    const auto model = m_core->projectManager()->getTimeline();
    if (!model->isTrack(trackId)) {
        setError(i18n("No such track: %1", trackId));
        return;
    }
    model->setTrackProperty(trackId, QStringLiteral("kdenlive:timeline_active"), active ? QStringLiteral("1") : QStringLiteral("0"));
}

bool KdenliveApi::isTrackActive(int trackId) const
{
    if (!hasProject()) {
        return false;
    }
    const auto model = m_core->projectManager()->getTimeline();
    if (!model->isTrack(trackId)) {
        return false;
    }
    return model->getTrackProperty(trackId, QStringLiteral("kdenlive:timeline_active")).toString() == QStringLiteral("1");
}

bool KdenliveApi::setTrackName(int trackId, const QString &name)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return false;
    }
    const auto model = m_core->projectManager()->getTimeline();
    if (!model->isTrack(trackId)) {
        setError(i18n("No such track: %1", trackId));
        return false;
    }
    model->setTrackName(trackId, name);
    return true;
}

bool KdenliveApi::undo()
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return false;
    }
    const auto stack = m_core->undoStack();
    if (!stack || !stack->canUndo()) {
        setError(i18n("Nothing to undo"));
        return false;
    }
    stack->undo();
    return true;
}

bool KdenliveApi::redo()
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return false;
    }
    const auto stack = m_core->undoStack();
    if (!stack || !stack->canRedo()) {
        setError(i18n("Nothing to redo"));
        return false;
    }
    stack->redo();
    return true;
}

bool KdenliveApi::removeClipAt(int trackId, int position)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return false;
    }
    const auto model = m_core->projectManager()->getTimeline();
    if (!model->isTrack(trackId)) {
        setError(i18n("No such track: %1", trackId));
        return false;
    }
    const int clipId = model->getClipByPosition(trackId, position);
    if (clipId < 0) {
        setError(i18n("No clip at position %1 on track %2", position, trackId));
        return false;
    }
    return removeClip(clipId);
}

bool KdenliveApi::removeClip(int clipId)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return false;
    }
    const auto model = m_core->projectManager()->getTimeline();
    if (!model->isClip(clipId)) {
        setError(i18n("No such clip: %1", clipId));
        return false;
    }
    std::function<bool(void)> undo = []() { return true; };
    std::function<bool(void)> redo = []() { return true; };
    if (!model->requestItemDeletion(clipId, undo, redo, true)) {
        setError(i18n("Could not remove clip %1", clipId));
        return false;
    }
    m_core->pushUndo(undo, redo, i18n("Delete clip"));
    return true;
}

int KdenliveApi::splitClip(int clipId, int position)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return -1;
    }
    const auto model = m_core->projectManager()->getTimeline();
    if (!model->isClip(clipId)) {
        setError(i18n("No such clip: %1", clipId));
        return -1;
    }
    const int start = model->getItemPosition(clipId);
    const int duration = model->getItemPlaytime(clipId);
    if (position <= start || position >= start + duration) {
        setError(i18n("The cut must be inside the clip, between %1 and %2", start + 1, start + duration - 1));
        return -1;
    }
    std::function<bool(void)> undo = []() { return true; };
    std::function<bool(void)> redo = []() { return true; };
    if (!TimelineFunctions::requestClipCut(model, clipId, position, undo, redo)) {
        setError(i18n("Could not cut clip %1", clipId));
        return -1;
    }
    m_core->pushUndo(undo, redo, i18n("Cut clip"));
    // The part that is after the cut is the new clip
    const int trackId = model->getItemTrackId(clipId);
    for (const auto &clip : clips(trackId)) {
        if (clip.position == position) {
            return clip.id;
        }
    }
    setError(i18n("Could not cut clip %1", clipId));
    return -1;
}

bool KdenliveApi::removeTrack(int trackId)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return false;
    }
    const auto model = m_core->projectManager()->getTimeline();
    if (!model->isTrack(trackId)) {
        setError(i18n("No such track: %1", trackId));
        return false;
    }
    // The undo is handled by the model
    if (!model->requestTrackDeletion(trackId)) {
        setError(i18n("Could not remove track %1", trackId));
        return false;
    }
    // The clip of the bin that holds the sequence must point to the new version
    // of it, otherwise the project is saved with an outdated sequence
    m_core->projectItemModel()->updateSequenceClip(model->uuid(), model->durations(), -1, true);
    return true;
}

bool KdenliveApi::addEffect(int clipId, const QString &effectId)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return false;
    }
    const auto model = m_core->projectManager()->getTimeline();
    if (!model->isClip(clipId)) {
        setError(i18n("No such clip: %1", clipId));
        return false;
    }
    if (effectId.isEmpty()) {
        setError(i18n("No effect given"));
        return false;
    }
    // The effect would be added to the selected clips instead of the one that
    // is asked for, the operation of a script must always be the same
    model->requestClearSelection();
    // The effect must exist, adding an unknown one makes the effect stack crash
    if (!EffectsRepository::get()->exists(effectId)) {
        setError(i18n("No such effect: %1", effectId));
        return false;
    }
    if (model->addClipEffect(clipId, effectId, false).isEmpty()) {
        setError(i18n("Could not add effect %1 to clip %2", effectId, clipId));
        return false;
    }
    return true;
}

bool KdenliveApi::removeEffect(int clipId, const QString &effectId)
{
    setError({});
    if (!hasProject()) {
        setError(i18n("No project loaded"));
        return false;
    }
    const auto model = m_core->projectManager()->getTimeline();
    if (!model->isClip(clipId)) {
        setError(i18n("No such clip: %1", clipId));
        return false;
    }
    const auto effects = clipEffects(clipId);
    bool found = false;
    for (const auto &effect : effects) {
        if (effect.toMap().value(QStringLiteral("id")).toString() == effectId) {
            found = true;
            break;
        }
    }
    if (!found) {
        setError(i18n("Clip %1 has no effect %2", clipId, effectId));
        return false;
    }
    model->removeEffectFromGroup(clipId, effectId, -1);
    return true;
}

QVariantList KdenliveApi::clipEffects(int clipId) const
{
    if (!hasProject()) {
        return {};
    }
    const auto model = m_core->projectManager()->getTimeline();
    if (!model->isClip(clipId)) {
        return {};
    }
    return model->getClipEffects(clipId);
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
    if (!hasProject()) {
        return 0;
    }
    // The cursor is held by the timeline model, it is also available when
    // there is no graphical interface
    return m_core->projectManager()->getTimeline()->cursorPosition();
}

void KdenliveApi::setPlayheadPosition(int position)
{
    setError({});
    if (!hasProject()) {
        return;
    }
    if (position < 0) {
        setError(i18n("The playhead cannot be before the start: %1", position));
        return;
    }
    m_core->projectManager()->getTimeline()->setCursorPosition(position);
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
    // The folder must exist, the save would fail otherwise
    const QFileInfo targetInfo(target);
    if (!QDir(targetInfo.absolutePath()).exists()) {
        setError(i18n("The folder does not exist: %1", targetInfo.absolutePath()));
        return false;
    }
    if (m_core->window()) {
        // In the GUI, reuse the regular save path, it handles backups and thumbnails
        return projectManager->saveFileAs(target, true);
    }
    // Headless: the save path of the interface needs a MainWindow
    return projectManager->saveFileHeadless(target);
}

void KdenliveApi::showMessage(const QString &text)
{
    if (!m_core) {
        return;
    }
    m_core->displayMessage(text, InformationMessage);
}
