"""Keep reproducible local boot attempts; never equate process exit 0 with a boot."""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    return hashlib.file_digest(path.open('rb'), 'sha256').hexdigest() if path.is_file() else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', choices=['Debug', 'Release'], default='Release')
    parser.add_argument('--timeout-ms', type=int, default=5000)
    parser.add_argument('--label', default='boot')
    args = parser.parse_args()
    if not 100 <= args.timeout_ms <= 120000:
        parser.error('timeout must be 100..120000 ms')
    label = re.sub(r'[^A-Za-z0-9_-]', '_', args.label)[:60]
    stamp = dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    folder = ROOT / 'out' / 'evidence' / f'{stamp}_{label}'
    folder.mkdir(parents=True, exist_ok=False)
    exe = ROOT / 'out' / args.configuration.lower() / 'launcher.exe'
    env = os.environ.copy()
    env['PATH'] = r'C:\msys64\clang64\bin;' + env['PATH']
    command = [str(exe), '--headless', '--boot', f'--timeout={args.timeout_ms}']
    def git(*cmd):
        return subprocess.run(['rtk', 'proxy', 'git', *cmd], cwd=ROOT, capture_output=True,
                              encoding='utf-8', errors='replace').stdout.strip()
    diff = git('diff', '--binary', 'HEAD')
    (folder / 'working-tree.patch').write_text(diff, encoding='utf-8')
    evidence = {'utc': stamp, 'label': args.label, 'command': command, 'cwd': str(ROOT),
                'head': git('rev-parse', 'HEAD'), 'status': git('status', '--short'),
                'diff_sha256': hashlib.sha256(diff.encode()).hexdigest(),
                'configuration': args.configuration,
                'executable_sha256': sha256(exe),
                'game_sha256': sha256(ROOT / 'CUSA01130' / 'eboot.bin'),
                'sfo_sha256': sha256(ROOT / 'CUSA01130' / 'sce_sys' / 'param.sfo'),
                'settings_sha256': sha256(ROOT / 'gravity_settings.ini')}
    # Include hashes for untracked sources too; git diff does not contain them.
    evidence['sources'] = {str(p.relative_to(ROOT)): sha256(p)
        for base in ['engine', 'tests', 'tools'] for p in sorted((ROOT / base).rglob('*'))
        if p.is_file() and p.suffix in {'.cpp', '.h', '.inc', '.py', '.ps1', '.vert', '.frag'}}
    evidence['cmake_sha256'] = sha256(ROOT / 'CMakeLists.txt')
    from generate_manifest import parse_param_sfo
    evidence['game_metadata'] = parse_param_sfo(ROOT / 'CUSA01130/sce_sys/param.sfo')
    try:
        result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True,
                                timeout=args.timeout_ms / 1000 + 20)
        output = result.stdout.decode('utf-8', errors='replace')
        errors = result.stderr.decode('utf-8', errors='replace')
        evidence['exit_code'] = result.returncode
    except subprocess.TimeoutExpired as error:
        output = (error.stdout or b'').decode('utf-8', errors='replace')
        errors = (error.stderr or b'').decode('utf-8', errors='replace')
        evidence['exit_code'] = None
        evidence['harness_timeout'] = True
    (folder / 'stdout.log').write_text(output, encoding='utf-8')
    (folder / 'stderr.log').write_text(errors, encoding='utf-8')
    combined = output + '\n' + errors
    evidence['execution_status'] = (re.search(r'Status:\s*(\w+)', combined) or [None, 'UNKNOWN'])[1]
    trap = re.search(r'NID:\s*(\S+)', combined)
    if trap:
        nid = trap[1]
        inventory = json.loads((ROOT / 'docs/evidence/imports.json').read_text(encoding='utf-8'))
        item = next((x for x in inventory['imports'] if x['nid'] == nid), {})
        evidence['blocker'] = {'nid': nid, 'symbol': item.get('verified_symbol'),
                               'library': item.get('library')}
        evidence['outcome'] = 'unresolved_import'
    elif evidence.get('harness_timeout'):
        evidence['outcome'] = 'harness_timeout'
    elif evidence['execution_status'] == 'SUCCESS':
        evidence['outcome'] = 'guest_returned'  # Does not imply a playable game.
    elif 'Crash Reason: Timeout' in combined:
        evidence['outcome'] = 'guest_timeout'
    else:
        evidence['outcome'] = 'failed_or_timed_out'
    (folder / 'result.json').write_text(json.dumps(evidence, indent=2, ensure_ascii=False), encoding='utf-8')
    print(json.dumps({'evidence': str(folder), 'outcome': evidence['outcome'],
                      'status': evidence['execution_status'], 'exit_code': evidence['exit_code'],
                      'blocker': evidence.get('blocker'), 'stderr': errors[-2000:],
                      'report': output[-3500:]}, ensure_ascii=False, indent=2))
    return 0 if evidence['outcome'] == 'guest_returned' else 1


if __name__ == '__main__':
    raise SystemExit(main())
