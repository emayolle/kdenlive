/*
    SPDX-FileCopyrightText: 2026 Erwan Mayolle <erwan.mayolle.pro@gmail.com>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "pythonengine.h"
#include "kdenliveapi.h"

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <KLocalizedString>
#include <QDir>
#include <QVariantMap>

#include <QFileInfo>
#include <QStandardPaths>

namespace {

// ---------------------------------------------------------------------------
// Conversions between the Qt and the Python worlds
// ---------------------------------------------------------------------------

QString toQString(PyObject *object)
{
    if (!object || object == Py_None) {
        return {};
    }
    if (PyUnicode_Check(object)) {
        Py_ssize_t size = 0;
        const char *utf8 = PyUnicode_AsUTF8AndSize(object, &size);
        return utf8 ? QString::fromUtf8(utf8, size) : QString();
    }
    return {};
}

PyObject *fromQString(const QString &value)
{
    return PyUnicode_FromString(value.toUtf8().constData());
}

/** @brief Compiles and runs a script.
 *
 *  The code is compiled with a name, so that the traceback of a failed script
 *  shows the file and the line where it failed.
 */
PyObject *runScript(const QByteArray &code, const QString &name, PyObject *globals)
{
    PyObject *compiled = Py_CompileString(code.constData(), name.toUtf8().constData(), Py_file_input);
    if (!compiled) {
        return nullptr;
    }
    PyObject *result = PyEval_EvalCode(compiled, globals, globals);
    Py_DECREF(compiled);
    return result;
}

/** @brief Formats the traceback of a failed script.
 *
 *  The traceback tells where the script failed, it is the most useful
 *  information for the user.
 */
QString formatTraceback(PyObject *traceback)
{
    QString result;
    if (!traceback) {
        return result;
    }
    PyObject *module = PyImport_ImportModule("traceback");
    if (!module) {
        PyErr_Clear();
        return result;
    }
    PyObject *lines = PyObject_CallMethod(module, "format_tb", "O", traceback);
    if (lines) {
        if (PyList_Check(lines)) {
            const Py_ssize_t count = PyList_Size(lines);
            for (Py_ssize_t i = 0; i < count; i++) {
                PyObject *line = PyList_GetItem(lines, i);
                if (!line) {
                    continue;
                }
                const char *utf8 = PyUnicode_AsUTF8(line);
                if (utf8) {
                    result += QString::fromUtf8(utf8);
                }
            }
        }
        Py_DECREF(lines);
    } else {
        PyErr_Clear();
    }
    Py_DECREF(module);
    return result;
}

/** @brief Converts a map of variants to a python dictionary. */
PyObject *fromVariantMap(const QVariantMap &values)
{
    PyObject *dict = PyDict_New();
    if (!dict) {
        return nullptr;
    }
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        PyObject *item = nullptr;
        const int type = it.value().typeId();
        if (type == QMetaType::Bool) {
            item = PyBool_FromLong(it.value().toBool() ? 1 : 0);
        } else if (type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::LongLong || type == QMetaType::Long) {
            item = PyLong_FromLongLong(it.value().toLongLong());
        } else if (type == QMetaType::Double || type == QMetaType::Float) {
            item = PyFloat_FromDouble(it.value().toDouble());
        } else if (type == QMetaType::QString) {
            item = fromQString(it.value().toString());
        } else {
            item = Py_None;
            Py_INCREF(item);
        }
        if (item) {
            PyDict_SetItemString(dict, it.key().toUtf8().constData(), item);
            Py_DECREF(item);
        }
    }
    return dict;
}

/**
 * @brief Creates the namespace a script runs in.
 *
 * The builtins must be set to the module, not to its dictionary, otherwise
 * python fails with a type error.
 */
PyObject *createScriptNamespace()
{
    PyObject *globals = PyDict_New();
    if (!globals) {
        return nullptr;
    }
    PyObject *builtins = PyImport_ImportModule("builtins");
    if (builtins) {
        PyDict_SetItemString(globals, "__builtins__", builtins);
        Py_DECREF(builtins);
    }
    return globals;
}

