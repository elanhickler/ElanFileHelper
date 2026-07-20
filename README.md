# 📁 ElanFileHelper

**Batch file operations you can show a user, and undo.**

A small, dependency-free C++17 library (`std::string` + `std::filesystem`,
nothing else) for file handling that treats batch operations as a
first-class workflow: build a FROM → TO list, show the user exactly what
will happen, detect every conflict *before* touching the disk, run it,
and — if anyone changes their mind — undo the whole thing from the
recorded rename history. Paths are UTF-8 throughout (converted to wide
characters internally on Windows), so `日本語`, `prüfung ✓`, and emoji
filenames all round-trip correctly.

Ported from the battle-tested JUCE-era `ElanJuceHelpers` and re-verified
from scratch (83-check smoke test, all passing).

## Why this exists

Most file code is fire-and-forget: rename in a loop, hope nothing
collides, shrug if it half-finishes. This library is built around the
opposite idea — **a batch of file operations is a transaction with a
paper trail**:

1. You stage operations (nothing touches disk yet).
2. You get a human-readable conflict report and a FROM → TO preview —
   exactly the data a confirmation dialog wants.
3. Execution is two-phase (every file first moves under a random-suffixed
   name, then the suffixes are stripped), so an A→B, B→A swap can never
   destroy a file mid-batch.
4. Every completed step is remembered. `undoTasks()` walks the history
   backwards. If a run *or* an undo fails halfway (file locked, disk
   full), calling it again continues where it left off.
5. Afterwards, the old-path → new-path map is yours — so anything that
   *linked* to those files (playlists, project files, databases) can be
   re-pointed reliably instead of breaking.

## The three classes

### `FileHelper` — the foundation

Static functions, two flavors of everything: `is...`/`does...` checks
that never throw, and `ensure...` guards that throw
`std::invalid_argument` with a specific `FileHelper::Error` message.

| Area | Functions |
|---|---|
| Existence checks (no-throw) | `doesFileExist` · `doesFolderExist` · `doesPathExist` · `doesDirContainFiles` · `doesDirContainFolders` · `isFile` · `isFolder` · `isFileOrFolder` · `isAbsolutePath` · `isRelativePath` |
| Validation (no-throw) | `isValidFilePath` · `isValidFileName` · `isValidFolderName` · `isValidPath` · `isValidFolderPath` |
| Validation (throwing guards) | `ensureValidFilePath` · `ensureValidPath` · `ensureValidDir` · `ensureValidFileName` · `ensureValidFolderName` · `ensurePathExists` · `ensureFileExists` · `ensureFileNotExist` · `ensureFolderExists` |
| Path surgery (pure string) | `getName` · `getNameNoExt` · `getDir` · `getFolder` · `getParent` · `getExt` · `setExt` · `removeExt` · `hasExt` · `addSuffix` · `assumeExt` · `assumeFolder` · `assume` · `getFileParts` (2/3/4-part overloads) · `createLegalString` · `toUniversalSlash` · `toSystemSlash` |
| Rename / move / copy | `setFileName` · `setFileDir` · `setFilePath` · `setFolderName` · `setName` · `setPath` · `copyFile` |
| Create / read / delete | `createFile` · `createTextFile` · `createTempFile` · `createFolder` · `readFile` · `permanentDelete` · `moveToTrash` (real Recycle Bin on Windows) · `removeEmptyFoldersRecursive` · `removeEmptyFilesRecursive` |
| Listing (with `*`/`?` wildcards) | `getFiles` · `getFolders` · `getFilesRecursive` · `getFoldersRecursive` |
| OS integration | `openFile` · `openFolder` · `revealFile` · `revealFolder` (Explorer/Finder) |
| Misc | `getUnusedFilePath` (auto `name(2).ext`) · `isDataEqual` · `getDateStr` · `getTimeStr` · `getDateAndTimeStr` |

```cpp
FileHelper::createTextFile("C:/notes/todo.txt", "hello", false);
FileHelper::setFileName("C:/notes/todo.txt", "done.txt", false);
auto wavs = FileHelper::getFilesRecursive("C:/samples", "*.wav");
FileHelper::moveToTrash("C:/notes/old");            // recoverable
std::string safe = FileHelper::getUnusedFilePath("C:/out/mix.wav", true); // "mix(2).wav" if taken
```

### `BatchFileChanger` — FROM → TO lists, previews, and undo

The transaction engine. Stage `move` / `rename` / `copy` / `temporary`
operations, preview, run, undo.

```cpp
BatchFileChanger changer;

// Feed it a FROM -> TO list straight from your UI:
changer.addTasks({
    { "C:/music/take1.wav", "C:/music/album/01 - intro.wav" },
    { "C:/music/take2.wav", "C:/music/album/02 - verse.wav" },
}, BatchFileChanger::Operation::move);

// 1. Show the user what's about to happen
for (auto& [from, to] : changer.getRenamePairs())
    ui.addRow(from, to);

// 2. Show conflicts (tab/newline table -> drop straight into a grid/text view).
//    Empty string means all clear.
std::string conflicts = changer.checkForConflicts();

// 3. Do it (two-phase, swap-safe, resumable on failure)
changer.runTasks();

// 4. Changed your mind? Everything walks back.
changer.undoTasks();
```

