#!/usr/bin/env python3
"""Isolated Linux-container test of the real static MIPS SSH server.

Requires qemu-mipsel, python3-paramiko and BusyBox mkpasswd. Run only inside a
disposable container, with C1_SSH_TEST_CONFIRM=1. Never uses a device or LAN.
"""
import io
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time

import paramiko
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

assert Path('/.dockerenv').is_file() and os.environ.get('C1_SSH_TEST_CONFIRM') == '1'
root = Path(__file__).resolve().parents[2]
bins = root / 'linux-tools/.build/linux-tools/bin'
controller = root / 'terminal/assets/bin/sshd'
auth = Path('/storage/terminal/dropbear')
helper = Path('/usr/bin/cryptpw')
assert not auth.exists() and not helper.exists(), 'test needs a fresh disposable container'
auth.mkdir(parents=True, mode=0o700)
# BusyBox calls the same SHA-crypt applet mkpasswd on Debian and cryptpw on C1Max.
helper.write_text('#!/bin/sh\nexec /bin/busybox mkpasswd "$@"\n')
helper.chmod(0o755)
password = 'Review-only-test-42!'
hashfile = auth / 'password.hash'

with tempfile.TemporaryDirectory(prefix='c1max-ssh-', dir='/root') as tmp:
    directory = Path(tmp)
    release = directory / 'release'
    release.mkdir()
    (release / 'linux-tools').symlink_to(bins.parent)
    env = dict(os.environ, C1_APPS_ROOT=str(release), C1_APPS_DATA=str(directory / 'data'))
    def control(*args, stdin=None, ok=True):
        result = subprocess.run(['sh', str(controller), *args], input=stdin,
                                text=True, capture_output=True, env=env, timeout=15)
        assert (result.returncode == 0) == ok, result.stderr + result.stdout
        return result.stdout + result.stderr

    # A fresh installation must be recoverable even before Settings has been
    # opened. The MIPS Dropbear binary cannot execute directly in this x86
    # test container, so start reaches the keygen boundary and leaves the
    # generated default hash for the later real-server checks.
    assert 'host-key generation failed' in control('start', ok=False)
    assert hashfile.stat().st_mode & 0o777 == 0o600
    default_hash = hashfile.read_bytes()
    assert b'c1max' not in default_hash
    control('password', stdin=password + '\n' + password + '\n')
    assert hashfile.stat().st_mode & 0o777 == 0o600
    good_hash = hashfile.read_bytes()
    assert password.encode() not in good_hash
    control('password', stdin='first\nsecond\n', ok=False)
    assert hashfile.read_bytes() == good_hash
    control('port', '80', ok=False)
    control('port', '2229')
    assert 'port=2229\n' in control('status', '--machine')

    private = Ed25519PrivateKey.generate()
    public = private.public_key().public_bytes(serialization.Encoding.OpenSSH,
                                               serialization.PublicFormat.OpenSSH)
    keyfile = directory / 'client.pub'
    keyfile.write_bytes(public + b' review-test\n')
    control('authorize', str(keyfile))
    control('authorize', str(keyfile))
    assert (auth / 'authorized_keys').read_bytes() == public + b'\n'
    assert 'keys=1\n' in control('status', '--machine')
    pem = private.private_bytes(serialization.Encoding.PEM,
                                serialization.PrivateFormat.OpenSSH,
                                serialization.NoEncryption())
    client_key = paramiko.Ed25519Key.from_private_key(io.StringIO(pem.decode()))
    hostkey = directory / 'hostkey'
    subprocess.run(['qemu-mipsel', str(bins / 'dropbearkey'), '-t', 'ed25519', '-f', str(hostkey)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    log = (directory / 'server.log').open('w+')
    server = subprocess.Popen(['qemu-mipsel', str(bins / 'dropbear'), '-F', '-D', str(auth),
                               '-r', str(hostkey), '-p', '127.0.0.1:' + str(port),
                               '-P', str(directory / 'server.pid')], stdout=log, stderr=log)
    try:
        for _ in range(100):
            try:
                sock = socket.create_connection(('127.0.0.1', port), timeout=.1)
                sock.close()
                break
            except OSError:
                assert server.poll() is None, 'server exited'
                time.sleep(.05)
        else:
            raise AssertionError('server did not bind')

        def login(value=None, key=None, expected=True):
            transport = paramiko.Transport(('127.0.0.1', port))
            transport.auth_timeout = 10
            try:
                transport.start_client(timeout=10)
                # Pin the generated server host key; no blanket trust policy.
                hostpub = subprocess.check_output(['qemu-mipsel', str(bins / 'dropbearkey'),
                                                  '-y', '-f', str(hostkey)], text=True)
                assert transport.get_remote_server_key().get_base64() in hostpub
                try:
                    if key is None: transport.auth_password('root', value, fallback=False)
                    else: transport.auth_publickey('root', key)
                    accepted = transport.is_authenticated()
                except paramiko.AuthenticationException:
                    accepted = False
                assert accepted == expected, 'unexpected authentication result'
                if accepted:
                    channel = transport.open_session(timeout=10)
                    channel.exec_command('printf C1MAX_SSH_OK')
                    assert channel.makefile('rb').read() == b'C1MAX_SSH_OK'
                    assert channel.recv_exit_status() == 0
                    channel.close()
            finally:
                transport.close()

        login(password)
        login('wrong', expected=False)
        login(password + '\nignored', expected=False)
        login(password + '\x00ignored', expected=False)
        hashfile.unlink()
        login(password, expected=False)
        login(key=client_key)  # public key still works without any password
        hashfile.write_bytes(good_hash)
        hashfile.chmod(0o644)
        login(password, expected=False)
        hashfile.unlink()
        saved = directory / 'hash'
        saved.write_bytes(good_hash)
        saved.chmod(0o600)
        hashfile.symlink_to(saved)
        login(password, expected=False)
        hashfile.unlink()
        print('PASS SSH: explicit credentials, first/duplicate key import, password and public-key login,')
        print('wrong/missing/insecure/symlink hash and newline/NUL rejection; real MIPS loopback server')
    except Exception:
        log.flush()
        log.seek(0)
        print(log.read())
        raise
    finally:
        server.terminate()
        try: server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
        log.close()
        shutil.rmtree('/storage/terminal')
        helper.unlink()