/** @brief Builds a python dictionary from a list of key and value pairs. */
PyObject *fromPairs(const QList<QPair<QString, PyObject *>> &pairs)
{
    PyObject *dict = PyDict_New();
    if (!dict) {
        return nullptr;
    }
    for (const auto &pair : pairs) {
        if (!pair.second) {
            continue;
        }
        PyDict_SetItemString(dict, pair.first.toUtf8().constData(), pair.second);
    }
    return dict;
}

/** @brief The api the module works on. */
KdenliveApi *s_api = nullptr;

/** @brief The actions registered by the scripts, in registration order. */
struct RegisteredAction
{
    QString id;
    QString label;
    PyObject *callback{nullptr};
};
QList<RegisteredAction> s_actions;

// ---------------------------------------------------------------------------
// The Project type
// ---------------------------------------------------------------------------

struct PyProject
{
    PyObject_HEAD KdenliveApi *api;
};

void projectDealloc(PyObject *self)
{
    Py_TYPE(self)->tp_free(self);
}

/** @brief Returns the api of a project, fails if there is none. */
KdenliveApi *projectApi(PyObject *self)
{
    auto *project = reinterpret_cast<PyProject *>(self);
    if (!project->api) {
        PyErr_SetString(PyExc_RuntimeError, "No project is loaded");
        return nullptr;
    }
    return project->api;
}

PyObject *projectNew(PyTypeObject *type, PyObject *, PyObject *)
{
    auto *self = reinterpret_cast<PyProject *>(type->tp_alloc(type, 0));
    if (self) {
        self->api = nullptr;
    }
    return reinterpret_cast<PyObject *>(self);
}

PyObject *projectGetPath(PyObject *self, void *)
{
    return fromQString(reinterpret_cast<PyProject *>(self)->api->projectPath());
}
PyObject *projectGetName(PyObject *self, void *)
{
    return fromQString(reinterpret_cast<PyProject *>(self)->api->projectName());
}
PyObject *projectGetProfile(PyObject *self, void *)
{
    return fromQString(reinterpret_cast<PyProject *>(self)->api->profileName());
}
PyObject *projectGetDuration(PyObject *self, void *)
{
    return PyLong_FromLong(reinterpret_cast<PyProject *>(self)->api->projectDuration());
}
PyObject *projectGetFps(PyObject *self, void *)
{
    return PyFloat_FromDouble(reinterpret_cast<PyProject *>(self)->api->projectFps());
}
PyObject *projectGetError(PyObject *self, void *)
{
    return fromQString(reinterpret_cast<PyProject *>(self)->api->lastError());
}
PyObject *projectGetPlayhead(PyObject *self, void *)
{
    return PyLong_FromLong(reinterpret_cast<PyProject *>(self)->api->playheadPosition());
}
int projectSetPlayhead(PyObject *self, PyObject *value, void *)
{
    const long position = PyLong_AsLong(value);
    if (position == -1 && PyErr_Occurred()) {
        return -1;
    }
    reinterpret_cast<PyProject *>(self)->api->setPlayheadPosition(static_cast<int>(position));
    return 0;
}

PyGetSetDef projectGetSet[] = {
    {const_cast<char *>("path"), projectGetPath, nullptr, const_cast<char *>("The path of the project file"), nullptr},
    {const_cast<char *>("name"), projectGetName, nullptr, const_cast<char *>("The name of the project"), nullptr},
    {const_cast<char *>("profile"), projectGetProfile, nullptr, const_cast<char *>("The video profile of the project"), nullptr},
    {const_cast<char *>("duration"), projectGetDuration, nullptr, const_cast<char *>("The duration in frames"), nullptr},
    {const_cast<char *>("fps"), projectGetFps, nullptr, const_cast<char *>("The number of frames per second"), nullptr},
    {const_cast<char *>("error"), projectGetError, nullptr, const_cast<char *>("The error of the last operation"), nullptr},
    {const_cast<char *>("playhead"), projectGetPlayhead, projectSetPlayhead, const_cast<char *>("The playhead position in frames"), nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr},
};

