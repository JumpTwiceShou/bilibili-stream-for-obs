"""Run loopback OpenVPN TCP/UDP tests with ephemeral in-memory TLS material.

Requires Python cryptography. Usage: python tests/run_openvpn_network_tests.py TEST_EXE CORE_EXE
No user profiles, credentials, system certificates or network routes are changed.
"""
import datetime
import json
import subprocess
import sys

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID, ObjectIdentifier

key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "local-openvpn-test")])
now = datetime.datetime.now(datetime.timezone.utc)
legacy = '--legacy' in sys.argv[3:]
builder = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
        .serial_number(x509.random_serial_number()).not_valid_before(now - datetime.timedelta(minutes=1))
        .not_valid_after(now + datetime.timedelta(hours=1))
        .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True))
if legacy:
    builder = builder.add_extension(x509.UnrecognizedExtension(
        ObjectIdentifier('2.16.840.1.113730.1.1'), bytes.fromhex('03020640')), critical=False)
else:
    builder = (builder.add_extension(x509.KeyUsage(True, False, True, False, False, True, True, False, False), critical=True)
               .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False))
cert = builder.sign(key, hashes.SHA256())
untrusted_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
untrusted_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'untrusted-test-ca')])
untrusted_ca = (x509.CertificateBuilder().subject_name(untrusted_name).issuer_name(untrusted_name)
                .public_key(untrusted_key.public_key()).serial_number(x509.random_serial_number())
                .not_valid_before(now - datetime.timedelta(minutes=1)).not_valid_after(now + datetime.timedelta(hours=1))
                .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
                .sign(untrusted_key, hashes.SHA256()))
fixture = {"legacy": legacy, "untrusted_ca": untrusted_ca.public_bytes(serialization.Encoding.PEM).decode(),
           "certificate": cert.public_bytes(serialization.Encoding.PEM).decode(),
           "key": key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                     serialization.NoEncryption()).decode()}
result = subprocess.run([sys.argv[1], sys.argv[2], "--openvpn-fixture-stdin"],
                        input=json.dumps(fixture).encode(), timeout=120)
sys.exit(result.returncode)
