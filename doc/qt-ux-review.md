# Qt interface: five UX improvement proposals

A review of the VLC 4.0 Qt/QML interface (`modules/gui/qt/`) against everyday
user tasks. Each item below is a gap found in the current code, not a
hypothetical. File and line references are against the tree at the time of
writing.

The five are ordered by how often a typical user hits them.

---

## 1. Preferences are unsearchable in the view users actually land in

### Problem

`PrefsDialog` has three modes — Simple, Advanced ("All"), and Expert —
selected by the button row at the bottom of the dialog. Two of them have a
search box. The default one does not.

* `setExpert()` builds an `expert_table_filter` `SearchLineEdit` plus a
  `Ctrl+F` shortcut — `dialogs/preferences/preferences.cpp:201`
* `setAdvanced()` builds a `tree_filter` `SearchLineEdit` plus a `Ctrl+F`
  shortcut — `dialogs/preferences/preferences.cpp:245`
* `setSimple()` builds a category list and a panel stack, and nothing
  else — `dialogs/preferences/preferences.cpp:274`

The default landing mode is Simple (`qt-initial-prefs-view` defaults to `0`,
`preferences.cpp:170`). So the standard path is: open Preferences, land in a
mode with no search, and either scan six category icons by hand or discover
that the "All" radio button reveals a search field.

The three modes are also disjoint. Advanced-mode search filters a tree of
module names; Expert-mode search filters a flat table of raw config keys.
Neither can return a result that lives on a Simple panel, and there is no
navigation from a hit in one mode to the friendly control for the same
setting in another. A user who searches "subtitle" in Advanced mode gets
module nodes, not the subtitle font-size spinner they were looking for —
which is sitting in Simple → Subtitles, three clicks away.

Worth noting how far the good version of this already exists elsewhere in the
same dialog: the hotkey editor has a search field with an "in" scope selector
(`dialogs/preferences/preferences_widgets.cpp:1294`). The pattern is
established; it just is not applied to the settings themselves.

### Why it matters

Preferences is where users go when something is wrong — audio device, subtitle
encoding, hardware decoding, network caching. Every one of those is a
"something is broken, fix it now" moment, and the current design answers with a
category grid.

### Proposal

Add a single search field above the mode selector that stays visible in all
three modes, and make Simple panels searchable content.

Concretely:

1. Give `SPrefsPanel` an index of the controls it builds. `SPrefsPanel`
   already keeps `QList<ConfigControl *> controls`
   (`simple_preferences.hpp:117`), and every `ConfigControl` knows its
   `module_config_t`, so name, title and longtext are already available. Add a
   method that returns `(panel id, widget, searchable text)` triples.

   `controls` alone is not sufficient coverage, though. The Media Library panel
   builds `MLFoldersEditor` widgets and a reload button directly and appends no
   `ConfigControl` at all (`simple_preferences.cpp:1023`), so a `controls`-only
   index silently omits it. The index needs a second source for
   directly-constructed widgets — simplest is to let each panel register extra
   `(widget, label text)` pairs explicitly, falling back to walking the panel's
   `QLabel`/`QGroupBox` titles.
2. Build the index lazily but eagerly enough to search: currently panels are
   constructed on first visit (`preferences.cpp:295`). Searching needs every
   panel constructed, which is cheap enough to do on first search rather than
   on dialog open. Note the panel count is not fixed at six — the Media Library
   category is only added when a media library instance exists
   (`simple_preferences.cpp:263`), so the index must be built from the
   categories actually registered rather than from a hardcoded count.
3. On a hit, switch to the owning panel, scroll the control into view, and
   flash a highlight on it. This is the interaction users know from browser and
   OS settings search.
4. Show cross-mode results in the same dropdown, labelled by mode, so an
   Advanced-only setting is still reachable from the same box. This closes the
   "I searched and found nothing, but it exists" trap.
5. Move the `Ctrl+F` shortcut to the dialog level so it works regardless of
   mode.

Effort: moderate. Steps 1–3 are self-contained and deliver most of the value;
step 4 can follow.

---

## 2. The play queue cannot be searched or filtered

### Problem

The media library and the network browser both have search. The play queue
does not.

* `SearchBox.qml` exists as a reusable widget (`widgets/qml/SearchBox.qml`)
  and is wired into `PageExt.qml` and `network/qml/BrowseTreeHeader.qml`.
* `BaseModel` exposes `searchPattern` as a first-class property —
  `util/base_model.hpp:34`.
* `PlaylistListModel` derives straight from `QAbstractListModel`
  (`playlist/playlist_model.hpp:36`) and has no filter property.
* `PlaylistController`'s full invokable surface is play/pause/stop/next/prev,
  `clear`, `goTo`, `append`, `insert`, `shuffle`, `sort`
  (`playlist/playlist_controller.hpp:112-138`). There is no search.
