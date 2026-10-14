#!/usr/bin/env python
"""Quick SSH command runner for RK3588 (192.168.0.103 admin/admin).

Usage: python _rk3588.py "command to run"
"""
import sys
import paramiko

HOST = '192.168.0.103'
PORT = 22
USER = 'admin'
PASSWORD = 'admin'


def run(cmd, timeout=120):
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, port=PORT, username=USER, password=PASSWORD, timeout=10)
    try:
        stdin, stdout, stderr = client.exec_command(cmd, timeout=timeout, get_pty=False)
        out = stdout.read().decode('utf-8', errors='replace')
        err = stderr.read().decode('utf-8', errors='replace')
        exit_code = stdout.channel.recv_exit_status()
        if out:
            sys.stdout.write(out)
            if not out.endswith('\n'):
                sys.stdout.write('\n')
        if err:
            sys.stderr.write(err)
        sys.stdout.flush()
        return exit_code
    finally:
        client.close()


if __name__ == '__main__':
    if len(sys.argv) >= 3 and sys.argv[1] == '--get':
        remote, local = sys.argv[2], sys.argv[3]
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        client.connect(HOST, port=PORT, username=USER, password=PASSWORD, timeout=10)
        try:
            sftp = client.open_sftp()
            sftp.get(remote, local)
            st = sftp.stat(remote)
            print(f'[DOWN] {remote} -> {local} ({st.st_size} bytes)')
            sftp.close()
        finally:
            client.close()
        sys.exit(0)
    if len(sys.argv) >= 3 and sys.argv[1] == '--put':
        local, remote = sys.argv[2], sys.argv[3]
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        client.connect(HOST, port=PORT, username=USER, password=PASSWORD, timeout=10)
        try:
            sftp = client.open_sftp()
            # Ensure remote dir exists
            remote_dir = '/'.join(remote.split('/')[:-1])
            if remote_dir:
                cur = ''
                for p in remote_dir.split('/'):
                    cur = f'{cur}/{p}' if cur else f'/{p}'
                    try:
                        sftp.stat(cur)
                    except IOError:
                        try:
                            sftp.mkdir(cur)
                        except IOError:
                            pass
            sftp.put(local, remote)
            print(f'[UP] {local} -> {remote}')
            sftp.close()
        finally:
            client.close()
        sys.exit(0)
    cmd = ' '.join(sys.argv[1:])
    sys.exit(run(cmd))
