# Qt Device Workbench

This is a standalone repository. Do not modify the surrounding media-workspace repository.
Read docs/architecture.md and include/workbench/contracts.h before coding.
The parent owns contracts, root CMake, apps/workbench, scripts, integration tests and packaging.
Each child owns ONLY its explicit assigned modules/apps/tests. Never change public contracts without asking the parent. Do not commit, push or create repositories from child agents.
All business modules are independently built SHARED libraries (Windows DLLs). Apps only compose modules, never duplicate their implementation. No source inclusion from another module; only include/workbench/contracts.h and imported CMake targets.
Use Qt 6.8+, C++17, CMake 3.24+, MSVC x64. Qt Widgets for UI, Qt Network for TCP, Qt Test for tests. No external libraries.
Use QObject worker objects moved to dedicated QThreads. Create and operate sockets/timers/files only in their owning thread. Queued cross-thread signals carry value copies; never call worker methods directly across threads. Shutdown must run in owning thread before quit/wait.
Network connections are loopback by default. NDJSON wire messages, bounded to 64 KiB per line. No sleeps or waitFor* in GUI/worker slots. Timers drive delays and retries.
Do not add placeholder binaries or claims of passing tests. Build and execute real tests; report unavailable toolchains honestly.