- **`addTask(from, to, op)`** — `to` can be a folder (file keeps its
  name), a full file path, or for `rename` just a new name. `temporary`
  relocates a file under a new root while preserving its full original
  folder structure (drive letter becomes a folder) — a reversible
  "set aside" bin.
- **`checkForConflicts()`** — simulates the batch against what's really
  on disk *plus* the batch itself, catching both "target already exists"
  and "two tasks want the same destination". Returns a tabbed report for
  display; running with unresolved conflicts throws.
- **`setDuplicateFormat(...)` / `applyDuplicateFormat()`** — when
  multiple tasks legitimately target the same name, auto-number them
  (`name(1).ext`, `name(01).ext`, custom regex formats, forward /
  reverse / random numbering order).
- **`runTasks()` / `undoTasks()`** — both are resumable: a failure
  (locked file, permissions) leaves a consistent state, and calling
  again continues from where it stopped.
- **`getRenamePairs()`** — the old → new map, in the order you added
  tasks. This is the robust-linking hook: after a batch, iterate it to
  update every external reference to the moved files.

### `BatchFileHelper` — bulk creation with duplicate-suffix intelligence

For the other direction: you have a list of file paths you *want to
exist* (importing, exporting, generating). It scans the target folders,
understands existing `(1)`-style suffixes, and negotiates names so
nothing is ever overwritten.

```cpp
BatchFileHelper batch({
    "C:/render/track.wav",
    "C:/render/track.wav",   // duplicate on purpose
    "C:/render/fresh.wav",
});

if (batch.checkForDuplicates())
    batch.createNewFilesAndAddDupeSuffixToExistingFiles();
    // disk now has track(1).wav (the original, content intact),
    // track(2).wav, track(3).wav, fresh.wav
else
    batch.createNewFiles();

// Full rename/creation history, for UI display or re-linking:
for (auto& change : batch.getFileChangeHistory())
    log(change.oldFile, " -> ", change.newFile);

batch.undoFileChanges(); // walks the history backwards, resumable
```

Also on board: `printRenameList()` / `printFileList()` (display-ready
summaries), `solveIllegalCharacterIssues()` (sanitize a whole list at
once), `trashListedExistingFiles()` (collect-into-temp-folder → Recycle
Bin, so even deletion is recoverable), `clearDupeNumbers()`,
`fileListContainsIllegalChars()`, and static
`getUnusedDupeNumbers(files)` — one collision-free dupe number per input
file (suffixed files keep their number, the rest get the smallest free
one), aligned with the input so `nums[i]` is always safe for `files[i]`.

## Building a UI on top

Everything a batch-rename interface needs is already surfaced as data:

| UI element | API |
|---|---|
| FROM → TO preview table | `BatchFileChanger::getRenamePairs()` |
| Conflict panel / blocking dialog | `BatchFileChanger::checkForConflicts()` (tab/newline table) |
| Duplicate-resolution preview | `BatchFileHelper::printRenameList()`, `printFileList()` |
| Per-task result (assigned number, final path) | `Task::getNewPathStr()`, `Task::getDupNumber()` |
| Undo button + history view | `undoTasks()` / `undoFileChanges()` + `getFileChangeHistory()` |
| "Show in Explorer" | `FileHelper::revealFile` / `revealFolder` |
| Friendly error messages | `FileHelper::Error` + `FileHelper::errorMessages` |
| Input validation as-you-type | `FileHelper::isValidFileName` / `createLegalString` |

## Error model

Anything prefixed `ensure`, and every disk-mutating call, throws
`std::invalid_argument` whose message is `"<path> -> <reason>"`, with
reasons drawn from the `FileHelper::Error` table (illegal character,
ends with dot, already exists, multi-slash, permission-flavored unknown
errors, ...). The `is...`/`does...` twins never throw. Batch classes
convert internal failures into `false` returns + automatic rollback
attempts, keeping exceptions at the API boundary.

## Building

Two `.cpp` files, no dependencies. Drop `include/` into your project, or:

```
cmake -B build && cmake --build build
build/smoke_test        # 90+ checks against a temp-folder sandbox
```

Windows: links `shell32` (Recycle Bin + Explorer integration). C++17 or
later. macOS/Linux compile too; `moveToTrash` currently throws off-Windows
rather than pretending.

## Provenance

Ported from `ElanJuceHelpers` (JUCE-based, ~2019) to pure std C++17.
The port fixed eleven latent bugs found along the way — including a
permanent-delete that was silently a no-op and an undo path that
destroyed the file it was about to restore — each marked with a
`ported fix:` comment at the site. The smoke test exercises the full
transaction cycle, including a real Recycle-Bin round trip.

## License

Source-available for noncommercial use only, same as the rest of the
Soundemote family. Commercial use requires a separate written commercial
license from Soundemote.