* `PlaylistToolbar.qml` offers sort and clear, nothing else.

So the play queue supports sorting a 500-item list and shuffling it, but
finding one track in it means scrolling.

### Why it matters

The queue is the one list in VLC that the user built themselves, which makes it
the list they most expect to be able to interrogate. "Where is that track I
added twenty minutes ago" is a routine question with no answer short of
scrolling, and it gets worse exactly as the queue gets more valuable.

The asymmetry is also a learnability problem in itself: search works in the
library view, so users reasonably assume it works in the queue, try it, and
find nothing to type into.

### Proposal

Add filter-as-you-type to the play queue, matching the library's interaction so
the two feel like the same product.

1. Add a `searchPattern` property to `PlaylistListModel` backed by a
   `QSortFilterProxyModel`, or lift the model onto `BaseModel` the way the
   library models are. Match against title, artist and album — the roles the
   delegate already renders (`playlist/qml/PlaylistDelegate.qml`).
2. **Map proxy rows back to source rows on every path that consumes an index.**
   This is the bulk of the work and the main source of risk, because the queue
   currently passes view row numbers straight into source-order APIs:
   activation calls `MainPlaylistController.goTo(index, true)`
   (`PlaylistPane.qml:353`), drag-and-drop calls
   `MainPlaylistController.insert(index, ...)` (`:251`, `:260`), Delete calls
   `model.removeItems(...)` (`:345`), and current-item positioning uses
   `MainPlaylistController.currentIndex` (`:296`). Under a filter, activating
   visible row 0 would play source row 0 rather than the item the user clicked.
   Every one of those call sites needs an explicit map, plus the context menu
   and the move/drop handlers.
3. Filter the *view* only, never the playback order. Filtering must not change
   what plays next, and clearing the filter must restore the full list
   untouched. Keep the currently-playing item visible (or pinned) even when it
   does not match, so the user never loses their place.
4. Placement needs a small widget change, not a drop-in. `SearchBox.qml`
   expands its text field downward — `anchors.top: iconButton.bottom`
   (`widgets/qml/SearchBox.qml:126`) — and `PlaylistToolbar` is the bottom item
   of `PlaylistPane`, so an unmodified `SearchBox` would open its field below
   the toolbar and out of the pane. Either give `SearchBox` an upward-opening
   mode or put the field above the list instead of in the bottom toolbar.
5. Make Delete-key removal operate on the filtered selection, and be explicit
   in the UI that a filter is active when it is — an active filter plus a
   destructive action is exactly where users make mistakes (see item 3).

Effort: moderate. The model-side filtering and the widget are nearly free; the
real cost is the proxy-to-source mapping audit in step 2, which touches most of
`PlaylistPane.qml` and must be complete to avoid acting on the wrong item.

---

## 3. Destructive play queue actions are one click, with no confirmation and no undo

### Problem

There is no undo anywhere in the play queue. Two paths destroy user work
immediately:

* The toolbar clear button: `onClicked: MainPlaylistController.clear()` —
  `playlist/qml/PlaylistToolbar.qml:145`. That calls straight through to
  `vlc_playlist_Clear()` (`playlist/playlist_controller.cpp:727-732`). No
  dialog, no snapshot.
* The Delete key: `Keys.onDeletePressed: model.removeItems(...)` —
  `playlist/qml/PlaylistPane.qml:345`. Fires on whatever is selected, with no
  guard.

`grep -i undo` across `modules/gui/qt/playlist/` and `src/playlist/` returns
nothing. Neither `PlaylistController` nor `PlaylistListModel` retains removed
items.

The clear button sits in the same toolbar row as the sort control, at a size
and spacing where a misclick is entirely plausible — and the result of that
misclick is unrecoverable.

### Why it matters

A hand-assembled queue can represent a lot of work: files dragged in from
several folders, ordered deliberately, built up over a session. It is not
persisted anywhere the user can get back to, so a stray click on Clear is
permanent data loss in the only sense that matters to the person it happens to.

The severity is worth stating plainly: this is the only place in the reviewed
UI where a single unmodified click destroys unrecoverable user-created state.

Undo is also the right primitive rather than just a confirmation dialog — the
Delete key path is *supposed* to be fast, and putting a modal in front of it
would make normal pruning tedious. Confirmation and undo solve different
halves.

### Proposal

Implement both, each where it fits:

1. **Undo for removals.** Keep a bounded stack (say 10 entries) in
   `PlaylistController` holding removed items plus their indices. Reinsertion
   already exists via `PlaylistController::insert`.

   Capturing the removals is the part that needs care. `PlaylistListModel::removeItems`
   calls `vlc_playlist_RequestRemove` directly (`playlist/playlist_model.cpp:338`),
   bypassing `PlaylistController` entirely, and the controller's
   `on_items_removed` callback only learns an index and a count *after* the
   items are gone — too late to snapshot them. So a stack that merely lives in
   the controller would leave the keyboard and context-menu paths
   non-undoable. The model must take the snapshot while it still holds the
   items and hand it to the controller before calling into the core.
