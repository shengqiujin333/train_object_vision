import os
import sys

import paramiko

sys.stdout.reconfigure(encoding="utf-8")

cred = os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL", "")
client = paramiko.SSHClient()
client.load_system_host_keys()
keyfile = os.path.expanduser("~/.ssh/known_hosts")
if os.path.exists(keyfile):
    client.load_host_keys(keyfile)
client.set_missing_host_key_policy(paramiko.RejectPolicy())
try:
    client.connect("192.168.0.103", port=22, username="admin", password=cred,
                   timeout=10, auth_timeout=10, banner_timeout=10)
except Exception as exc:
    print("CONNECT_FAIL %r" % exc)
    sys.exit(1)
cmds = [
    "dpkg -l 'qtdeclarative5-dev' 'qtbase5-dev' 2>/dev/null | tail -n 5",
    "ls /usr/lib/aarch64-linux-gnu/qt5/qml/QtQuick.2 2>/dev/null | head -5; echo QMLDIR_LS_EXIT=$?",
    "g++ -print-file-name=libQt5Qml.so 2>/dev/null",
]
try:
    for c in cmds:
        stdin, stdout, stderr = client.exec_command(c, timeout=30)
        out = stdout.read().decode("utf-8", "replace")
        err = stderr.read().decode("utf-8", "replace")
        code = stdout.channel.recv_exit_status()
        print("CMD %r exit=%d" % (c, code))
        print(out.strip())
        if err.strip():
            print("stderr:", err.strip()[:200])
finally:
    client.close()
