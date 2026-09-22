# QMdmm for macOS

Drag `QMdmm6.app` onto the `Applications` shortcut in this window to install
it.

## This build is not signed

The app is neither signed with a Developer ID certificate nor notarized, so
macOS has no way to tell where it came from and refuses the first launch,
reporting that the developer cannot be verified. Two ways to get past that:

- Allow this one app in **System Settings > Privacy & Security**, where the
  refused launch is listed with an **Open Anyway** button.
- Or clear the download flag that Finder put on the app:

      xattr -dr com.apple.quarantine /Applications/QMdmm6.app

## What is in the app

Nothing else has to be installed. The bundle carries the Qt libraries it uses,
the QML modules it loads, and the command line programs it starts as child
processes, so it can be moved, renamed or removed like any other app.

## Licence

QMdmm is free software, distributed under the GNU Affero General Public
License, version 3 or later. The source is at
<https://github.com/QMdmm/QMdmm>.
