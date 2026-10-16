# -*- coding: utf-8 -*-
"""ITEM-024 final cleanup: remove the kept remote build tree /tmp/railhmi_build.

The v1 fixtures round kept it for the v2 ext re-run; that re-run is complete.
Finite timeouts; no credentials logged.
"""
import base64
import hashlib
import os
import sys

import paramiko

EXPECTED_FP = "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU"

secret = os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL")
if not secret:
    sys.stderr.write("credential env not loaded\n")
    sys.exit(2)

c = paramiko.SSHClient()
c.load_system_host_keys()
c.set_missing_host_key_policy(paramiko.RejectPolicy())
c.connect("192.168.0.103", username="admin", password=secret, timeout=10,
          auth_timeout=10, banner_timeout=10, look_for_keys=False, allow_agent=False)
fp = "SHA256:" + base64.b64encode(
    hashlib.sha256(c.get_transport().get_remote_server_key().asbytes()).digest()
).decode().rstrip("=")
if fp != EXPECTED_FP:
    sys.stderr.write("fingerprint mismatch\n")
    c.close()
    sys.exit(2)
_in, out, err = c.exec_command(
    "rm -rf /tmp/railhmi_build && echo cleaned && ls -d /tmp/railhmi_build 2>&1", timeout=60)
print(out.read().decode(), end="")
print(err.read().decode(), end="")
c.close()
