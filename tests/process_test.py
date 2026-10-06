"""Exercise actual EXEs/DLLs, TCP, worker threads, CSV and downstream output.
No protocol implementation is mocked. Also used against binary-only packages.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time
import unittest

OPTIONS = None

class RealPrograms(unittest.TestCase):
    def setUp(self):
        self.folder = OPTIONS.evidence / self._testMethodName
        self.folder.mkdir(parents=True, exist_ok=True)
        self.children = []
        self.logs = []
        self.bin = OPTIONS.bin.resolve()

    def exe(self, program):
        if OPTIONS.layout == 'programs':
            return self.bin / program / (program + '.exe')
        return self.bin / (program + '.exe')

    def launch(self, program, *args):
        exe = self.exe(program)
        self.assertTrue(exe.is_file(), str(exe))
        log = open(self.folder / (program + '-' + str(len(self.children)) + '.log'), 'wb')
        env = dict(os.environ)
        if OPTIONS.layout == 'programs':
            # Qt SDK/build are intentionally absent from dependency search.
            env['PATH'] = str(exe.parent) + os.pathsep + str(Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32')
            env.pop('QT_PLUGIN_PATH', None)
            env.pop('QTDIR', None)
        env['QT_QPA_PLATFORM'] = 'offscreen'
        env['QT_QPA_FONTDIR'] = str(Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'Fonts')
        process = subprocess.Popen([str(exe), *map(str, args)], cwd=exe.parent, env=env, stdout=log, stderr=subprocess.STDOUT)
        self.children.append(process)
        self.logs.append(log)
        return process

    def ready(self, path, process):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if path.exists():
                try:
                    return json.loads(path.read_text(encoding='utf-8'))['port']
                except (ValueError, KeyError):
                    pass
            if process.poll() is not None:
                self.fail('Program exited before reporting readiness: ' + str(process.returncode))
            time.sleep(.03)
        self.fail('No ready file: ' + str(path))

    def tearDown(self):
        for process in self.children:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        for log in self.logs:
            log.close()

    def run_capture(self, mode='none', late_sink=False, complete_replay=False):
        # Unique per-test files ensure output never relies on a previous run.
        dev_ready = self.folder / 'device-ready.json'
        sink_ready = self.folder / 'sink-ready.json'
        csv_path = self.folder / 'measurements.csv'
        output = self.folder / 'downstream.ndjson'
        report_path = self.folder / 'report.json'
        duration = 65000 if complete_replay else (4100 if late_sink else 3100)
        # Server lifetime must include child process startup/GUI initialization time.
        sim = self.launch('device-simulator', '--port', 0, '--ready-file', dev_ready, '--duration-ms', duration + 15000)
        device_port = self.ready(dev_ready, sim)
        if late_sink:
            with socket.socket() as reserve:
                reserve.bind(('127.0.0.1', 0))
                sink_port = reserve.getsockname()[1]
        else:
            receiver = self.launch('result-receiver', '--port', 0, '--output', output, '--ready-file', sink_ready, '--duration-ms', duration + 15000)
            sink_port = self.ready(sink_ready, receiver)
        end_args = ['--quit-after-replay'] if complete_replay else []
        app = self.launch('device-workbench', '--headless', '--capture', '--device-port', device_port,
                          '--sink-port', sink_port, '--interval-ms', 10 if complete_replay else 20, '--duration-ms', duration,
                          '--record', csv_path, '--report', report_path, '--fault', mode, '--fault-every', 7,
                          '--screenshot', self.folder / 'interface.png', *end_args)
        if late_sink:
            time.sleep(1.2)
            receiver = self.launch('result-receiver', '--port', sink_port, '--output', output, '--ready-file', sink_ready, '--duration-ms', duration + 15000)
            self.ready(sink_ready, receiver)
        self.assertEqual(app.wait(timeout=duration / 1000 + 12), 0)
        report = json.loads(report_path.read_text(encoding='utf-8'))
        with csv_path.open(encoding='utf-8-sig', newline='') as source:
            records = list(csv.DictReader(source))
        delivered = [json.loads(line) for line in output.read_text(encoding='utf-8').splitlines() if line.strip()]
        self.assertGreater(report['samples'], 20, report)
        self.assertGreater(report['commandAcks'], 0, report)
        self.assertTrue(report['workersStopped'], report)
        self.assertTrue(all(report['threads'].values()), report)
        self.assertEqual(report['stored'], len(records), report)
        self.assertEqual(report['samples'], len(records), report)
        self.assertEqual(report['delivered'], len(delivered), report)
        self.assertEqual(len(records), len(delivered), report)
        self.assertEqual(len({(r['deviceId'], r['sequence']) for r in delivered}), len(delivered))
        for saved, forwarded in zip(records, delivered):
            self.assertEqual(saved['deviceId'], forwarded['deviceId'])
            self.assertEqual(int(saved['sequence']), forwarded['sequence'])
            self.assertEqual(int(saved['timestampMs']), forwarded['timestampMs'])
            for field in ('temperature', 'humidity', 'voltage'):
                self.assertAlmostEqual(float(saved[field]), forwarded[field], places=10)
        self.assertGreater((self.folder / 'interface.png').stat().st_size, 3000)
        if mode == 'malformed':
            self.assertTrue(report['errors'], 'Malformed device messages must be reported')
        return report

    def assert_public_values(self, report):
        self.assertEqual(report['errors'], [], report)
        with (self.exe('device-simulator').parent / 'data/intel-lab-mote1.csv').open(encoding='utf-8', newline='') as source:
            public_rows = list(csv.DictReader(source))
        with (self.folder / 'measurements.csv').open(encoding='utf-8-sig', newline='') as recorded:
            saved_rows = list(csv.DictReader(recorded))
        # Verify actual TCP -> GUI/CSV -> downstream values against the independent
        # published excerpt, not just against each other.
        for saved, original in zip(saved_rows, public_rows):
            self.assertEqual(saved['deviceId'], original['deviceId'])
            self.assertEqual(int(saved['timestampMs']), int(original['timestampMs']))
            for field in ('temperature', 'humidity', 'voltage'):
                self.assertEqual(float(saved[field]), float(original[field]))
        self.assertEqual(report['samples'], len(saved_rows))
        self.assertLessEqual(len(saved_rows), len(public_rows))

    def test_normal_capture_display_save_forward(self):
        self.assert_public_values(self.run_capture())

    def test_complete_public_recording_saved_and_confirmed(self):
        report = self.run_capture(complete_replay=True)
        self.assert_public_values(report)
        self.assertEqual(report['samples'], 4096, report)
        self.assertTrue(report['replayFinished'], report)

    def test_fragmented_messages(self):
        self.run_capture('fragment')

    def test_malformed_messages_are_filtered(self):
        self.run_capture('malformed')

    def test_device_disconnect_reconnect(self):
        self.run_capture('disconnect')

    def test_delayed_messages(self):
        self.run_capture('delay')

    def test_downstream_starts_late_and_pending_records_arrive(self):
        self.run_capture(late_sink=True)

    def test_invalid_cli_inputs_fail(self):
        for program, args in [('device-workbench', ['--headless', '--interval-ms', '0']),
                              ('device-simulator', ['--port', '99999']),
                              ('result-receiver', ['--port', '-1'])]:
            child = self.launch(program, *args)
            self.assertNotEqual(child.wait(timeout=5), 0)

    def test_full_downstream_queue_stops_device_capture(self):
        ready = self.folder / 'device-ready.json'
        sim = self.launch('device-simulator', '--port', 0, '--ready-file', ready, '--duration-ms', 20000)
        port = self.ready(ready, sim)
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1', 0))
            unavailable_port = reserve.getsockname()[1]
        report_path = self.folder / 'report.json'
        csv_path = self.folder / 'measurements.csv'
        app = self.launch('device-workbench', '--headless', '--capture', '--device-port', port,
                          '--sink-port', unavailable_port, '--interval-ms', 10, '--duration-ms', 4400,
                          '--record', csv_path, '--report', report_path)
        self.assertEqual(app.wait(timeout=15), 0)
        report = json.loads(report_path.read_text(encoding='utf-8'))
        self.assertTrue(any('queue_full' in message for message in report['errors']), report)
        self.assertGreaterEqual(report['samples'], 257, report)
        self.assertLessEqual(report['samples'], 270, report)
        self.assertEqual(report['delivered'], 0, report)
        self.assertEqual(report['samples'], report['stored'], report)
        self.assertGreaterEqual(report['commandAcks'], 2, report)
        self.assertTrue(report['workersStopped'], report)

    def test_save_failure_does_not_block_downstream(self):
        ready = self.folder / 'device-ready.json'
        sink_ready = self.folder / 'sink-ready.json'
        output = self.folder / 'downstream.ndjson'
        report_path = self.folder / 'report.json'
        sim = self.launch('device-simulator', '--port', 0, '--ready-file', ready, '--duration-ms', 20000)
        receiver = self.launch('result-receiver', '--port', 0, '--output', output, '--ready-file', sink_ready, '--duration-ms', 20000)
        port = self.ready(ready, sim)
        sink_port = self.ready(sink_ready, receiver)
        app = self.launch('device-workbench', '--headless', '--capture', '--device-port', port,
                          '--sink-port', sink_port, '--interval-ms', 20, '--duration-ms', 2400,
                          '--record', self.folder, '--report', report_path)
        self.assertEqual(app.wait(timeout=12), 0)
        report = json.loads(report_path.read_text(encoding='utf-8'))
        self.assertGreater(report['samples'], 20, report)
        self.assertEqual(report['stored'], 0, report)
        self.assertEqual(report['delivered'], report['samples'], report)
        self.assertTrue(report['errors'], report)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--bin', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--layout', choices=['flat', 'programs'], default='flat')
    OPTIONS, extra = parser.parse_known_args()
    evidence_root = OPTIONS.evidence.resolve()
    OPTIONS.evidence = evidence_root / ('run-' + str(time.time_ns()))
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(RealPrograms)
    results = unittest.TextTestRunner(verbosity=2).run(suite)
    OPTIONS.evidence.mkdir(parents=True, exist_ok=True)
    (evidence_root / 'summary.json').write_text(json.dumps({
        'tests': results.testsRun, 'failures': len(results.failures), 'errors': len(results.errors),
        'passed': results.wasSuccessful(), 'layout': OPTIONS.layout, 'runDir': str(OPTIONS.evidence)}, indent=2), encoding='utf-8')
    sys.exit(0 if results.wasSuccessful() else 1)