2. Bind undo in the playlist pane and add an entry to the queue's context menu
   — but not unconditionally to `Ctrl+Z`. On macOS the `key-random` default is
   `Command+z` (`src/libvlc-module.c:2375`), the same chord as the platform's
   standard Undo, so an unguarded binding either shadows random-toggle or
   collides with it. Route undo through a conflict-checked action and, on
   macOS, either scope it to the playlist pane's focus or reassign the random
   binding.
3. **A snapshot for Clear.** Push the whole queue onto the same stack before
   `vlc_playlist_Clear()`, making Clear undoable rather than merely confirmed.
4. **A toast with an inline Undo action.** "Removed 12 items — Undo". This is
   strictly better than a pre-action confirm dialog: no friction in the common
   case, full recovery in the mistake case. The toast infrastructure already
   exists (`Widgets.DrawerExt` as used by the error popup in
   `dialogs/dialogs/qml/Dialogs.qml:158`).
5. **A confirmation for Clear only when the queue is large** (a threshold of
   ~25 items is a reasonable starting point) and only if step 3 is not shipped.
   If Clear is undoable, the dialog is unnecessary.

Effort: moderate, and the payoff is disproportionate — this is the highest
severity-per-line item in this document.

---

## 4. Playback errors discard the information the user needs

### Problem

When a file fails to open, the core raises a two-part error: a title and a
detail string that names the failing MRL.

```c
vlc_dialog_display_error( p_input, _("Your media can't be opened"),
                          _("VLC is unable to open the MRL '%s'."
                          " Check the log for details."), psz_mrl );
```
— `src/input/input.c:2936`

Both parts reach `DialogErrorModel::pushError`, which stores them
(`dialogs/dialogs/dialogmodel.cpp:116`). But the toast renders only the title:

```qml
text: (DialogErrorModel.repeatedMessageCount > 1 ? '[' + ... + '] ' : '')
      + DialogErrorModel.notificationText
```
— `dialogs/dialogs/qml/Dialogs.qml:212`

and `notificationText` returns `lastNotificationText`, which is assigned
`error.title` only (`dialogmodel.cpp:126-131`). The `text` field — the one
holding the filename — is never displayed in the toast.

So the user sees a red bar reading "Your media can't be opened", for 5 seconds
(`Dialogs.qml:259`), with no indication of *which* media. In a 200-item queue
where item 47 is a dead symlink, that message is close to useless.

The escape hatch is better than the toast, though it is not obvious from the
button. "Show Details" calls `DialogsProvider.messagesDialog(1)`
(`Dialogs.qml:232`) → `MessagesDialog::showTab(1)`, which selects tab index 1 —
the **Errors** tab (`dialogs/messages/messages_panel.ui:93`), not the raw
message log. That tab does render both halves of every error, title *and*
detail, so the MRL is there (`messages.cpp:195` and `:199`).

So the information is not unreachable; it is just absent from the surface the
user is actually looking at, and the route to it is unmarked. What is fair to
say about the destination is narrower than "it's a log viewer": the Errors tab
is a plain `QPlainTextEdit` of appended text with no per-error structure or
actions, and it sits in a window shared with a verbosity spinbox
(`messages.cpp:99`), a "Save log file as..." button, and a Modules Tree tab —
framing that reads as diagnostics rather than as "here is the file that
failed".

And the detail text itself instructs the user to "Check the log for details",
which is the application asking the person to do its job.

### Why it matters

Failure is the moment when interface quality matters most, because it is the
only moment when the user is stuck. The information needed to get unstuck —
the filename, the reason — exists in memory, is passed to the UI layer, and is
then dropped on the floor before rendering.

The 5-second timeout compounds it: errors that arrive while the user is looking
elsewhere are gone with no persistent indicator. There is a badge count on
platforms with Qt ≥ 6.5 (`dialogmodel.cpp:133-138`) but nothing in-window.

### Proposal

1. **Show the detail text.** Render `title` as the toast headline and `text`
   beneath it, elided to two lines. Requires exposing the last error's `text`
   alongside `notificationText`, or binding the toast to the last row of the
   model rather than to a cached string. This is a small change and by itself
   fixes most of the problem.
2. **Put the filename first.** Change the core message so the identity of the
   failing item leads, and drop "Check the log for details" — replace it with a
   reason where one is available (file not found / permission denied /
   unsupported format are all distinguishable at the point of failure and
   deserve distinct, actionable messages).