PyObject *projectBinClips(PyObject *self, PyObject *)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    const auto clips = api->binClips();
    PyObject *list = PyList_New(clips.size());
    if (!list) {
        return nullptr;
    }
    for (int i = 0; i < clips.size(); i++) {
        const auto &clip = clips.at(i);
        PyObject *item = fromPairs({
            {QStringLiteral("id"), fromQString(clip.id)},
            {QStringLiteral("name"), fromQString(clip.name)},
            {QStringLiteral("type"), fromQString(clip.type)},
            {QStringLiteral("producer"), fromQString(clip.producer)},
            {QStringLiteral("duration"), PyLong_FromLong(clip.duration)},
            {QStringLiteral("is_sequence"), PyBool_FromLong(clip.isSequence)},
            {QStringLiteral("is_subclip"), PyBool_FromLong(clip.isSubClip)},
        });
        if (!item) {
            Py_DECREF(list);
            return nullptr;
        }
        PyList_SET_ITEM(list, i, item);
    }
    return list;
}

PyObject *projectAddClip(PyObject *self, PyObject *args)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    const char *url = nullptr;
    const char *name = nullptr;
    if (!PyArg_ParseTuple(args, "s|s", &url, &name)) {
        return nullptr;
    }
    const QString id = api->addClipToBin(QString::fromUtf8(url), name ? QString::fromUtf8(name) : QString());
    PyErr_Clear();
    return fromQString(id);
}

PyObject *projectRemoveClip(PyObject *self, PyObject *args)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    const char *binId = nullptr;
    if (!PyArg_ParseTuple(args, "s", &binId)) {
        return nullptr;
    }
    const bool result = api->removeClipFromBin(QString::fromUtf8(binId));
    PyErr_Clear();
    return PyBool_FromLong(result);
}

PyObject *projectAddSubClip(PyObject *self, PyObject *args)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    const char *binId = nullptr;
    int in = 0;
    int out = 0;
    if (!PyArg_ParseTuple(args, "sii", &binId, &in, &out)) {
        return nullptr;
    }
    const QString id = api->addSubClip(QString::fromUtf8(binId), in, out);
    PyErr_Clear();
    return fromQString(id);
}

/** @brief Converts a list of integers to a list for the api. */
QList<int> toIntList(PyObject *object)
{
    QList<int> result;
    if (!object) {
        return result;
    }
    // Accept any sequence of ids, a list or a tuple
    PyObject *sequence = PySequence_Fast(object, "A list of ids is expected");
    if (!sequence) {
        PyErr_Clear();
        return result;
    }
    const Py_ssize_t count = PySequence_Fast_GET_SIZE(sequence);
    for (Py_ssize_t i = 0; i < count; i++) {
        PyObject *item = PySequence_Fast_GET_ITEM(sequence, i);
        if (item && PyLong_Check(item)) {
            result.append(static_cast<int>(PyLong_AsLong(item)));
        }
    }
    Py_DECREF(sequence);
    return result;
}

PyObject *projectMoveClip(PyObject *self, PyObject *args)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    int clipId = -1;
    int trackId = -1;
    int position = 0;
    if (!PyArg_ParseTuple(args, "iii", &clipId, &trackId, &position)) {
        return nullptr;
    }
    const bool result = api->moveClip(clipId, trackId, position);
    PyErr_Clear();
    return PyBool_FromLong(result);
}

PyObject *projectGroupClips(PyObject *self, PyObject *args)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    PyObject *list = nullptr;
    if (!PyArg_ParseTuple(args, "O", &list)) {
        return nullptr;
    }
    const int groupId = api->groupClips(toIntList(list));
    PyErr_Clear();
    return PyLong_FromLong(groupId);
}

PyObject *projectUngroupClips(PyObject *self, PyObject *args)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    PyObject *list = nullptr;
    if (!PyArg_ParseTuple(args, "O", &list)) {
        return nullptr;
    }
    const bool result = api->ungroupClips(toIntList(list));
    PyErr_Clear();
    return PyBool_FromLong(result);
}

PyObject *projectMixClip(PyObject *self, PyObject *args)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    int clipId = -1;
    const char *mixId = nullptr;
    int position = 0;
    if (!PyArg_ParseTuple(args, "i|si", &clipId, &mixId, &position)) {
        return nullptr;
    }
    const bool result = api->mixClip(clipId, mixId ? QString::fromUtf8(mixId) : QString(), position);
    PyErr_Clear();
    return PyBool_FromLong(result);
}

