# AGENTS.md

Guidance for AI agents working on this repository -- coding standards only.
Project management (backlog / feedback / decisions) lives in a separate ledger
repository and is intentionally not covered here.

## Reference documentation

Read these before touching code:

- [README](README.md) -- game rules, build/run instructions, protocol overview.
- [doc/architecture.md](doc/architecture.md) -- how the modules
  (`QMdmmCore` / `QMdmmNetworking` / `QMdmmBot` / `QMdmmGui` / `QMdmmServer`) fit together.
- [doc/getting-started.md](doc/getting-started.md) -- building, running a
  server, playing a game end-to-end, client API overview.

## Coding standards

### Private implementations (`<Class>P`)

- A public class's implementation may live in a private class whose name is the
  public one with a `P` suffix: `RoomP` for `Room`, `SocketP` for `Socket`.
  Declare it in `<class>_p.h`, inside the module's `p` namespace (`QMdmmCore::p`,
  `QMdmmNetworking::p`), tagged with that module's private export macro
  (`QMDMMCORE_PRIVATE_EXPORT` / `QMDMMNETWORKING_PRIVATE_EXPORT`), and put the
  implementation in `<class>_p.cpp`. A P type with no out-of-line members -- a
  plain data holder such as `RoomP` or `AgentP` -- needs no `<class>_p.cpp`.
- The public header only forward-declares it (`class ClientP;` / `struct RoomP;`)
  and keeps the instance private -- behind a `d` member, or behind a file-scope
  static in its own translation unit where there is no per-instance state.
  Wrap the forward declaration and the `friend` declaration, if any, in
  `#ifndef DOXYGEN` so neither reaches the published documentation.
- A derived implementation is named `<base P>_<concrete type>`:
  `SocketP_QTcpSocket` / `SocketP_QLocalSocket` / `SocketP_QWebSocket`,
  `SettingsWrapperP_QSettings` / `SettingsWrapperP_QVariantMap`.
- List the private headers and sources in the module's
  `QMDMM<MODULE>_PRIVATE_HEADERS` / `QMDMM<MODULE>_PRIVATE_SOURCES`. The private
  headers are installed (under `include/<Module>/private/<version>`) only when
  `QMDMM_EXPORT_PRIVATE` is on.

### Memory management

- Do not use `QScopedPointer` -- it is deprecated in Qt. Use `std::unique_ptr`
  instead. (Already applied in existing code; see commits `dddd2a4` / `2e00107`.)
- Do not use raw pointers for types that are not `QObject`-derived. Use
  `std::shared_ptr` / `std::unique_ptr` etc. instead.

### C-style variadic functions

- Do not write your own C-style variadic function (`(const char *fmt, ...)`)
  or use the `va_list` type anywhere in the codebase. Only *calling* a
  third-party variadic function (e.g. `qWarning("...%d", x)` or
  `QString::vasprintf`) is allowed.
- Variadic macros and template parameter packs are fine.
- For printf-style formatting with a dynamic argument list, use a variadic
  template + `QString::arg` chain instead. See `configError` in
  `QMdmmServer/src/config.cpp` for the canonical form.

### Use of `auto`

- Do not use `auto` when the concrete type can be written out explicitly --
  write `Protocol::PacketType`, `Client *`, `QList<LogicRunner *>`, etc.
- `auto` is allowed only in these cases:
  1. The type name is longer than 100 characters. Canonical example: the
     return type of `list2Set` in `qmdmmcore.cpp` is 126 chars --
     `QSet<typename std::remove_cv_t<typename std::iterator_traits<decltype(std::cbegin((const T &)std::declval<T>()))>::value_type>>`.
  2. The type is anonymous and cannot be named -- a lambda or an anonymous
     struct/class.
  3. An upstream library's documentation explicitly requires `auto` -- e.g.
     `qScopeGuard`, whose return type depends on the lambda closure type and
     cannot be spelled out.
