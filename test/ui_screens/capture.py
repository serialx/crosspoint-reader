#!/usr/bin/env python3
"""Capture a declared UI matrix with the pinned eink-qemu; no device access."""
import argparse
import hashlib
import json
import re
import subprocess
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def records(path):
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]


def steps_for(group, machine):
    steps = [] if group.get('boot') else ['ready']
    for screen in group['screens']:
        if machine not in screen.get('machines', ['x3', 'x4']):
            continue
        steps.append('mark:before-' + screen['id'])
        navigation = screen['steps'].get(machine, []) if isinstance(screen['steps'], dict) else screen['steps']
        for step in navigation:
            if group.get('rotates') and step.startswith('press:'):
                steps.append('wait-quiet:1')
            steps.append(step)
            if step == 'wait-log:Page render':
                steps.append('wait-quiet:2')
            if group.get('input_quiet_ms') == 0 and step.startswith('press:'):
                steps.append('wait-quiet:1')
        if not group.get('boot'):
            steps.append('wait-quiet:1')
        steps.extend(['shot:' + screen['id'], 'mark:after-' + screen['id']])
    return steps


def summarize(run, group, machine):
    rows = records(run / 'run.jsonl')
    console = records(run / 'console.jsonl')
    by_name = {r['name']: r for r in rows if r.get('step') in ('mark', 'shot')}
    trace_path = run.parent / 'panel.trace'
    waveforms = [line[line.index('uc8279_refresh '):] for line in trace_path.read_text().splitlines()
                 if 'uc8279_refresh ' in line] if trace_path.exists() else []
    result = []
    for screen in group['screens']:
        if machine not in screen.get('machines', ['x3', 'x4']):
            continue
        sid = screen['id']
        before, shot, after = (by_name[k] for k in ('before-' + sid, sid, 'after-' + sid))
        logs = [r['line'] for r in console if before['t'] <= r['t'] <= after['t']]
        activity = None
        stack = []
        for r in console:
            if r['t'] > shot['t']:
                break
            match = re.search(r'Entering activity: (.+)', r['line'])
            if match:
                activity = match[1]
            if 'Pushed to activity stack' in r['line']:
                stack.append(activity)
            if 'Popped from activity stack' in r['line']:
                activity = stack.pop()
            exited = re.search(r'Exiting activity: (.+)', r['line'])
            if exited and stack and stack[-1] == exited[1]:
                stack.pop()
        refresh = []
        controller_modes = []
        controller_logs = logs
        if machine == 'x4' and screen['activity'] == 'Boot':
            # Boot is photographed before normal readiness. Its completed refresh
            # log can be drained after the final mark; this case has one paint.
            controller_logs = [r['line'] for r in console if r['t'] >= before['t']]
        for line in logs:
            match = re.search(r'\[EPD\] displayBuffer mode=(\d+)', line)
            if match:
                refresh.append(['full', 'half', 'fast'][int(match[1])])
            elif '[EPD] displayGrayBuffer' in line:
                refresh.append('grayscale')
            elif '[EPD] displayWindow' in line:
                refresh.append('window')
        for line in controller_logs:
            match = re.search(r'\[SSD1677\] (FULL|HALF|FAST) refresh .*?(\(ctrl2=.*)', line)
            if match:
                controller_modes.append(match[1].lower() + ' ' + match[2])
        if machine == 'x3' and waveforms:
            controller_modes = waveforms[before['panel_refreshes']:after['panel_refreshes']]
        if machine == 'x4' and screen['activity'] == 'Boot':
            if len(controller_modes) != after['panel_refreshes'] - before['panel_refreshes']:
                raise RuntimeError(f'{run}: boot controller log/count mismatch')
        result.append({'id': sid, 'activity_observed': activity, 'activity_expected': screen['activity'],
                       'image': str((run / shot['file']).relative_to(run.parent.parent)),
                       'png_sha256': sha(run / shot['file']), 'pixels_sha256': shot['pixels_sha256'],
                       'size': shot['size'], 'refresh_requests': refresh,
                       'controller_refreshes': controller_modes,
                       'refresh_source': 'SDK API logs plus ' + ('UC8279 panel trace' if machine == 'x3' else 'SSD1677 driver logs'),
                       'panel_refreshes': after['panel_refreshes'] - before['panel_refreshes'],
                       'panel_refresh_ms': round(after['panel_refresh_ms'] - before['panel_refresh_ms'], 3),
                       'host_seconds': round(after['t'] - before['t'], 3),
                       'guest_ms': after['guest_ms'] - before['guest_ms']})
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--emulator-identity', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--machines', nargs='+', choices=['x3', 'x4'], default=['x3', 'x4'])
    p.add_argument('--orientations', nargs='+', type=int, choices=range(4), default=list(range(4)))
    p.add_argument('--dark', nargs='+', type=int, choices=[0, 1], default=[0, 1])
    p.add_argument('--groups', nargs='+')
    p.add_argument('--gdb', type=Path, default=Path.home() / '.platformio/packages/tool-riscv32-esp-elf-gdb/bin/riscv32-esp-elf-gdb')
    args = p.parse_args()
    suite = json.loads((HERE / 'screens.json').read_text())
    groups = [g for g in suite['groups'] if not args.groups or g['id'] in args.groups]
    if args.groups and set(args.groups) - {g['id'] for g in groups}:
        p.error('Unknown group')
    identity = json.loads(args.emulator_identity.read_text())
    for key in ['cli', 'qemu']:
        if sha(identity[key]) != identity[key + '_sha256']:
            raise ValueError(f'{key} identity mismatch')
    build = args.build.resolve()
    artifacts = {name: sha(build / name) for name in ['firmware.bin', 'firmware.elf', 'partitions.bin', 'bootloader.bin']}
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    meta = {'schema': 1, 'status': 'running', 'suite_sha256': sha(HERE / 'screens.json'),
            'harness_sha256': {name: sha(HERE / name) for name in
                               ['capture.py', 'fixtures.py', 'fault.py', 'screens.json']},
            'xtc_fixture_sha256': sha(HERE.parent / 'xtc_diff/generate.py'),
            'emulator': identity, 'artifacts': artifacts,
            'build': str(build), 'rtc': '2026-01-01T09:00:00', 'sleep': 'off', 'turbo': False,
            'ghosting': False, 'input_quiet_ms': 200, 'runs': []}
    save(out / 'manifest.json', meta)
    started = time.monotonic()
    try:
        for machine in args.machines:
            for orientation in args.orientations:
                for dark in args.dark:
                    for group in groups:
                        if orientation and not group.get('rotates', False):
                            continue
                        if machine not in group.get('machines', ['x3', 'x4']):
                            continue
                        name = f'{machine}-o{orientation}-d{dark}-{group["id"]}'
                        case = out / name
                        case.mkdir()
                        config = case / 'config'
                        config.mkdir()
                        settings = dict(suite['settings'], orientation=orientation, screenInverted=dark)
                        settings.update(group.get('settings', {}))
                        save(config / 'settings.json', settings)
                        save(config / 'state.json', {'showBootScreen': True})
                        for filename, contents in group.get('config_files', {}).items():
                            save(config / filename, contents)
                        from fixtures import prepare
                        assets = prepare(case, group.get('fixture', 'empty'))
                        script = case / 'steps.txt'
                        script.write_text('\n'.join(steps_for(group, machine)) + '\n')
                        input_quiet = group.get('input_quiet_ms', meta['input_quiet_ms'])
                        cmd = [identity['cli'], 'run', str(build), '--machine', machine, '--qemu', str(Path(identity['qemu']).parent),
                               '--sleep', 'off', '--rtc', meta['rtc'], '--input-quiet',
                               str(input_quiet),
                               '--timeout', '90', '--out', str(case / 'run'),
                               '--script', str(script), '--qemu-arg=-trace', '--qemu-arg=uc8279_refresh']
                        cmd.extend(['--qemu-arg=-D', '--qemu-arg=' + str(case / 'panel.trace')])
                        if group.get('boot'):
                            cmd.extend(['--settle', '0', '--panel-idle', '0'])
                        for path in sorted(config.iterdir()):
                            cmd.extend(['--put', str(path) + '=/.crosspoint'])
                        for asset, target in assets:
                            cmd.extend(['--put', str(asset) + '=' + target])
                        print(name, flush=True)
                        tick = time.monotonic()
                        with (case / 'stdout.jsonl').open('w') as stdout, (case / 'stderr.log').open('w') as stderr:
                            if group.get('fault') == 'panic-reset':
                                from fault import run_crash
                                code = run_crash(cmd, build, case, stdout, stderr, args.gdb)
                            else:
                                code = subprocess.run(cmd, stdout=stdout, stderr=stderr).returncode
                        entry = {'name': name, 'machine': machine, 'orientation': orientation, 'dark': dark,
                                 'group': group['id'], 'command': cmd, 'exit_code': code,
                                 'host_seconds': round(time.monotonic() - tick, 3), 'screens': []}
                        meta['runs'].append(entry)
                        if code:
                            raise RuntimeError(f'{name} failed: see {case}')
                        entry['screens'] = summarize(case / 'run', group, machine)
                        entry['input_idle_timeouts'] = [r for r in records(case / 'run/run.jsonl')
                                                        if r.get('input_quiet_reached') is False]
                        entry['input_sha256'] = {str(path.relative_to(case)): sha(path)
                                                 for folder in [config, case / 'assets']
                                                 for path in sorted(folder.rglob('*')) if path.is_file()}
                        entry['script_sha256'] = sha(script)
                        for screen in entry['screens']:
                            if screen['activity_observed'] != screen['activity_expected']:
                                raise RuntimeError(f'{name}/{screen["id"]}: expected {screen["activity_expected"]}, '
                                                   f'observed {screen["activity_observed"]}')
                            if machine == 'x3' and len(screen['controller_refreshes']) != screen['panel_refreshes']:
                                raise RuntimeError(f'{name}/{screen["id"]}: incomplete controller trace')
                        save(out / 'manifest.json', meta)
        meta['status'] = 'passed'
    except BaseException as error:
        meta['status'] = 'failed'
        meta['error'] = str(error)
        raise
    finally:
        meta['host_seconds'] = round(time.monotonic() - started, 3)
        save(out / 'manifest.json', meta)


if __name__ == '__main__':
    main()