PyObject *projectClipMix(PyObject *self, PyObject *args)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    int clipId = -1;
    if (!PyArg_ParseTuple(args, "i", &clipId)) {
        return nullptr;
    }
    const QVariantMap mix = api->clipMix(clipId);
    PyErr_Clear();
    return fromVariantMap(mix);
}

PyObject *projectSetTrackActive(PyObject *self, PyObject *args)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    int trackId = -1;
    int active = 1;
    if (!PyArg_ParseTuple(args, "i|p", &trackId, &active)) {
        return nullptr;
    }
    api->setTrackActive(trackId, active != 0);
    PyErr_Clear();
    return PyBool_FromLong(active != 0);
}

PyObject *projectIsTrackActive(PyObject *self, PyObject *args)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    int trackId = -1;
    if (!PyArg_ParseTuple(args, "i", &trackId)) {
        return nullptr;
    }
    const bool active = api->isTrackActive(trackId);
    PyErr_Clear();
    return PyBool_FromLong(active);
}

PyObject *projectSetTrackName(PyObject *self, PyObject *args)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    int trackId = -1;
    const char *name = nullptr;
    if (!PyArg_ParseTuple(args, "is", &trackId, &name)) {
        return nullptr;
    }
    const bool result = api->setTrackName(trackId, QString::fromUtf8(name));
    PyErr_Clear();
    return PyBool_FromLong(result);
}

PyObject *projectRemoveClipMix(PyObject *self, PyObject *args)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    int clipId = -1;
    if (!PyArg_ParseTuple(args, "i", &clipId)) {
        return nullptr;
    }
    const bool result = api->removeClipMix(clipId);
    PyErr_Clear();
    return PyBool_FromLong(result);
}

PyObject *projectUndo(PyObject *self, PyObject *)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    const bool result = api->undo();
    PyErr_Clear();
    return PyBool_FromLong(result);
}

PyObject *projectRedo(PyObject *self, PyObject *)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    const bool result = api->redo();
    PyErr_Clear();
    return PyBool_FromLong(result);
}

PyObject *projectTracks(PyObject *self, PyObject *)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    const auto tracks = api->tracks();
    PyObject *list = PyList_New(tracks.size());
    if (!list) {
        return nullptr;
    }
    for (int i = 0; i < tracks.size(); i++) {
        const auto &track = tracks.at(i);
        PyObject *item = fromPairs({
            {QStringLiteral("id"), PyLong_FromLong(track.id)},
            {QStringLiteral("name"), fromQString(track.name)},
            {QStringLiteral("tag"), fromQString(track.tag)},
            {QStringLiteral("is_audio"), PyBool_FromLong(track.isAudio)},
            {QStringLiteral("is_video"), PyBool_FromLong(track.isVideo)},
            {QStringLiteral("clip_count"), PyLong_FromLong(track.clipCount)},
            {QStringLiteral("duration"), PyLong_FromLong(track.duration)},
        });
        if (!item) {
            Py_DECREF(list);
            return nullptr;
        }
        PyList_SET_ITEM(list, i, item);
    }
    return list;
}

PyObject *projectAddTrack(PyObject *self, PyObject *args)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    const char *name = nullptr;
    int audio = 0;
    if (!PyArg_ParseTuple(args, "|sp", &name, &audio)) {
        return nullptr;
    }
    const int id = api->addTrack(name ? QString::fromUtf8(name) : QString(), audio != 0);
    PyErr_Clear();
    return PyLong_FromLong(id);
}

