# Fixed design and interface contract v1

Build an original generic Qt device workbench inspired by Serial Studio's receive/display/save workflow; do not copy Pro features or represent this as upstream Serial Studio.

## Programs and threads

device-simulator.exe: one Qt event-loop thread; device_simulator DLL replays prepared public measurements by default; wire_protocol DLL frames messages and simulates fragmented writes. The explicit --synthetic CLI mode retains sine fixtures for protocol exercises. replayFile is an optional QObject property configured before listen; no public factory/slot or v1 wire contract changes.
device-workbench.exe: main GUI thread owns frontend DLL; a receive/process QThread owns device_source DLL and its codec/socket/timers; a file QThread owns record_store DLL; an outbound QThread owns result_push DLL and its codec/socket/timers.
result-receiver.exe: one event-loop thread; result_receiver DLL receives, validates, deduplicates and saves accepted records.

Modules have distinct functions; visualization is INSIDE frontend, not another peer module. contracts DLL provides public types, QObject signal/slot interfaces, factories and conversion validation. Every module is a real SHARED target, never static or header-only substitution. Link each executable to only its modules and contracts; distribute module DLLs independently with public header/import libraries and metadata. Dependencies between modules go through this frozen header, no implementation source sharing.

## Data and control direction

Simulator TCP -> DeviceSource -> three independent QUEUED value-copy deliveries: Frontend.showSample, RecordStore.append, ResultPush.enqueue. Storage does not gate pushing; pushing does not gate drawing.
Frontend.connectRequested/startRequested/stopRequested/simulationRequested -> QUEUED DeviceSource slots -> TCP command -> Simulator -> TCP ack -> DeviceSource.commandResult/status -> QUEUED Frontend.showStatus.
Frontend.recordRequested/recordStopRequested/historyRequested -> QUEUED RecordStore slots. RecordStore.historyReady -> QUEUED Frontend.showHistory.
Frontend.downstreamRequested -> QUEUED ResultPush.connectSink. Push status/backlog/delivery and storage status/errors -> main-thread frontend. Shutdown: stop acquisition first, flush store in its thread, stop push in its thread, quit/wait workers, destroy owning objects.

## TCP wire contract

Defaults: simulator binds 127.0.0.1:9101; downstream binds 127.0.0.1:9102. Ports configurable; port 0 supported for tests. UTF-8, one compact JSON object per newline. CRLF accepted. Maximum encoded line/buffer 65536 bytes. Handle partial messages, multiple messages per read, malformed JSON and oversized input without crashing; codec reports errors and resynchronizes at newline. Test sender intentionally splits and combines writes. Protocol v=1; unknown version/types rejected, no unbounded input accumulation.

Command example: {"v":1,"type":"command","id":"cmd-1","action":"start","intervalMs":100}
Actions: start interval 10..60000 milliseconds, stop, fault (mode none|fragment|malformed|disconnect|delay, every positive integer). Device ID sim-001. On fault, every Nth sample is modified; delay schedules a delayed send without blocking; disconnect aborts connection. Fault acknowledgements are valid normal messages. Start/stop ack: {"v":1,"type":"ack","id":"cmd-1","ok":true,"detail":"started"}. Invalid command receives ok=false; stop stops measurement scheduling.
Sample: {"v":1,"type":"sample","deviceId":"sim-001","sequence":1,"timestampMs":1791216000000,"temperature":25.6,"humidity":60.2,"voltage":3.3}
Validation: nonempty device ID; integer sequence>=1 and timestampMs>=1; finite temperature -100..200, humidity 0..100, voltage 0..1000. Reject strings used in place of numbers, missing fields, wrong type/version and noninteger integers. JSON integers fit exact double integer range <=9007199254740991.
Finite recorded replay sends an optional terminal envelope after its final queued sample: {"v":1,"type":"stream_end","deviceId":"intel-lab-mote1","sequence":4096,"reason":"replay_finished"}. The receiver accepts it only for a previously seen device and a positive integer sequence no lower than that device's last validated sample; gaps can come from deliberately corrupted sample messages. It clears capture intent and reports replay completion to the GUI. Unknown/stale/invalid terminal messages remain errors. Sample and CSV contracts are unchanged.
DeviceSource reconnects after disconnect using nonblocking timer. Track requested measurement interval and restart after reconnect. Reject duplicate/out-of-order samples within same device session; sequence in Simulator monotonically increases across reconnect, starts at 1 for new simulator process. Device restart with sequence reset requires reconnect/new session detection (document and test behavior). Pending command acknowledgements time out after 2 seconds and report failure. DeviceSource exposes all signals in contracts.h, no new public API.

## Saving and pushing

RecordStore writes CSV in UTF-8, header deviceId,sequence,timestampMs,temperature,humidity,voltage; numbers use locale-independent formatting and precision that round-trips. CSV fields quoted/escaped. begin creates parent directory, truncates chosen file, reports errors; append without recording is ignored. stored only after successful write/flush. stop/shutdown flush and close. load validates headers/rows, limits maximum 100000 records, reports corruption, emits historyReady; do not silently fabricate missing values.
ResultPush delivers the same sample JSON. Downstream ACK after validating AND flushing its output: {"v":1,"type":"received","deviceId":"sim-001","sequence":1}. Sender keeps bounded queue (256 records), at most one unacknowledged record, reconnects and resends unacknowledged head on timeout/disconnect. delivered only after matching deviceId AND sequence. Full queue rejects newest and emits error containing queue_full; host stops acquisition and visibly reports failure. Do not silently lose data. ResultReceiver deduplicates deviceId+sequence in current process, writes one NDJSON line per accepted unique sample and ACKs retries without duplicate file lines. OutputPath empty means console-only acceptance; output file failure must not ACK accepted. Persistence of push queue across application restart is explicitly out of scope; reconnect within same process IS in scope. Rate limited UI refresh (about 20 Hz) and bounded plot history <=1000 points; display all numeric current values. Cross-thread sample emission carries values; no shared mutable buffers.

## Build and tests

Parent provides contracts, CMake and host composition. Children create their own modules/<name>/CMakeLists.txt with SHARED target wb_<name>, public include root, private Qt dependencies, link wb_contracts and wb_wire_protocol where needed. Factory export definitions use extern "C" Q_DECL_EXPORT; public imported factory declarations in contracts.h must be conditionally exported for the implementing target (parent will supply macros). Each module has tests/<module>_test.cpp using QtTest QTEST_GUILESS_MAIN, frontend uses QTEST_MAIN; tests register wb types and should assert thread affinity, real sockets, failure conditions, delivery correctness, NOT implementation duplication. Report files and executed test results. Parent's process tests launch real executables and verify recording, forwarding, command ack, malformed/fragmented data, reconnect, shutdown, binary-only deployment.

## Ownership

simulator agent: modules/wire_protocol, modules/device_simulator, apps/simulator; tests/wire_protocol_test.cpp and tests/device_simulator_test.cpp.
acquisition agent: modules/device_source, modules/frontend; tests/device_source_test.cpp and tests/frontend_test.cpp.
delivery agent: modules/record_store, modules/result_push, modules/result_receiver, apps/receiver; tests/record_store_test.cpp, tests/result_push_test.cpp, tests/result_receiver_test.cpp.
parent: contracts implementation/header, root build/test registration, apps/workbench, scripts, docs, examples, Git/GitHub, packaging and independent integration verification.