- `auto foo(auto b) { return bar(b); }` template is not allowed even after C++20. use
  `template<typename T> typename decltype(bar(std::declval<T>())) foo(T b) { return bar(b); }`

### `if` / `while` / `for` conditionals

- Do not use `if (x)` when x is not a boolean value. Use `if (x != nullptr)`
  (for pointers) or `if (x != 0)` (for integers) instead. For classes which
  have `operator bool()`, only use `if (x)` when no other method is available.
  (e.g. for `std::optional<int> o;`, it is forced to use `if (o.has_value())`
  instead of `if (o)`). Same rule applies for `while` and `for` loops.

### Range-based `for` over `std::map` / `std::unordered_map`

- Do not rewrite a `std::map` / `std::unordered_map` walk as a range-based
  `for`. Keep the explicit iterator: `it->first` / `it->second` name the key
  and the value at the point of use, while a range-based `for` hands out a
  pair and reads worse.
- `clang-tidy`'s `modernize-loop-convert` will keep asking for the conversion,
  so annotate the loop instead of converting it:
  `// NOLINTNEXTLINE(modernize-loop-convert): <reason>`. Canonical example: the
  nine loops in `QMdmmCore/src/qmdmmroom.cpp`.

### Source files are pure ASCII

- Comments and identifiers must contain no Chinese (or any other non-ASCII)
  characters.
- User-visible strings must go through i18n: `tr()` in C++ / `qsTr()` in QML,
  with translations in `QMdmmGui/translations/*.ts`. Never hardcode Chinese
  (or any natural-language) strings in code.

### Code formatting

- C++: run `clang-format -i` (config: `.clang-format`) on files you touched.
- QML: run `qmlformat -i` (config: `.qmlformat.ini`); note a known indentation
  bug -- check the resulting diff afterwards.

### `clang-tidy`

- Test and smoke code (`test/` / `smoke/`) is not linted. The policy is written
  into the files themselves: each test / smoke translation unit carries a
  file-level `// NOLINTBEGIN` / `// NOLINTEND` pair, so the whole tree can be
  scanned without the caller having to remember to filter paths. The `test.h`
  helpers are headers rather than translation units, so they carry no pair.

### `using namespace`

- In production code (i.e. `src/`)
  - Only namespaces whose name ends with `[Ll]iterals` -- e.g.
    `Qt::StringLiterals` -- are allowed for UDL usage in `.cpp` file scope.
  - Other `using namespace` can only appear inside a code block.
- In test code (i.e. `test/` / `smoke/`)
  - Use whatever convenient for testing.

### `QStringLiteral`

- `QStringLiteral` macro was previously heavily used, but got replaced by UDL
  during a modernize process.
- Use `using namespace Qt::StringLiterals;` and the UDL `u"..."_s` instead of
  `QStringLiteral("...")`
  - For macro usage, see following example
  ```
#define SOME_MACRO(x) \
  if (x)              \
      v << QStringLiteral(x);
  ```
  Above example code should be modified to:
  ```
#define SOME_MACRO(x) \
  if (x)              \
      v << u"" x ""_s;
  ```

### Qt names are always versioned

- Use the versioned spelling for every Qt function and target --
  `qt6_add_executable`, `Qt6::Core` -- never the versionless one
  (`qt_add_executable`, `Qt::Core`).
- The root `CMakeLists.txt` sets `QT_NO_CREATE_VERSIONLESS_FUNCTIONS` and
  `QT_NO_CREATE_VERSIONLESS_TARGETS`, so a versionless name is not something
  that works but is discouraged: the alias is never created, CMake does not
  know the command, and configure fails outright.
- The reason is not that this tree once mixed Qt 5.15 and 6.5. It is that the
  project has to let several Qt versions coexist in a single build pass, which
  versionless names make impossible -- a versionless alias belongs to whichever
  Qt version's `find_package` ran, so only one can win per pass.
