# Contributing to WaveLathe

Thanks for looking. Bug reports, fixes and ideas are all welcome.

## Reporting a bug

Open an [issue](https://github.com/mrdatacommando/WaveLathe/issues) and
include:

- **the version**, which is shown beside the name at the top of the window
- **standalone or plugin**, and if it's the plugin, which DAW and which version
- **what you did, what you expected, and what happened instead**
- **for a crash, the dump file.** Windows writes it to `%LOCALAPPDATA%\CrashDumps`
  as `WaveLathe.exe.<number>.dmp`. Zip it first, because GitHub won't accept a
  `.dmp` file as it is. The dump says where the crash happened, which usually
  beats any description.

If the problem is about how something sounds, a short recording or the preset
or project file helps a lot.

## Before you start on a change

For anything bigger than a fix, open an issue first so we can agree on the
approach before you spend time on it.

WaveLathe reads only its own preset and project files, and this won't change.
Please don't send code that decodes another product's proprietary preset
format. Sound matching works from audio you supply, never from someone
else's files.

## Making a change

1. Build it: see [Building from source](README.md#building-from-source).
2. Run the tests and get a green result:

   ```
   powershell -ExecutionPolicy Bypass -File Tools\run-tests.ps1
   ```

3. Build with no new compiler warnings. The Release build is currently clean.
4. If you fix a bug, add a test that fails without your fix, where you can.

Match the code around you:

- C++17 and JUCE idioms, with the same naming and layout as the file you're in.
- Comments explain *why* something is the way it is, not what the next line
  does. Most files show the density that's expected.
- Files are stored exactly as they are on disk (see `.gitattributes`) with LF
  line endings. Keep them that way.
- A new source file starts with the same two-line header as the others:

  ```cpp
  // WaveLathe - Copyright (C) 2026 Mark Van de Velde
  // SPDX-License-Identifier: AGPL-3.0-or-later
  ```

  Add your own copyright line under the first one if you like.

Leave the version number alone. It's bumped when a release is cut. In the pull
request, describe what changed from a user's point of view, since that's what
goes into the changelog.

## Licence of contributions

WaveLathe is licensed under the GNU Affero General Public License, version 3 or
later. When you contribute, you agree that your contribution is licensed under
the same terms. You keep the copyright on your work. There is no contributor
licence agreement, and nothing lets anyone take your code closed-source later.