PyObject *projectTimelineClips(PyObject *self, PyObject *args)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    int trackId = -1;
    if (!PyArg_ParseTuple(args, "i", &trackId)) {
        return nullptr;
    }
    const auto clips = api->clips(trackId);
    PyObject *list = PyList_New(clips.size());
    if (!list) {
        return nullptr;
    }
    for (int i = 0; i < clips.size(); i++) {
        const auto &clip = clips.at(i);
        PyObject *item = fromPairs({
            {QStringLiteral("id"), PyLong_FromLong(clip.id)},
            {QStringLiteral("track_id"), PyLong_FromLong(clip.trackId)},
            {QStringLiteral("position"), PyLong_FromLong(clip.position)},
            {QStringLiteral("duration"), PyLong_FromLong(clip.duration)},
            {QStringLiteral("bin_id"), fromQString(clip.binId)},
            {QStringLiteral("name"), fromQString(clip.name)},
        });
        if (!item) {
            Py_DECREF(list);
            return nullptr;
        }
        PyList_SET_ITEM(list, i, item);
    }
    return list;
}

PyObject *projectInsertClip(PyObject *self, PyObject *args)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    const char *binId = nullptr;
    int trackId = -1;
    int position = 0;
    const char *mode = nullptr;
    if (!PyArg_ParseTuple(args, "sii|s", &binId, &trackId, &position, &mode)) {
        return nullptr;
    }
    const int id = api->insertClip(QString::fromUtf8(binId), trackId, position, mode ? QString::fromUtf8(mode) : QString());
    PyErr_Clear();
    return PyLong_FromLong(id);
}

PyObject *projectRemoveClipAt(PyObject *self, PyObject *args)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    int trackId = -1;
    int position = 0;
    if (!PyArg_ParseTuple(args, "ii", &trackId, &position)) {
        return nullptr;
    }
    const bool result = api->removeClip(trackId, position);
    PyErr_Clear();
    return PyBool_FromLong(result);
}

PyObject *projectTrackDuration(PyObject *self, PyObject *args)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    int trackId = -1;
    if (!PyArg_ParseTuple(args, "i", &trackId)) {
        return nullptr;
    }
    return PyLong_FromLong(api->trackDuration(trackId));
}

PyObject *projectSave(PyObject *self, PyObject *args)
{
    auto *api = projectApi(self);
    if (!api) {
        return nullptr;
    }
    const char *path = nullptr;
    if (!PyArg_ParseTuple(args, "|s", &path)) {
        return nullptr;
    }
    const bool result = api->save(path ? QString::fromUtf8(path) : QString());
    PyErr_Clear();
    return PyBool_FromLong(result);
}

PyMethodDef projectMethods[] = {
    {"bin_clips", projectBinClips, METH_NOARGS, "The clips of the project bin."},
    {"add_clip", projectAddClip, METH_VARARGS, "Import a file in the bin, returns its bin id."},
    {"remove_clip", projectRemoveClip, METH_VARARGS, "Remove a clip from the bin."},
    {"add_subclip", projectAddSubClip, METH_VARARGS, "Create a subclip of a bin clip, returns its bin id."},
    {"tracks", projectTracks, METH_NOARGS, "The tracks of the timeline."},
    {"add_track", projectAddTrack, METH_VARARGS, "Add a track to the timeline, returns its id. The second argument tells if it is an audio track."},
    {"set_track_active", projectSetTrackActive, METH_VARARGS, "Make a track the active one, only the active tracks receive the timeline operations."},
    {"is_track_active", projectIsTrackActive, METH_VARARGS, "Tell whether a track is the active one."},
    {"set_track_name", projectSetTrackName, METH_VARARGS, "Rename a track."},
    {"move_clip", projectMoveClip, METH_VARARGS, "Move a clip of the timeline to a track and a position."},
    {"group_clips", projectGroupClips, METH_VARARGS, "Group clips together, takes a list of clip ids and returns the group id."},
    {"ungroup_clips", projectUngroupClips, METH_VARARGS, "Remove clips from their group, takes a list of clip ids."},
    {"mix_clip", projectMixClip, METH_VARARGS,
     "Mix a clip with the one next to it. The type of mix and the position, at the start or the end of the clip, are optional."},
    {"clip_mix", projectClipMix, METH_VARARGS, "The mix of a clip, empty when the clip has no mix."},
    {"remove_clip_mix", projectRemoveClipMix, METH_VARARGS, "Remove the mix of a clip."},
    {"undo", projectUndo, METH_NOARGS, "Undo the last operation on the project."},
    {"redo", projectRedo, METH_NOARGS, "Redo the last undone operation."},
    {"timeline_clips", projectTimelineClips, METH_VARARGS, "The clips of a track."},
    {"insert_clip", projectInsertClip, METH_VARARGS, "Insert a bin clip in the timeline, returns its id. The mode is normal, overwrite or insert."},
    {"remove_clip_at", projectRemoveClipAt, METH_VARARGS, "Remove the clip at a position of a track."},
    {"track_duration", projectTrackDuration, METH_VARARGS, "The duration of a track in frames."},
    {"save", projectSave, METH_VARARGS, "Save the project."},
    {nullptr, nullptr, 0, nullptr},
};

