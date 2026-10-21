/*
    SPDX-FileCopyrightText: 2026 Erwan Mayolle <erwan.mayolle.pro@gmail.com>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "catch.hpp"
#include "test_utils.hpp"
// test specific headers
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "scripting/kdenliveapi.h"
#include "scripting/pythonengine.h"
#ifdef HAVE_SCRIPT_EDITOR
#include "scripting/scripteditor.h"
#endif

#include "core.h"

#include <QFile>
#include <QTemporaryDir>

using namespace fakeit;

TEST_CASE("Scripting api", "[Scripting]")
{
    // Create timeline
    auto binModel = pCore->projectItemModel();
    binModel->clean();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);

    // Create document
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    std::function<bool(void)> undo = []() { return true; };
    std::function<bool(void)> redo = []() { return true; };
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);

    KdenliveApi api(pCore.get());

    // A clip with 100 frames, the mix needs free frames to work
    const QString binId = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", binModel, 100, false);

    SECTION("Add a track")
    {
        const int trackCount = api.tracks().size();
        const int trackId = api.addTrack(QStringLiteral("Scripting track"), false);
        REQUIRE(trackId > -1);
        REQUIRE(api.tracks().size() == trackCount + 1);

        const auto track = api.tracks().last();
        REQUIRE(track.id == trackId);
        REQUIRE(track.name == QStringLiteral("Scripting track"));
        REQUIRE(track.isVideo);
        REQUIRE(!track.isAudio);

        // The track can be renamed
        REQUIRE(api.setTrackName(trackId, QStringLiteral("Renamed")));
        REQUIRE(api.tracks().last().name == QStringLiteral("Renamed"));

        // Only the active tracks receive the timeline operations
        REQUIRE(api.isTrackActive(trackId));
        api.setTrackActive(trackId, false);
        REQUIRE(!api.isTrackActive(trackId));
        api.setTrackActive(trackId, true);
        REQUIRE(api.isTrackActive(trackId));

        // An unknown track is refused
        REQUIRE(api.setTrackName(-1, QStringLiteral("Nope")) == false);
        REQUIRE(!api.lastError().isEmpty());
    }

    SECTION("Insert a clip in the timeline")
    {
        const int trackId = api.addTrack(QStringLiteral("Scripting track"), false);
        REQUIRE(trackId > -1);
        api.setTrackActive(trackId, true);

        const int clipId = api.insertClip(binId, trackId, 100);
        REQUIRE(clipId > -1);

        const auto clips = api.clips(trackId);
        REQUIRE(clips.size() == 1);
        REQUIRE(clips.first().id == clipId);
        REQUIRE(clips.first().position == 100);
        REQUIRE(clips.first().duration == 100);
        REQUIRE(clips.first().binId == binId);
        REQUIRE(clips.first().trackId == trackId);

        // The clip can be found by its position
        REQUIRE(api.clipAt(trackId, 100).id == clipId);
        REQUIRE(api.clipAt(trackId, 0).id == -1);

        // An unknown clip or track is refused
        REQUIRE(api.insertClip(QStringLiteral("42"), trackId, 0) == -1);
        REQUIRE(!api.lastError().isEmpty());
        REQUIRE(api.insertClip(binId, -1, 0) == -1);
        REQUIRE(!api.lastError().isEmpty());
    }

    SECTION("Move and group the clips")
    {
        const int trackId = api.addTrack(QStringLiteral("Scripting track"), false);
        api.setTrackActive(trackId, true);
        const int clip1 = api.insertClip(binId, trackId, 100);
        const int clip2 = api.insertClip(binId, trackId, 200);
        REQUIRE(clip1 > -1);
        REQUIRE(clip2 > -1);

        // Move a clip
        REQUIRE(api.moveClip(clip2, trackId, 300));
        const auto clips = api.clips(trackId);
        REQUIRE(clips.size() == 2);
        REQUIRE(clips.at(1).id == clip2);
        REQUIRE(clips.at(1).position == 300);

        // Group the clips
        const int groupId = api.groupClips({clip1, clip2});
        REQUIRE(groupId > -1);
        // A group needs at least two clips
        REQUIRE(api.groupClips({clip1}) == -1);
        REQUIRE(!api.lastError().isEmpty());
        // An unknown clip cannot be grouped
        REQUIRE(api.groupClips({clip1, 9999}) == -1);
        REQUIRE(!api.lastError().isEmpty());

        // And ungroup them
        REQUIRE(api.ungroupClips({clip1, clip2}));
        REQUIRE(api.ungroupClips({9999}) == false);
        REQUIRE(!api.lastError().isEmpty());
    }

    SECTION("Mix the clips")
    {
        const int trackId = api.addTrack(QStringLiteral("Scripting track"), false);
        api.setTrackActive(trackId, true);
        const int clip1 = api.insertClip(binId, trackId, 0);
        const int clip2 = api.insertClip(binId, trackId, 100);
        REQUIRE(clip1 > -1);
        REQUIRE(clip2 > -1);

        // A clip that has no mix has no mix info
        REQUIRE(api.clipMix(clip1).isEmpty());

        // Mix the first clip with the second one
        REQUIRE(api.mixClip(clip1, QStringLiteral("luma"), 0));

        const auto mix = api.clipMix(clip1);
        REQUIRE(!mix.isEmpty());
        REQUIRE(mix.value(QStringLiteral("has_end_mix")).toBool());
        REQUIRE(!mix.value(QStringLiteral("has_start_mix")).toBool());
        // The other clip of the mix is the second one
        REQUIRE(mix.value(QStringLiteral("other_clip_id")).toInt() == clip2);
        REQUIRE(mix.value(QStringLiteral("duration")).toInt() > 0);

        // The mix can be removed
        REQUIRE(api.removeClipMix(clip1));
        REQUIRE(api.clipMix(clip1).isEmpty());
        // A clip without mix cannot be mixed again
        REQUIRE(api.removeClipMix(clip1) == false);
        REQUIRE(!api.lastError().isEmpty());
    }

    SECTION("Undo the operations")
    {
        const int trackId = api.addTrack(QStringLiteral("Scripting track"), false);
        api.setTrackActive(trackId, true);
        REQUIRE(api.insertClip(binId, trackId, 100) > -1);
        REQUIRE(api.clips(trackId).size() == 1);

        // The insertion is undoable
        REQUIRE(api.undo());
        REQUIRE(api.clips(trackId).isEmpty());

        // And redoable
        REQUIRE(api.redo());
        REQUIRE(api.clips(trackId).size() == 1);

        // An unknown clip cannot be mixed
        REQUIRE(api.mixClip(9999) == false);
        REQUIRE(!api.lastError().isEmpty());
    }

    SECTION("Run a python script")
    {
        REQUIRE(PythonEngine::instance().initialize());
        PythonEngine::instance().setApi(pCore.get());

        // The script fails with an assertion, the error is reported
        QString error;
        REQUIRE(!PythonEngine::instance().runString("import kdenlive\nassert False, 'the script failed'\n", "failing script", &error));
        REQUIRE(!error.isEmpty());

        // A script can edit the project
        const QString script = QStringLiteral("import kdenlive\n"
                                              "project = kdenlive.project()\n"
                                              "track_id = project.add_track('Script track', False)\n"
                                              "assert track_id > -1\n"
                                              "project.set_track_active(track_id, True)\n"
                                              "clips = project.timeline_clips(track_id)\n"
                                              "assert clips == []\n"
                                              "tracks = project.tracks()\n"
                                              "assert any(track['name'] == 'Script track' for track in tracks)\n"
                                              "assert project.error == ''\n");
        error.clear();
        const bool success = PythonEngine::instance().runString(script, "script", &error);
        INFO("The script failed with: " << error.toStdString());
        REQUIRE(success);
        REQUIRE(error.isEmpty());

        // The script did edit the project
        const auto tracks = api.tracks();
        REQUIRE(std::any_of(tracks.cbegin(), tracks.cend(), [](const ScriptTrack &track) { return track.name == QStringLiteral("Script track"); }));
    }

    SECTION("Collect the output of a script")
    {
        REQUIRE(PythonEngine::instance().initialize());
        PythonEngine::instance().setApi(pCore.get());

        QString output;
        QString error;
        const bool success = PythonEngine::instance().runStringCapturing("print('hello')\n"
                                                                         "import sys\n"
                                                                         "print('on stderr', file=sys.stderr)\n",
                                                                         "script", &output, &error);
        INFO("The script failed with: " << error.toStdString());
        REQUIRE(success);
        REQUIRE(output.contains(QStringLiteral("hello")));
        REQUIRE(output.contains(QStringLiteral("on stderr")));

        // A failed script still reports what it printed before failing
        output.clear();
        error.clear();
        REQUIRE(!PythonEngine::instance().runStringCapturing("print('before the failure')\nassert False, 'boom'\n", "script", &output, &error));
        REQUIRE(output.contains(QStringLiteral("before the failure")));
        REQUIRE(!error.isEmpty());

        // The standard streams are restored, a later script still prints
        output.clear();
        REQUIRE(PythonEngine::instance().runStringCapturing("print('again')\n", "script", &output));
        REQUIRE(output.contains(QStringLiteral("again")));
    }

#ifdef HAVE_SCRIPT_EDITOR
    SECTION("Run a script from the script editor")
    {
        REQUIRE(PythonEngine::instance().initialize());
        PythonEngine::instance().setApi(pCore.get());

        ScriptEditor editor;
        // The editor has a document, it is the one that shows the python code
        REQUIRE(!editor.script().isEmpty());

        editor.setScript(QStringLiteral("print('from the editor')\n"));
        REQUIRE(editor.script().contains(QStringLiteral("from the editor")));

        bool finished = false;
        bool success = false;
        QObject::connect(&editor, &ScriptEditor::scriptFinished, &editor, [&](bool ok) {
            finished = true;
            success = ok;
        });
        editor.runScript();
        REQUIRE(finished);
        REQUIRE(success);

        // A script that fails is reported
        editor.setScript(QStringLiteral("assert False, 'the script failed'\n"));
        finished = false;
        success = true;
        editor.runScript();
        REQUIRE(finished);
        REQUIRE(!success);
    }
#endif

    SECTION("Register an action")
    {
        REQUIRE(PythonEngine::instance().initialize());
        PythonEngine::instance().setApi(pCore.get());

        // A script registers an action, the callback gets the project
        const QString script = QStringLiteral("import kdenlive\n"
                                              "def action(project):\n"
                                              "    project.tracks()\n"
                                              "kdenlive.register_action('scriptingtest', 'Scripting test', action)\n");
        QString error;
        const bool success = PythonEngine::instance().runString(script, "script", &error);
        INFO("The script failed with: " << error.toStdString());
        REQUIRE(success);
        REQUIRE(error.isEmpty());

        // The action is registered
        REQUIRE(PythonEngine::instance().actionIds().contains(QStringLiteral("scriptingtest")));
        REQUIRE(PythonEngine::instance().actionLabel(QStringLiteral("scriptingtest")) == QStringLiteral("Scripting test"));

        // And can be triggered, the api is still valid
        REQUIRE(PythonEngine::instance().triggerAction(QStringLiteral("scriptingtest")));
    }

    SECTION("Run a script file")
    {
        REQUIRE(PythonEngine::instance().initialize());
        PythonEngine::instance().setApi(pCore.get());

        // Write a script in a file, this is how the scripts of the Scripts menu are run
        QTemporaryDir directory;
        REQUIRE(directory.isValid());
        const QString path = directory.path() + QStringLiteral("/script.py");
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("import kdenlive\n"
                   "def action(project):\n"
                   "    project.tracks()\n"
                   "kdenlive.register_action('filetest', 'File test', action)\n");
        file.close();

        QString error;
        const bool success = PythonEngine::instance().runFile(path, &error);
        INFO("The script failed with: " << error.toStdString());
        REQUIRE(success);
        REQUIRE(error.isEmpty());
        REQUIRE(PythonEngine::instance().actionIds().contains(QStringLiteral("filetest")));

        // A script that fails reports its error
        const QString failingPath = directory.path() + QStringLiteral("/failing.py");
        QFile failingFile(failingPath);
        REQUIRE(failingFile.open(QIODevice::WriteOnly | QIODevice::Text));
        failingFile.write("raise RuntimeError('the script failed')\n");
        failingFile.close();

        error.clear();
        REQUIRE(!PythonEngine::instance().runFile(failingPath, &error));
        REQUIRE(error.contains(QStringLiteral("the script failed")));
        // The error tells where the script failed
        REQUIRE(error.contains(QStringLiteral("failing.py")));
    }

    SECTION("Load a plugin")
    {
        REQUIRE(PythonEngine::instance().initialize());
        PythonEngine::instance().setApi(pCore.get());

        // A plugin defines PLUGIN_ENTRY, it registers its actions there. This is
        // how the scripts of the scripts directory are loaded.
        QTemporaryDir directory;
        REQUIRE(directory.isValid());
        const QString path = directory.path() + QStringLiteral("/plugin.py");
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("import kdenlive\n"
                   "def PLUGIN_ENTRY():\n"
                   "    def action(project):\n"
                   "        project.tracks()\n"
                   "    kdenlive.register_action('plugintest', 'Plugin test', action)\n");
        file.close();

        QString error;
        const bool success = PythonEngine::instance().importFile(path, &error);
        INFO("The plugin failed with: " << error.toStdString());
        REQUIRE(success);
        REQUIRE(error.isEmpty());
        REQUIRE(PythonEngine::instance().actionIds().contains(QStringLiteral("plugintest")));

        // A plugin that fails reports its error
        const QString failingPath = directory.path() + QStringLiteral("/failingplugin.py");
        QFile failingFile(failingPath);
        REQUIRE(failingFile.open(QIODevice::WriteOnly | QIODevice::Text));
        failingFile.write("raise RuntimeError('the plugin failed')\n");
        failingFile.close();

        error.clear();
        REQUIRE(!PythonEngine::instance().importFile(failingPath, &error));
        REQUIRE(error.contains(QStringLiteral("the plugin failed")));
        REQUIRE(error.contains(QStringLiteral("failingplugin.py")));
    }

    pCore->projectManager()->closeCurrentDocument(false, false);
}
