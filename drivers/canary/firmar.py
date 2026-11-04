#!/usr/bin/env python3
"""
Firma Authenticode (PKCS#7, SHA-256) de ejecutables PE con un certificado de prueba
autofirmado, sin herramientas externas (solo 'cryptography').
Uso: firmar.py <certificado.pem> <clave.pem> fichero1.sys [fichero2.sys ...]
Si el certificado/clave no existen, los crea.
Despues hay que recalcular el CheckSum del PE (pechecksum.py).
"""
import hashlib, os, struct, sys, datetime
from cryptography import x509
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa, padding

# ---------- DER minimo ----------
def L(n):
    if n < 0x80: return bytes([n])
    b = n.to_bytes((n.bit_length() + 7) // 8, "big")
    return bytes([0x80 | len(b)]) + b
def T(tag, body): return bytes([tag]) + L(len(body)) + body
def SEQ(*p): return T(0x30, b"".join(p))
def SET(*p): return T(0x31, b"".join(p))
def OCT(b): return T(0x04, b)
def INT(n):
    b = n.to_bytes(max(1, (n.bit_length() + 8) // 8), "big", signed=False)
    return T(0x02, b)
def NULL(): return b"\x05\x00"
def OID(s):
    a = [int(x) for x in s.split(".")]
    out = bytes([40 * a[0] + a[1]])
    for v in a[2:]:
        enc = [v & 0x7F]; v >>= 7
        while v: enc.append(0x80 | (v & 0x7F)); v >>= 7
        out += bytes(reversed(enc))
    return T(0x06, out)

OID_SHA256   = "2.16.840.1.101.3.4.2.1"
OID_RSA      = "1.2.840.113549.1.1.1"
OID_SIGNED   = "1.2.840.113549.1.7.2"
OID_SPC_IND  = "1.3.6.1.4.1.311.2.1.4"
OID_SPC_PEIMG= "1.3.6.1.4.1.311.2.1.15"
OID_SPC_OPUS = "1.3.6.1.4.1.311.2.1.12"
OID_CT       = "1.2.840.113549.1.9.3"
OID_MD       = "1.2.840.113549.1.9.4"
ALG_SHA256 = SEQ(OID(OID_SHA256), NULL())

def get_cert(certp, keyp):
    if os.path.exists(certp) and os.path.exists(keyp):
        cert = x509.load_pem_x509_certificate(open(certp, "rb").read())
        key = serialization.load_pem_private_key(open(keyp, "rb").read(), None)
        return cert, key
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "SM-T500 Test Signing")])
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(datetime.datetime(2020, 1, 1)).not_valid_after(datetime.datetime(2045, 1, 1))
            .add_extension(x509.KeyUsage(True, False, False, False, False, False, False, False, False), True)
            .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.CODE_SIGNING]), False)
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), True)
            .sign(key, hashes.SHA256()))
    open(certp, "wb").write(cert.public_bytes(serialization.Encoding.PEM))
    open(keyp, "wb").write(key.private_bytes(serialization.Encoding.PEM,
        serialization.PrivateFormat.TraditionalOpenSSL, serialization.NoEncryption()))
    return cert, key

def authenticode_hash(b, opt, nhdr):
    csum = opt + 64
    secdir = opt + 112 + 4 * 8
    size_headers = struct.unpack_from("<I", b, opt + 60)[0]
    nsec = struct.unpack_from("<H", b, nhdr + 6)[0]
    sec_off = opt + struct.unpack_from("<H", b, nhdr + 20)[0]
    h = hashlib.sha256()
    h.update(b[:csum]); h.update(b[csum + 4:secdir]); h.update(b[secdir + 8:size_headers])
    total = size_headers
    secs = []
    for i in range(nsec):
        o = sec_off + 40 * i
        raw_size, raw_ptr = struct.unpack_from("<II", b, o + 16)
        if raw_size: secs.append((raw_ptr, raw_size))
    for ptr, size in sorted(secs):
        h.update(b[ptr:ptr + size]); total += size
    if len(b) > total: h.update(b[total:])        # datos finales (sin tabla de certificados)
    return h.digest()

def sign_pe(path, cert, key):
    b = bytearray(open(path, "rb").read())
    nhdr = struct.unpack_from("<I", b, 0x3C)[0]
    assert b[nhdr:nhdr + 4] == b"PE\0\0"
    opt = nhdr + 24
    assert struct.unpack_from("<H", b, opt)[0] == 0x20B, "solo PE32+"
    secdir = opt + 112 + 4 * 8
    if struct.unpack_from("<II", b, secdir) != (0, 0):          # ya firmado: lo quitamos
        off, _ = struct.unpack_from("<II", b, secdir)
        del b[off:]; struct.pack_into("<II", b, secdir, 0, 0)
    digest = authenticode_hash(bytes(b), opt, nhdr)

    obsolete = "<<<Obsolete>>>".encode("utf-16-be")
    peimg = SEQ(b"\x03\x01\x00", T(0xA0, T(0xA2, T(0x80, obsolete))))
    content_body = SEQ(OID(OID_SPC_PEIMG), peimg) + SEQ(ALG_SHA256, OCT(digest))
    content = T(0x30, content_body)                          # SpcIndirectDataContent
    md = hashlib.sha256(content_body).digest()               # sin etiqueta ni longitud externas

    attrs = [SEQ(OID(OID_CT), SET(OID(OID_SPC_IND))),
             SEQ(OID(OID_MD), SET(OCT(md))),
             SEQ(OID(OID_SPC_OPUS), SET(SEQ()))]
    attrs.sort()
    attrs_set = SET(*attrs)
    sig = key.sign(attrs_set, padding.PKCS1v15(), hashes.SHA256())

    cert_der = cert.public_bytes(serialization.Encoding.DER)
    issuer = cert.issuer.public_bytes()
    signer = SEQ(INT(1), SEQ(issuer, INT(cert.serial_number)), ALG_SHA256,
                 T(0xA0, b"".join(attrs)), SEQ(OID(OID_RSA), NULL()), OCT(sig))
    signed = SEQ(INT(1), SET(ALG_SHA256), SEQ(OID(OID_SPC_IND), T(0xA0, content)),
                 T(0xA0, cert_der), SET(signer))
    pkcs7 = SEQ(OID(OID_SIGNED), T(0xA0, signed))

    win = struct.pack("<IHH", 8 + len(pkcs7), 0x0200, 0x0002) + pkcs7
    win += b"\0" * (-len(win) % 8)
    while len(b) % 8: b += b"\0"
    struct.pack_into("<II", b, secdir, len(b), len(win))
    b += win
    open(path, "wb").write(b)
    return pkcs7, digest

if __name__ == "__main__":
    certp, keyp, *files = sys.argv[1:]
    cert, key = get_cert(certp, keyp)
    for f in files:
        _, d = sign_pe(f, cert, key)
        print(f"{f}: firmado (SHA-256 Authenticode {d.hex()[:16]}...)")