PyTypeObject projectType = {
    PyVarObject_HEAD_INIT(nullptr, 0) "kdenlive.Project",
};

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------

PyObject *moduleProject(PyObject *, PyObject *)
{
    if (!s_api) {
        PyErr_SetString(PyExc_RuntimeError, "No project is loaded");
        return nullptr;
    }
    auto *self = reinterpret_cast<PyProject *>(projectType.tp_alloc(&projectType, 0));
    if (!self) {
        return nullptr;
    }
    self->api = s_api;
    return reinterpret_cast<PyObject *>(self);
}

/** @brief kdenlive.register_action(id, label, callback) */
PyObject *moduleRegisterAction(PyObject *, PyObject *args)
{
    const char *id = nullptr;
    const char *label = nullptr;
    PyObject *callback = nullptr;
    if (!PyArg_ParseTuple(args, "ssO", &id, &label, &callback)) {
        return nullptr;
    }
    if (!PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "The action callback must be callable");
        return nullptr;
    }
    // The engine owns the actions, it also takes the reference on the callback
    PythonEngine::instance().registerAction(QString::fromUtf8(id), QString::fromUtf8(label), callback);
    Py_RETURN_NONE;
}

/** @brief kdenlive.scripts_directory() */
PyObject *moduleScriptsDirectory(PyObject *, PyObject *)
{
    return fromQString(PythonEngine::userScriptsDirectory());
}

PyMethodDef moduleMethods[] = {
    {"project", moduleProject, METH_NOARGS, "Return the project currently loaded in Kdenlive."},
    {"register_action", moduleRegisterAction, METH_VARARGS,
     "Add an action to the Scripts menu, it takes an id, a label and a callback. The callback is called with the project as its only argument."},
    {"scripts_directory", moduleScriptsDirectory, METH_NOARGS, "The directory where the user scripts are stored."},
    {nullptr, nullptr, 0, nullptr},
};

const char *moduleDoc = "The Kdenlive scripting API.\n"
                        "\n"
                        "A script edits the project that is loaded in Kdenlive:\n"
                        "\n"
                        "    import kdenlive\n"
                        "\n"
                        "    project = kdenlive.project()\n"
                        "    bin_id = project.add_clip('/path/to/video.mp4', 'My video')\n"
                        "    track_id = project.add_track('V2', False)\n"
                        "    project.set_track_active(track_id, True)\n"
                        "    project.insert_clip(bin_id, track_id, 0)\n"
                        "    project.save()\n"
                        "\n"
                        "The scripts of the user scripts directory are listed in the Scripts menu,\n"
                        "a script can also run without the interface with 'kdenlive --script'.\n"
                        "\n"
                        "See src/scripting/README.md for the complete reference.";

PyModuleDef moduleDef = {
    PyModuleDef_HEAD_INIT, "kdenlive", moduleDoc, -1, moduleMethods, nullptr, nullptr, nullptr, nullptr,
};

} // namespace

// ---------------------------------------------------------------------------
// PythonEngine
// ---------------------------------------------------------------------------

class PythonEngine::Private
{
public:
    bool initialized{false};
    // The engine owns the api, the scripts can be run at any time
    std::unique_ptr<KdenliveApi> api;
};

PythonEngine::PythonEngine(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
}

PythonEngine::~PythonEngine()
{
    delete d;
}

PythonEngine &PythonEngine::instance()
{
    static PythonEngine engine;
    return engine;
}

