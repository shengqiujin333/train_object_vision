#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""N023 整体交付复验驱动(software_tester, 视觉线 ITEM-013)。

V-RUN: 交付 runner board_vision_selftest.py 独立复跑(stamp verify-20261020-n023-v1, --keep-remote)
V-EXT: v2 夹具逐字节复用板端 g++ 直编 + 运行(预期 18/18 全绿)
V-NEG: v1 夹具逐字节复用(预期 fail=3 反转取证, VEXT1_RUN_EXIT=1)
清理: 仅全部判据通过后按名清理保留树。

教训应用: 退出码一律从 *_EXIT token 解析, 不用 rrun 外层 exit(v2 驱动 vneg exit=8 缺陷不复犯);
夹具上传驱动内文件名引用与泄漏扫描自排除名同步; 凭据临时文件用完即删。
"""
import json
import os
import pathlib
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[3]
APP = REPO / 'app'
SRC = REPO / 'docs' / 'vision' / 'test_evidence' / 'ITEM-013'
OUT = HERE
STAMP = 'verify-20261020-n023-v1'
ENVV = 'TRAIN_RK3588_SSH_CREDENTIAL'
RUNNER = APP / 'tools' / 'board_vision_selftest.py'
REMOTE = '/tmp/railhmi_vision_item013_' + STAMP

TRANS = {'host_key_fingerprint': None, 'steps': []}


def note(step, **kw):
    TRANS['steps'].append({'step': step, **kw})


def fl(txt):
    print('[driver] ' + txt, flush=True)


def save_transcript():
    p = OUT / ('vision-item013-192_168_0_103-' + STAMP + '.driver.transcript.json')
    p.write_text(json.dumps(TRANS, ensure_ascii=False, indent=2), encoding='utf-8')


def import_credential() -> int:
    ps = subprocess.run(
        ['powershell.exe', '-NoProfile', '-Command',
         '[Environment]::GetEnvironmentVariable("%s","User")' % ENVV],
        capture_output=True, text=True, timeout=20)
    v = ps.stdout.strip()
    if not v:
        fl('credential import failed: empty')
        return 2
    os.environ[ENVV] = v
    tmp = OUT / '.verify_item013_cred.tmp'
    tmp.write_text(v, encoding='utf-8')
    note('cred_import', exit=0, output='imported len=%d' % len(v))
    fl('credential imported (len %d)' % len(v))
    return 0


def token_exit(output: str, token: str):
    for line in reversed(output.splitlines()):
        s = line.strip()
        if s == token:
            return None
        if s.startswith(token):
            try:
                return int(s[len(token):].split()[0].strip())
            except ValueError:
                return None
    return None


def sftp_upload(client, local: pathlib.Path, remote: str) -> bool:
    try:
        sftp = client.open_sftp()
        try:
            sftp.put(str(local), remote)
        finally:
            sftp.close()
        return True
    except Exception as exc:
        fl('sftp upload failed: %r' % (exc,))
        return False


def main() -> int:
    rc = import_credential()
    if rc != 0:
        note('cred_import', exit=rc)
        save_transcript()
        return rc

    fl('V-RUN start (deliverable runner, keep-remote, overall re-run on current tree)')
    t0 = time.time()
    cmd = [sys.executable, str(RUNNER),
           '--host', '192.168.0.103', '--user', 'admin',
           '--credential-env', ENVV,
           '--outdir', str(OUT),
           '--stamp', STAMP, '--keep-remote']
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8',
                           errors='replace', timeout=1400, cwd=str(APP))
    except subprocess.TimeoutExpired:
        fl('V-RUN timeout after 1400s')
        note('vrun', exit=None, output='timeout')
        save_transcript()
        return 3
    elapsed = round(time.time() - t0, 1)
    (OUT / '.verify_item013_cred.tmp').unlink(missing_ok=True)
    (OUT / ('vision-item013-192_168_0_103-' + STAMP + '.driver.log')).write_text(
        p.stdout + '\n--- stderr ---\n' + p.stderr, encoding='utf-8')
    note('vrun', exit=p.returncode, elapsed_s=elapsed)
    fl('V-RUN exit=%d elapsed=%.0fs' % (p.returncode, elapsed))
    if p.returncode != 0:
        save_transcript()
        return p.returncode

    # 关键步骤 *_EXIT token 独立核验(不采信 runner 外层 exit=0)
    main_log = OUT / ('vision-item013-192_168_0_103-' + STAMP + '.log')
    mlog = main_log.read_text(encoding='utf-8', errors='replace')
    tok_ok = True
    for tok in ('BUILD_CORE_EXIT', 'BUILD_MEDIA_EXIT', 'TEST_EXIT', 'DET1_EXIT',
                'DET2_EXIT', 'SELF_EXIT', 'LIST_EXIT', 'MEDIA_SELFTEST_EXIT'):
        v = token_exit(mlog, tok + '=')
        note('token_' + tok, exit=v if v is not None else 9, output=tok)
        if v != 0:
            fl('token %s != 0 (got %r)' % (tok, v))
            tok_ok = False
    if not tok_ok:
        save_transcript()
        return 9

    # 归档副本到 SELFTEST/(driver 手动补, v2 口径)
    src = OUT / ('vision-item013-192_168_0_103-' + STAMP +
                 '.app_core-evidence_item013-selftest-' + STAMP + '.json')
    src_det = OUT / ('vision-item013-192_168_0_103-' + STAMP +
                     '.app_core-evidence_item013-selftest-det1-' + STAMP + '.json')
    if src.is_file() and src_det.is_file():
        adir = HERE.parents[0] / 'SELFTEST'
        adir.mkdir(parents=True, exist_ok=True)
        (adir / ('vision-selftest-192_168_0_103-' + STAMP + '.json')).write_bytes(src.read_bytes())
        (adir / ('vision-selftest-192_168_0_103-' + STAMP + '.deterministic.json')).write_bytes(
            src_det.read_bytes())
        note('archive', exit=0, output='SELFTEST copies written')
    else:
        note('archive', exit=None, output='selftest json missing')
        fl('V-RUN products missing; abort before V-EXT')
        save_transcript()
        return 4

    # ---- V-EXT / V-NEG via paramiko ----
    import paramiko
    client = paramiko.SSHClient()
    client.load_system_host_keys()
    secret = os.environ[ENVV]
    try:
        client.connect(hostname='192.168.0.103', port=22, username='admin', password=secret,
                       timeout=10, auth_timeout=10, banner_timeout=10,
                       look_for_keys=False, allow_agent=False)
    except Exception as exc:
        fl('board connect failed: %r' % (exc,))
        note('connect', exit=2, output=repr(exc))
        save_transcript()
        return 2
    tr = client.get_transport()
    if tr is not None and tr.get_remote_server_key() is not None:
        import base64
        import hashlib
        TRANS['host_key_fingerprint'] = 'SHA256:' + base64.b64encode(
            hashlib.sha256(tr.get_remote_server_key().asbytes()).digest()).decode().rstrip('=')
    fl('connected, fingerprint=' + str(TRANS['host_key_fingerprint']))

    def rrun(cmd, timeout):
        t1 = time.time()
        stdin, out, err = client.exec_command(cmd, timeout=timeout)
        so = out.read().decode('utf-8', 'replace')
        se = err.read().decode('utf-8', 'replace')
        rc2 = out.channel.recv_exit_status()
        return {'cmd': cmd, 'exit': rc2, 'elapsed_s': round(time.time() - t1, 1),
                'output': so + se}

    try:
        note('rrun_env', **rrun('uname -smr; g++ --version | head -1', 15))

        for fname in ('verify_item013_vext_v2.cpp', 'verify_item013_vext.cpp'):
            ok = sftp_upload(client, SRC / fname, REMOTE + '/app/' + fname)
            note('upload_' + fname, exit=0 if ok else 1)
        if not all(s.get('exit', 1) == 0 for s in TRANS['steps'] if s['step'].startswith('upload')):
            fl('fixture upload failed')
            save_transcript()
            return 5

        # V-EXT: v2 夹具(逐字节复用)编译 + 运行; 退出码从 VEXT_RUN_EXIT token 解析
        gx = ('cd %s/app && g++ -std=c++17 -I src/core/include '
              'verify_item013_vext_v2.cpp build-core/src/core/librailhmi_core.a '
              '-o vext2; echo VEXT_BUILD_EXIT=$?' % REMOTE)
        b = rrun(gx, 120)
        note('vext2_build', **b)
        fl('vext2 build exit=%d' % b['exit'])
        if b['exit'] != 0:
            save_transcript()
            return 6

        r = rrun('cd %s/app && ./vext2; echo VEXT_RUN_EXIT=$?' % REMOTE, 120)
        note('vext2_run', **r)
        vext_run_exit = token_exit(r['output'], 'VEXT_RUN_EXIT=')
        fl('vext2 run outer=%d token_exit=%r' % (r['exit'], vext_run_exit))
        (OUT / 'verify-item013-n023-vext-v2.log').write_text(r['output'], encoding='utf-8')
        if vext_run_exit != 0:
            fl('V-EXT FAILED (some checks failed); keep tree for forensics')
            save_transcript()
            return 7

        # V-NEG: v1 夹具逐字节复用; 预期 fail=3, VEXT1_RUN_EXIT=1
        gx1 = ('cd %s/app && g++ -std=c++17 -I src/core/include '
               'verify_item013_vext.cpp build-core/src/core/librailhmi_core.a '
               '-o vext1; echo VEXT1_BUILD_EXIT=$?' % REMOTE)
        b1 = rrun(gx1, 120)
        note('vext1_build', **b1)
        fl('vext1 build exit=%d' % b1['exit'])
        if b1['exit'] != 0:
            save_transcript()
            return 6
        r1 = rrun('cd %s/app && ./vext1; echo VEXT1_RUN_EXIT=$?' % REMOTE, 120)
        note('vext1_run', **r1)
        vneg_exit = token_exit(r1['output'], 'VEXT1_RUN_EXIT=')
        fl('vext1 run outer=%d token_exit=%r' % (r1['exit'], vneg_exit))
        (OUT / 'verify-item013-n023-vext-v1-replay.log').write_text(r1['output'], encoding='utf-8')

        fails = [l for l in r1['output'].splitlines() if l.startswith('vext FAIL ')]
        result_line = [l for l in r1['output'].splitlines() if l.startswith('vext RESULT ')]
        vneg_ok = (vneg_exit == 1 and len(fails) == 3
                   and result_line and result_line[0] == 'vext RESULT pass=13 fail=3'
                   and any('v01' in f for f in fails) and any('v02' in f for f in fails)
                   and any('v03' in f for f in fails))
        if not vneg_ok:
            fl('V-NEG unexpected: v1 fixture did not fail exactly as constructed '
               '(token_exit=%r fails=%d result=%s)' % (vneg_exit, len(fails),
                                                       result_line[0] if result_line else '<none>'))
            note('vneg', exit=8, output='v1 fixture reversal mismatch')
            save_transcript()
            return 8
        note('vneg', exit=0, output='reversal confirmed: fail=3 (v01/v02/v03)')

        # ---- 清理(仅全部判据通过后) ----
        c = rrun('rm -rf %s && echo ITEM013_BOARD_CLEANED' % REMOTE, 30)
        note('cleanup', **c)
        fl('cleanup exit=%d' % c['exit'])
        if 'ITEM013_BOARD_CLEANED' not in c['output']:
            fl('cleanup confirm missing; keep note')
    finally:
        client.close()

    save_transcript()
    fl('driver done')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
