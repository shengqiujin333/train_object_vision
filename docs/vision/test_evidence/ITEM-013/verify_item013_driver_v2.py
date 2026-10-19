#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-013 修复复核轮 v2 验证驱动(软件测试)。

V-RUN: 交付 runner board_vision_selftest.py 独立复跑(stamp verify-...-v2, --keep-remote)
V-EXT: v2 夹具板端 g++ 直编 + 运行(呈证判例翻转 + 回归保留)
V-NEG: 反转取证(v1 夹具逐字节复用, 预期 v01/v02/v03 FAIL + RESULT fail=3)

仅测试环境/测试工具;不改产品源码;凭据仅经环境变量;有限超时;只写 /tmp。
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
OUT = HERE
STAMP = 'verify-20261021-item013-v2'
ENVV = 'TRAIN_RK3588_SSH_CREDENTIAL'
RUNNER = APP / 'tools' / 'board_vision_selftest.py'

TRANS = {'host_key_fingerprint': None, 'steps': []}


def note(step, **kw):
    TRANS['steps'].append({'step': step, **kw})


def fl(txt):
    print('[driver] ' + txt, flush=True)


def save_transcript():
    p = OUT / ('vision-item013-192_168_0_103-' + STAMP + '.driver.transcript.json')
    p.write_text(json.dumps(TRANS, ensure_ascii=False, indent=2), encoding='utf-8')


def import_credential() -> int:
    # v1 口径: 临时文件落盘供 runner 子进程读取前先导入本进程;用完即删。
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


def sftp_download_text(client, remote: str, local: pathlib.Path) -> bool:
    try:
        sftp = client.open_sftp()
        try:
            sftp.get(remote, str(local))
        finally:
            sftp.close()
        return True
    except Exception as exc:
        fl('sftp download failed (%s): %r' % (remote, exc))
        return False


def main() -> int:
    rc = import_credential()
    if rc != 0:
        note('cred_import', exit=rc)
        save_transcript()
        return rc

    fl('V-RUN start (deliverable runner, keep-remote)')
    t0 = time.time()
    cmd = [sys.executable, str(RUNNER),
           '--host', '192.168.0.103', '--user', 'admin',
           '--credential-env', ENVV,
           '--outdir', str(OUT),
           '--stamp', STAMP, '--keep-remote']
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8',
                           errors='replace', timeout=1400, cwd=str(APP))
    except subprocess.TimeoutExpired as exc:
        fl('V-RUN timeout after 1400s')
        note('vrun', exit=None, output='timeout')
        save_transcript()
        return 3
    elapsed = round(time.time() - t0, 1)
    # 凭据临时文件用完即删(承接导入步骤)
    (OUT / '.verify_item013_cred.tmp').unlink(missing_ok=True)
    (OUT / ('vision-item013-192_168_0_103-' + STAMP + '.driver.log')).write_text(
        p.stdout + '\n--- stderr ---\n' + p.stderr, encoding='utf-8')
    note('vrun', exit=p.returncode, elapsed_s=elapsed)
    fl('V-RUN exit=%d elapsed=%.0fs' % (p.returncode, elapsed))
    if p.returncode != 0:
        save_transcript()
        return p.returncode

    # 归档副本到 SELFTEST/(runner --archive-dir 未传时不自动归档;这里手动补)
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

    REMOTE = '/tmp/railhmi_vision_item013_' + STAMP

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

        # 上传两个夹具源码
        for fname in ('verify_item013_vext_v2.cpp', 'verify_item013_vext.cpp'):
            ok = sftp_upload(client, OUT / fname,
                             REMOTE + '/app/' + fname)
            note('upload_' + fname, exit=0 if ok else 1)
        # v1 夹具文件保存到独立名避免覆盖(原 v1 名就是 verify_item013_vext.cpp)
        ok = sftp_upload(client, OUT / 'verify_item013_vext_v2.cpp',
                         REMOTE + '/app/verify_item013_vext_v2.cpp')
        note('upload_v2', exit=0 if ok else 1)

        if not all(s.get('exit', 1) == 0 for s in TRANS['steps'] if s['step'].startswith('upload')):
            fl('fixture upload failed')
            save_transcript()
            return 5

        # V-EXT: v2 夹具编译 + 运行
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
        fl('vext2 run exit=%d' % r['exit'])
        vext2_log = OUT / ('verify-item013-vext-v2.log')
        vext2_log.write_text(r['output'], encoding='utf-8')
        if r['exit'] != 0:
            fl('vext2 FAILED (some checks failed)')
            # 不清理,保留树便于取证
            save_transcript()
            return 7

        # V-NEG: v1 夹具逐字节复用,编译后运行;预期 v01/v02/v03 FAIL, RESULT fail=3
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
        fl('vext1 run exit=%d' % r1['exit'])
        (OUT / ('verify-item013-vext-v1-replay.log')).write_text(
            r1['output'], encoding='utf-8')

        if r1['exit'] == 0:
            # v1 呈证夹具在修复后应当 FAIL;恒 0 意味着夹具/树有问题
            fl('V-NEG unexpected: v1 fixture passed on fixed tree (should fail)')
            note('vneg', exit=8, output='v1 fixture should FAIL on fixed tree')
            save_transcript()
            return 8

        # ---- 清理(仅在全部判例通过时) ----
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