bool PythonEngine::initialize()
{
    if (d->initialized) {
        return true;
    }
    // The interpreter is not finalized on exit, this is the documented way to
    // avoid crashes when Python holds references to Qt objects.
    PyImport_AppendInittab("kdenlive", []() -> PyObject * {
        PyObject *module = PyModule_Create(&moduleDef);
        if (!module) {
            return nullptr;
        }
        projectType.tp_basicsize = sizeof(PyProject);
        projectType.tp_dealloc = projectDealloc;
        projectType.tp_flags = Py_TPFLAGS_DEFAULT;
        projectType.tp_doc = const_cast<char *>("A Kdenlive project.");
        projectType.tp_methods = projectMethods;
        projectType.tp_getset = projectGetSet;
        projectType.tp_new = projectNew;
        if (PyType_Ready(&projectType) < 0) {
            return nullptr;
        }
        Py_INCREF(&projectType);
        PyModule_AddObject(module, "Project", reinterpret_cast<PyObject *>(&projectType));
        return module;
    });

    Py_Initialize();
    if (!Py_IsInitialized()) {
        return false;
    }
    // The interpreter is embedded, it has no standard streams, a script would
    // not be able to print anything without this
    if (PyRun_SimpleString("import sys, io\n"
                           "sys.stdout = io.TextIOWrapper(io.FileIO(1, 'w', closefd=False), encoding='utf-8', errors='replace', line_buffering=True)\n"
                           "sys.stderr = io.TextIOWrapper(io.FileIO(2, 'w', closefd=False), encoding='utf-8', errors='replace', line_buffering=True)\n") != 0) {
        // The streams could not be set up, this is not fatal
        PyErr_Clear();
    }
    d->initialized = true;
    return true;
}

bool PythonEngine::isInitialized() const
{
    return d->initialized;
}

void PythonEngine::setApi(Core *core)
{
    d->api = std::make_unique<KdenliveApi>(core);
    s_api = d->api.get();
}

bool PythonEngine::runString(const QString &code, const QString &name, QString *errorOutput)
{
    if (!d->initialized) {
        if (errorOutput) {
            *errorOutput = i18n("The Python interpreter is not running");
        }
        return false;
    }
    PyGILState_STATE gil = PyGILState_Ensure();
    PyObject *globals = createScriptNamespace();
    const QByteArray utf8Code = code.toUtf8();
    // Compile the code with a name, so that the traceback of a failed script
    // shows the file and the line where it failed
    PyObject *result = runScript(utf8Code, name, globals);
    bool ok = result != nullptr;
    if (!ok && errorOutput) {
        PyObject *type = nullptr;
        PyObject *value = nullptr;
        PyObject *traceback = nullptr;
        PyErr_Fetch(&type, &value, &traceback);
        PyErr_NormalizeException(&type, &value, &traceback);
        // The exception is not a string, get its text representation
        if (value) {
            PyObject *text = PyObject_Str(value);
            if (text) {
                const char *utf8 = PyUnicode_AsUTF8(text);
                if (utf8) {
                    *errorOutput = QString::fromUtf8(utf8);
                }
                Py_DECREF(text);
            }
        }
        // Add the traceback, it tells where the script failed
        const QString frames = formatTraceback(traceback);
        if (!frames.isEmpty()) {
            *errorOutput += QLatin1Char('\n') + frames;
        }
        if (type) {
            Py_DECREF(type);
        }
        if (value) {
            Py_DECREF(value);
        }
        if (traceback) {
            Py_DECREF(traceback);
        }
        if (errorOutput->isEmpty()) {
            *errorOutput = i18n("Error in %1", name);
        }
    }
    Py_XDECREF(result);
    Py_DECREF(globals);
    PyGILState_Release(gil);
    return ok;
}

bool PythonEngine::runFile(const QString &path, QString *errorOutput)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorOutput) {
            *errorOutput = i18n("Could not read script %1", path);
        }
        return false;
    }
    const QString code = QString::fromUtf8(file.readAll());
    file.close();
    return runString(code, QFileInfo(path).fileName(), errorOutput);
}

