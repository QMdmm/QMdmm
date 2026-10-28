# AGENTS.md

Guidance for AI agents working on this repository -- coding standards only.
Project management (backlog / feedback / decisions) lives in a separate ledger
repository and is intentionally not covered here.

## Reference documentation

Read these before touching code:

- [README](README.md) -- game rules, build/run instructions, protocol overview.
- [doc/architecture.md](doc/architecture.md) -- how the modules
  (`QMdmmCore` / `QMdmmNetworking` / `QMdmmBot` / `QMdmmGui` / `QMdmmServer` /
  `QMdmmCtl`) fit together.
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

### Export macros

- A class that carries the module's export macro already exports its members,
  so do not repeat the macro on a static member function of it. MSVC rejects
  the second marking outright -- C2487, "member of dll interface class may not
  be declared with dll interface" -- while the other toolchains stay silent
  about it, which is how five members came to carry one
  (`LogicConfiguration::defaults()` / `v1()`, `Packet::fromJson`,
  `ClientConfiguration::defaults()`, `ServerConfiguration::defaults()`, all
  cleaned up in `6d5a1f8`). Keep the macro on the class and leave the member
  bare; the headers say why at each of those sites.
- The redundant marking changes nothing about the export surface: the members
  are exported because the class is. Dropping the class-level macro is what
  stops exporting them, so that is not a way to silence MSVC.

### Data member initializers

- Do not give a data member its default in the class body (`LogLevel level =
  LogLevel::Off;`). Such an initializer is part of every constructor the
  compiler writes for the type, so it is compiled into each translation unit
  that constructs it: a header that changes the default changes code the user
  never rebuilt, and one program then runs two different definitions of the
  same constructor. A `= default` constructor written in a header has the same
  problem for the same reason -- the body it defaults to is inline there.
- Put the default in the constructor's mem-initializer list instead, with the
  constructor defined in the `.cpp`. This applies to export-private types as
  much as to public ones: `QMDMMCORE_PRIVATE_EXPORT` narrows who is meant to
  link against a type, not who has already compiled it in.
- The exception is a type that is a plain data aggregate: every data member
  public, no constructor of any kind, no virtual functions, and callers build
  it by aggregate or designated initialization (`setError({.code = ...,
  .errorString = {}})`). There is no constructor of ours for the default to be
  folded into, the class-body initializer is the only way to spell one, and
  the type is data rather than a class with an interface. The exception lasts
  only while the type is that free to change shape: once its ABI is settled, a
  hand-written constructor is no longer available -- adding one stops the type
  being an aggregate, so every call site that builds it with designated
  initializers stops compiling -- and the default in the class body is then
  the default the type is stuck with.
- The exception above leaves two shapes to spell out. A caller that builds the
  type positionally (`setError({Socket::ProtocolError, {}})`) is still
  aggregate initialization, but the weaker half of it: such a call depends on
  the order of the members, which designated initialization does not -- a
  member inserted ahead of the one it passes leaves that call compiling with a
  different meaning, where a designated one stops compiling. Write the
  designated form wherever it fits; the call sites that spell it positionally
  today are all inside macros that raise an error. The other shape is a type
  that is only ever default-constructed -- no call site builds it with
  initializers at all, as with a private struct its owner holds in a
  `std::unique_ptr` and `make_unique`s. It keeps its class-body default too:
  there is no initializer to move into a constructor the type does not have,
  and the exception lasts exactly as long as the type stays an aggregate.
- A pointer default (`QProcess *m_serverProcess = nullptr;`) is not a lighter
  case: it is compiled into the same constructors, so it reads the same way.
- `.clang-tidy` disables `cppcoreguidelines-use-default-member-init` and
  `modernize-use-default-member-init` for this reason: both propose moving an
  initializer the other way, out of the constructor and into the class body.

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

- In production code (i.e. `src/`)
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
- In test code (i.e. `test/`)
  - Use whatever convenient for testing: `auto` is fine there, including
    `auto *x = new ...` where the concrete type is plain to write out.

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
- Command-line strings are the exception: the `--help` output and the option
  names, value names and descriptions behind it are not translated. The console
  front ends (`QMdmmServer/src/config.cpp`, `QMdmmBot/src/config.cpp`) and the
  GUI's own command-line handling (`QMdmmGui/src/main.cpp`) write them as they
  are, with no `tr()`. The rule above is about what the GUI shows, not about
  what a program prints to a terminal.

### Code formatting

- C++: run `clang-format -i` (config: `.clang-format`) on files you touched.
- QML: run `qmlformat -i` (config: `.qmlformat.ini`); note a known indentation
  bug -- check the resulting diff afterwards.

### `clang-tidy`

- Test code (`test/`) is not linted. The policy is written
  into the files themselves: each test translation unit carries a
  file-level `// NOLINTBEGIN` / `// NOLINTEND` pair, so the whole tree can be
  scanned without the caller having to remember to filter paths. The `test.h`
  helpers are headers rather than translation units, so they carry no pair.

### `using namespace`

- In production code (i.e. `src/`)
  - Only namespaces whose name ends with `[Ll]iterals` -- e.g.
    `Qt::StringLiterals` -- are allowed for UDL usage in `.cpp` file scope.
  - Other `using namespace` can only appear inside a code block.
- In test code (i.e. `test/`)
  - Use whatever convenient for testing.

### `QStringLiteral`

- In production code (i.e. `src/`)
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
  - The same namespace carries the byte-string UDL `"..."_ba` for `QByteArray`.
    It takes a narrow `const char *`, so the literal has no `u` prefix --
    `u"..."_ba` matches no `_ba` overload and does not compile.
- In test code (i.e. `test/`)
  - Use whatever convenient for testing.

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

### Doxygen commands use `@`

- Spell every doxygen command with `@` -- `@defgroup`, `@ref`, `@ingroup`,
  `@c` -- and never with a backslash.
- The two prefixes are the same command to doxygen. The reason to pick one is
  that mixing them inside a single comment block reads as if the difference
  meant something, and `@` is what this tree uses.

### Timing constants in tests

- A wait for an event -- a room filling, a match ending, a program exiting, an
  answer arriving -- is bounded by what the slowest thing that will ever run it
  needs, not by what it takes on the machine it was written on. Give the bound
  a name, and put both readings in its comment: what the wait covers on a
  development machine, and what the same wait costs on a CI runner.
- The two are not close. A match is over in a fraction of a second on a build
  tree and takes tens of seconds on a runner, a round costing a second or more
  once sanitised; a packaged trio inside a container deals a round every second
  or so. A bound read off a local timing therefore says "this should be over by
  now" while the run is still playing, and the case fails as if the program were
  wrong. That is not hypothetical: within one week a set of bots was given
  twenty seconds and a packaged room twelve, and every Linux cell of the runs
  that carried them failed.
- `QMdmmBot/test/tst_qmdmmheadlessgame.cpp` is the form to copy: one constant
  per wait, a comment saying what that wait covers, and a file header carrying
  both readings and the failure that taught them.
- A leg that has not run yet has no CI reading to cite: write the number as
  several times the local one and say it is an estimate, so the next failure
  has a number to correct rather than a derivation to redo.
- This is about the bound on a wait, not about asserting how long something
  took. A measured duration may be compared against the delay you imposed
  yourself -- it cannot come back sooner than the time you held it -- but not
  against a ceiling, because a ceiling is a statement about the machine, and
  the machine the assertion runs on is not the one it was written on.