3. **Give the existing Errors tab structure.** The destination already exists
   and already has the data, so this is presentation, not plumbing: replace the
   flat `QPlainTextEdit` with one row per error — timestamp, item name, reason —
   and per-row actions ("Show in folder", "Remove from queue"). Separating it
   from the verbosity/log furniture would also stop it reading as a diagnostics
   panel.
4. **Do not silently expire errors.** Let the toast auto-hide, but leave a
   persistent, dismissible indicator when unacknowledged errors remain, so the
   5-second window is not the only chance to notice.
5. **Coalesce per-item failures.** Ten dead files in one queue should produce
   one "10 items could not be played" summary opening onto the list, not ten
   sequential toasts. The repeat counter (`repeatedNotificationCount`,
   `dialogmodel.cpp:126`) is a starting point but currently only counts
   identical titles rather than grouping distinct items under one heading.

Effort: item 1 is a few lines. Items 3 and 5 are a small feature.

---

## 5. Keyboard shortcuts are undiscoverable from inside the player

### Problem

VLC is a keyboard-driven application with a large default hotkey set. There is
no in-application reference for it.

The Help menu contains exactly three entries — Help (F1), Check for Updates,
and About (`menus/menus.cpp:553-570`). `HelpDialog` is a `QTextBrowser`
displaying a fixed HTML blob (`dialogs/help/help.cpp:55-78`) whose entire
answer on the subject is a link out to a wiki:

```
"<p>To understand the main keyboard shortcuts, read the
 <a href=\"http://wiki.videolan.org/Hotkeys\">shortcuts</a> page.</p>"
```
— `include/vlc_intf_strings.h:83`

The only in-app surface listing shortcuts is Preferences → Hotkeys
(`dialogs/preferences/simple_preferences.cpp:984`), which is a *rebinding
editor*: a three-column table of every bindable action, reached through
Tools → Preferences → Hotkeys. It is searchable
(`preferences_widgets.cpp:1294`), which is good, but it is a configuration
tool, not a reference, and nothing in the player points to it.

Separately, those wiki URLs are plain `http://` — worth fixing regardless of
what else changes here.

### Why it matters

Shortcuts are the difference between VLC-as-a-window-with-buttons and
VLC-as-a-tool, and the feature is effectively hidden. A user who does not
already know that `Shift+Right` seeks 3 seconds
(`key-jump+extrashort` / `extrashort-jump-size`, `src/libvlc-module.c:2453`
and `:2751`), that `[` and `]` change playback speed fine-grained
(`:2437-2438`), or that `e` steps one frame (`:2471`) will not find out from
the application. Sending them to a web page to learn their own player's
controls is a poor answer, and a wrong one when the machine is offline.

There is also an accuracy problem that a static page cannot solve. Defaults are
per-platform — on macOS the same three actions are `Command+Ctrl+Right`, *no
binding at all* for fine speed control, and `e`
(`src/libvlc-module.c:2311-2326`) — and on top of that, users rebind. The
shortcuts a given person actually has are knowable only from their live
configuration, which is precisely what an in-app reference can read and a wiki
page cannot.

### Proposal

1. **Add a "Keyboard Shortcuts" entry to the Help menu**, bound to a
   conventional key (`Ctrl+/` or `?`), opening a read-only, searchable,
   grouped list — Playback, Navigation, Audio, Video, Subtitles, Interface.
2. **Generate it from the live configuration**, the same source
   `KeySelectorControl` reads, so it always reflects the user's actual
   bindings, including rebound ones. This is what makes an in-app reference
   strictly better than the wiki page rather than a duplicate of it.
3. **Link the two directions**: an "Edit shortcuts…" button in the reference
   opening the Hotkeys preferences panel, and a hint in that panel that the
   reference exists.
4. **Show shortcuts on the controls that have them.** Player control tooltips
   are the natural teaching surface — "Play (Space)", "Fullscreen (F)". The
   control bar buttons already carry `description` strings
   (`player/qml/controlbarcontrols/`), so appending the current binding is a
   contained change and it teaches shortcuts passively, to users who would
   never open a reference at all.
5. **Fix the `http://` links** in `I_LONGHELP` to `https://`.

Effort: small. Item 4 arguably delivers the most learning per line of code.

---

## Cross-cutting note

Four of these five are not missing infrastructure — they are existing
infrastructure not connected to the place the user needs it:

| Gap | Machinery that already exists |
|---|---|
| No settings search in Simple mode | `SearchLineEdit`, used in two other modes of the same dialog |
| No play queue search | `SearchBox.qml` + `BaseModel::searchPattern`, used by the library views |
| Notifications show no detail | `DialogError::text` is populated, stored, and already rendered in the Errors tab — just not in the notification |
| No shortcut reference | Hotkey table and config are already searchable in Preferences |

Only undo (item 3) requires genuinely new state. That makes this a favourable
set to work through: the cost is mostly wiring, and the results are visible in
the paths users take most often.