bool PythonEngine::importFile(const QString &path, QString *errorOutput)
{
    if (!d->initialized) {
        if (errorOutput) {
            *errorOutput = i18n("The Python interpreter is not running");
        }
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorOutput) {
            *errorOutput = i18n("Could not read script %1", path);
        }
        return false;
    }
    const QString code = QString::fromUtf8(file.readAll());
    file.close();

    PyGILState_STATE gil = PyGILState_Ensure();
    const QString moduleName = QFileInfo(path).completeBaseName();

    PyObject *globals = createScriptNamespace();
    PyDict_SetItemString(globals, "__file__", fromQString(path));
    PyDict_SetItemString(globals, "__name__", fromQString(moduleName));

    const QByteArray utf8Code = code.toUtf8();
    PyObject *result = runScript(utf8Code, path, globals);
    bool ok = result != nullptr;
    if (ok) {
        // A script that defines PLUGIN_ENTRY registers its actions there, this
        // is the convention used by the IDA plugins.
        PyObject *entry = PyDict_GetItemString(globals, "PLUGIN_ENTRY");
        if (entry && PyCallable_Check(entry)) {
            PyObject *entryResult = PyObject_CallFunctionObjArgs(entry, nullptr);
            if (!entryResult) {
                ok = false;
            }
            Py_XDECREF(entryResult);
        }
    }
    if (!ok && errorOutput) {
        PyObject *type = nullptr;
        PyObject *value = nullptr;
        PyObject *traceback = nullptr;
        PyErr_Fetch(&type, &value, &traceback);
        PyErr_NormalizeException(&type, &value, &traceback);
        errorOutput->clear();
        if (value) {
            // The value is the exception instance, its string form holds the message
            PyObject *message = PyObject_Str(value);
            if (message) {
                const char *utf8 = PyUnicode_AsUTF8(message);
                if (utf8) {
                    *errorOutput = QString::fromUtf8(utf8);
                }
                Py_DECREF(message);
            }
        }
        if (traceback) {
            const QString frames = formatTraceback(traceback);
            if (!frames.isEmpty()) {
                *errorOutput += QLatin1Char('\n') + frames;
            }
        }
        Py_XDECREF(type);
        Py_XDECREF(value);
        Py_XDECREF(traceback);
        if (errorOutput->isEmpty()) {
            *errorOutput = i18n("Error in %1", path);
        }
    }
    Py_XDECREF(result);
    Py_DECREF(globals);
    PyGILState_Release(gil);
    return ok;
}

void PythonEngine::registerAction(const QString &id, const QString &label, void *callback)
{
    // Called from the module function, the GIL is already held there
    auto *object = static_cast<PyObject *>(callback);
    Py_INCREF(object);
    for (auto &action : s_actions) {
        if (action.id == id) {
            Py_DECREF(action.callback);
            action.callback = object;
            action.label = label;
            return;
        }
    }
    s_actions.append({id, label, object});
}

QStringList PythonEngine::actionIds() const
{
    QStringList ids;
    for (const auto &action : s_actions) {
        ids.append(action.id);
    }
    return ids;
}

QString PythonEngine::actionLabel(const QString &id) const
{
    for (const auto &action : s_actions) {
        if (action.id == id) {
            return action.label;
        }
    }
    return {};
}

bool PythonEngine::triggerAction(const QString &id)
{
    RegisteredAction *action = nullptr;
    for (auto &candidate : s_actions) {
        if (candidate.id == id) {
            action = &candidate;
            break;
        }
    }
    if (!action || !action->callback) {
        return false;
    }
    PyGILState_STATE gil = PyGILState_Ensure();
    PyObject *project = moduleProject(nullptr, nullptr);
    PyObject *result = PyObject_CallFunctionObjArgs(action->callback, project, nullptr);
    bool ok = result != nullptr;
    if (!ok) {
        PyErr_Print();
    }
    Py_XDECREF(result);
    Py_XDECREF(project);
    PyGILState_Release(gil);
    return ok;
}

QString PythonEngine::userScriptsDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/scripts");
}

QStringList PythonEngine::systemScriptsDirectories()
{
    return {QStandardPaths::locateAll(QStandardPaths::AppDataLocation, QStringLiteral("scripts"), QStandardPaths::LocateDirectory)};
}
