# onvif_test.py — ONVIF probe/info/reboot for thermal camera .64 (stdlib only)
# usage: python3 onvif_test.py probe | info | reboot
import base64, hashlib, os, sys, time, urllib.request, urllib.error

HOST = '192.168.1.64'
USER = 'admin'
PWD  = '123456'
EPFILE = '/tmp/onvif_endpoint.txt'

CANDIDATES = [
    (80,   '/onvif/device_service'),
    (8899, '/onvif/device_service'),
    (8000, '/onvif/device_service'),
    (8080, '/onvif/device_service'),
    (80,   '/onvif/device_mngt'),
]

def call(action, body, port, path, auth=True, plaintext=False):
    headers = ''
    if auth:
        nonce = os.urandom(16)
        created = time.strftime('%Y-%m-%dT%H:%M:%S.000Z', time.gmtime())
        if plaintext:
            pw = PWD
        else:
            pw = base64.b64encode(hashlib.sha1(
                nonce + created.encode() + PWD.encode()).digest()).decode()
        nb64 = base64.b64encode(nonce).decode()
        ptype = ('http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordText'
                 if plaintext else
                 'http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordDigest')
        headers = (
            '<s:Header>'
            '<Security s:mustUnderstand="1" xmlns="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd">'
            '<UsernameToken><Username>%s</Username><Password Type="%s">%s</Password>'
            '<Nonce>%s</Nonce><Created>%s</Created></UsernameToken></Security></s:Header>'
        ) % (USER, ptype, pw, nb64, created)
    env = ('<?xml version="1.0" encoding="UTF-8"?>'
           '<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">%s'
           '<s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns:xsd="http://www.w3.org/2001/XMLSchema">%s</s:Body>'
           '</s:Envelope>') % (headers, body)
    url = 'http://%s:%d%s' % (HOST, port, path)
    req = urllib.request.Request(url, data=env.encode('utf-8'), headers={
        'Content-Type': 'application/soap+xml; charset=utf-8; action="%s"' % action})
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, r.read().decode('utf-8', 'replace')
    except urllib.error.HTTPError as e:
        try:
            return e.code, e.read().decode('utf-8', 'replace')
        except Exception:
            return e.code, ''
    except Exception as e:
        return None, '%s: %s' % (type(e).__name__, e)

def load_ep():
    port, path = open(EPFILE).read().split()
    return int(port), path

cmd = sys.argv[1]
TDS = 'http://www.onvif.org/ver10/device/wsdl'

if cmd == 'probe':
    for port, path in CANDIDATES:
        st, txt = call(TDS + '/GetSystemDateAndTime',
                       '<tds:GetSystemDateAndTime xmlns:tds="%s"/>' % TDS,
                       port, path, auth=False)
        print('probe %s:%s -> %s %s' % (port, path, st, txt[:150].replace('\n', ' ')), flush=True)
        if st == 200:
            open(EPFILE, 'w').write('%d %s' % (port, path))
            print('OK endpoint saved', flush=True)
            break
elif cmd == 'info':
    port, path = load_ep()
    st, txt = call(TDS + '/GetDeviceInformation',
                   '<tds:GetDeviceInformation xmlns:tds="%s"/>' % TDS, port, path)
    print('info -> %s' % st, flush=True)
    print(txt[:1500], flush=True)
    if st != 200:
        st2, txt2 = call(TDS + '/GetDeviceInformation',
                         '<tds:GetDeviceInformation xmlns:tds="%s"/>' % TDS,
                         port, path, plaintext=True)
        print('info(plaintext) -> %s' % st2, flush=True)
        print(txt2[:1500], flush=True)
elif cmd == 'reboot':
    port, path = load_ep()
    st, txt = call(TDS + '/SystemReboot',
                   '<tds:SystemReboot xmlns:tds="%s"/>' % TDS, port, path)
    print('reboot -> %s' % st, flush=True)
    print(txt[:800], flush=True)
