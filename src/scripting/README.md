# The Kdenlive scripting API

Kdenlive can be driven by Python scripts. A script gets the project that is
loaded in Kdenlive and edits it: adding clips to the bin, building a timeline,
grouping clips, applying mixes, and saving the result.

Scripts run in two ways:

- from the interface, with the **Scripts** menu. The scripts of the user
  scripts directory are listed there, and a script can also add its own entry
  with `kdenlive.register_action()`.
- from the command line, without opening the interface:

  ```sh
  kdenlive --script my_script.py project.kdenlive
  ```

The same code runs in both cases, the API is the same.

## Getting started

```python
import kdenlive

project = kdenlive.project()

# Add a file to the bin
bin_id = project.add_clip("/path/to/video.mp4", "My video")

# Add a track to the timeline and make it active
track_id = project.add_track("V2", False)
project.set_track_active(track_id, True)

# Insert the clip at the beginning of the track
clip_id = project.insert_clip(bin_id, track_id, 0)

# Save the project
project.save()
```

Every function of the `project` object is documented in the Python help:

```python
help(kdenlive.project)
```

## The project object

### Properties

| Property     | Type   | Description                                     |
|--------------|--------|-------------------------------------------------|
| `path`       | string | The path of the project file                     |
| `name`       | string | The name of the project                          |
| `profile`    | string | The video profile, for instance `atsc_1080p_25`  |
| `duration`   | int    | The duration of the project, in frames           |
| `fps`        | float  | The number of frames per second                  |
| `playhead`   | int    | The playhead position, in frames                 |
| `error`      | string | The error of the last operation, empty when it succeeded |

The `playhead` property can be set to move the playhead.

### The bin

| Function                              | Description                                            |
|---------------------------------------|--------------------------------------------------------|
| `bin_clips()`                         | The clips of the bin                                   |
| `add_clip(url, name)`                 | Import a file in the bin, returns its bin id           |
| `remove_clip(bin_id)`                 | Remove a clip from the bin                              |
| `add_subclip(bin_id, in, out)`        | Create a subclip of a bin clip, returns its bin id     |

A subclip is a zone of a bin clip, it is inserted in the timeline like any
other bin clip. The frames of the zone are included, `in` is the first frame
of the zone and `out` the last one.

The clips of the bin are dictionaries with the following keys:

| Key           | Type   | Description                                        |
|---------------|--------|----------------------------------------------------|
| `id`          | string | The bin id of the clip                             |
| `name`        | string | The name of the clip                               |
| `type`        | string | `av`, `video`, `audio`, `image`, `playlist`, `timeline`, `color` or `unknown` |
| `producer`    | string | The file or the description of the clip            |
| `duration`    | int    | The duration of the clip, in frames                |
| `is_sequence` | bool   | True for a timeline or a playlist                  |
| `is_subclip`  | bool   | True for a subclip                                 |

### The timeline

| Function                            | Description                                         |
|-------------------------------------|-----------------------------------------------------|
| `tracks()`                          | The tracks of the timeline                          |
| `add_track(name, audio)`            | Add a track, returns its id                         |
| `set_track_active(track_id, active)`| Make a track the active one                         |
| `is_track_active(track_id)`         | Tell whether a track is the active one              |
| `timeline_clips(track_id)`          | The clips of a track                                |
| `insert_clip(bin_id, track_id, position, mode)` | Insert a bin clip in the timeline, returns its id |
| `remove_clip_at(track_id, position)` | Remove the clip at a position of a track            |
| `move_clip(clip_id, track_id, position)` | Move a clip to a track and a position           |
| `group_clips(clip_ids)`             | Group clips together, returns the group id          |
| `ungroup_clips(clip_ids)`           | Remove clips from their group                       |
| `mix_clip(clip_id, mix_id, position)`| Mix a clip with the one next to it                   |
| `clip_mix(clip_id)`                 | The mix of a clip, empty when it has none            |
| `track_duration(track_id)`          | The duration of a track, in frames                  |
| `save(path)`                        | Save the project                                    |

The `insert_clip` mode is one of:

- `overwrite`: the content at the position is replaced, this is the default.
- `insert`: the content at the position is pushed to the right.
- `normal`: the operation is refused when something is already at the position,
  like the normal edit mode of the interface does.

Only the active tracks receive the timeline operations, this is what the
cursor of the interface does when it is moved. A track is active when it is
created, but `set_track_active` changes that.

The tracks of the timeline are dictionaries with the following keys:

| Key         | Type   | Description                        |
|-------------|--------|------------------------------------|
| `id`        | int    | The id of the track                |
| `name`      | string | The name of the track              |
| `is_audio`  | bool   | True for an audio track            |
| `is_video`  | bool   | True for a video track             |
| `clip_count`| int    | The number of clips on the track   |
| `duration`  | int    | The duration of the track, in frames|

The clips of a track are dictionaries with the following keys:

| Key        | Type   | Description                       |
|------------|--------|-----------------------------------|
| `id`       | int    | The id of the clip                |
| `track_id` | int    | The id of the track of the clip   |
| `position` | int    | The position of the clip, in frames |
| `duration` | int    | The duration of the clip, in frames |
| `bin_id`   | string | The bin id of the clip            |
| `name`     | string | The name of the clip              |
| `is_audio` | bool   | True for an audio clip            |
| `is_video` | bool   | True for a video clip             |

### Mixes

A mix is a transition between two clips, the second one is drawn over the
first one. A mix needs free frames in the clips, a clip that uses its whole
source cannot be mixed.

```python
# Mix the clip with the one that follows it
project.mix_clip(clip_id, "luma", 0)
# Mix the clip with the one that precedes it
project.mix_clip(clip_id, "luma", -1)
```

The mix of a clip is a dictionary with the following keys, it is empty when
the clip has no mix:

| Key             | Type | Description                                       |
|-----------------|------|---------------------------------------------------|
| `has_start_mix` | bool | True when the clip has a mix at its start         |
| `has_end_mix`   | bool | True when the clip has a mix at its end           |
| `other_clip_id` | int  | The id of the other clip of the mix               |
| `duration`      | int  | The duration of the mix, in frames                |

## The module

| Function                              | Description                                     |
|---------------------------------------|-------------------------------------------------|
| `kdenlive.project()`                  | The project currently loaded in Kdenlive        |
| `kdenlive.register_action(id, label, callback)` | Add an action to the Scripts menu       |
| `kdenlive.scripts_directory()`        | The directory where the user scripts are stored |

A script that registers an action gets the project as the only argument of
its callback:

```python
import kdenlive

def add_title(project):
    print(project.name)

kdenlive.register_action("add_title", "Add a title", add_title)
```
