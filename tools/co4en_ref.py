#!/usr/bin/env python3
"""CO4EN v0.4 reference encoder/decoder (draft, may change). Python 3 stdlib only.

Usage:
  python3 co4en_ref.py vectors        # print all test vectors (hex)
  python3 co4en_ref.py decode <hex> [--secret BASE32] [--lmk HEX]
"""
import struct, hmac, hashlib, base64, sys

VER = 0x01
T_DATA, T_CMD, T_ACK, T_BEACON, T_PAIR_REQ, T_PAIR_RESP, T_FRAG = 1, 2, 3, 4, 5, 6, 7
F_ACK_REQ, F_TIME_VALID, F_RAW, F_LOW_ENERGY, F_MORE, F_APP_ENC = 1, 2, 4, 8, 16, 32
TAG_OBIS, TAG_KV_TEXT, TAG_KV_ID, TAG_KEYMAP = 0x10, 0x13, 0x14, 0x15
TAG_STATUS, TAG_RAW, TAG_CMD, TAG_ACK_INFO = 0x20, 0x30, 0x40, 0x41
TAG_PAIR_REQ, TAG_PAIR_RESP, TAG_MIC = 0x50, 0x51, 0x5F
VT_BOOL, VT_I32, VT_I64, VT_F32, VT_F64, VT_STR, VT_BYTES = range(7)
MAX_FRAME = 250
MIC_LEN = 16
SALT = b"CO4EN1"

# ---------- keys ----------
def hkdf(ikm, info, n, salt=SALT):
    prk = hmac.new(salt, ikm, hashlib.sha256).digest()
    out, t, i = b"", b"", 1
    while len(out) < n:
        t = hmac.new(prk, t + info + bytes([i]), hashlib.sha256).digest(); out += t; i += 1
    return out[:n]

def k_auth(secret): return hkdf(secret, b"co4en auth", 32)
def k_enc(secret):  return hkdf(secret, b"co4en pair-enc", 32)
def mic(key, data): return hmac.new(key, data, hashlib.sha256).digest()[:MIC_LEN]

def qr_parse(s):
    p = s.split(":")
    assert p[0] == "CO4EN1" and len(p) == 3, "bad QR"
    mac = bytes.fromhex(p[1].replace("-", "").replace(":", ""))
    b32 = p[2].upper(); b32 += "=" * (-len(b32) % 8)
    return mac, base64.b32decode(b32)

def qr_make(mac, secret):
    return "CO4EN1:%s:%s" % (mac.hex().upper(), base64.b32encode(secret).decode().rstrip("="))

# ---------- header / TLV ----------
def header(typ, flags, seq, ts, rsv=0):
    return struct.pack("<BBBBII", VER, typ, flags, rsv, seq, ts)

def tlv(tag, val):
    assert len(val) <= 255
    return bytes([tag, len(val)]) + val

def vtype(t, q=0, ts_ofs=False): return t | (q << 4) | (0x40 if ts_ofs else 0)

def enc_value(t, v):
    if t == VT_BOOL: return bytes([1 if v else 0])
    if t == VT_I32: return struct.pack("<i", v)
    if t == VT_I64: return struct.pack("<q", v)
    if t == VT_F32: return struct.pack("<f", v)
    if t == VT_F64: return struct.pack("<d", v)
    if t == VT_STR:
        b = v.encode(); assert len(b) <= 64; return bytes([len(b)]) + b
    if t == VT_BYTES: return bytes([len(v)]) + v
    raise ValueError(t)

def check_key(k):
    import re
    assert re.fullmatch(r"[a-z0-9_.\-]{1,32}", k), "bad key %r" % k
    return k.encode()

def enc_keyid(i):
    assert 0 <= i <= 0x7FFF
    return bytes([i]) if i < 0x80 else bytes([0x80 | (i >> 8), i & 0xFF])

def kv_text(key, t, v, q=0, ts_ofs=None):
    k = check_key(key)
    b = bytes([len(k)]) + k + bytes([vtype(t, q, ts_ofs is not None)]) + enc_value(t, v)
    if ts_ofs is not None: b += struct.pack("<h", ts_ofs)
    return tlv(TAG_KV_TEXT, b)

def kv_id(kid, t, v, q=0, ts_ofs=None):
    b = enc_keyid(kid) + bytes([vtype(t, q, ts_ofs is not None)]) + enc_value(t, v)
    if ts_ofs is not None: b += struct.pack("<h", ts_ofs)
    return tlv(TAG_KV_ID, b)

def keymap(kid, key):
    k = check_key(key); return tlv(TAG_KEYMAP, enc_keyid(kid) + bytes([len(k)]) + k)

def frame(typ, flags, seq, ts, tlvs):
    f = header(typ, flags, seq, ts) + b"".join(tlvs)
    assert len(f) <= MAX_FRAME, len(f)
    return f

# ---------- pairing ----------
DEV_CII, DEV_GATEWAY, DEV_TEST = 0x01, 0x02, 0x7F
CAP_KV_TEXT, CAP_KV_ID, CAP_OBIS, CAP_RAW, CAP_CMD, CAP_V2, CAP_MAINS, CAP_TEST = (1 << i for i in range(8))

def pair_req(secret, node_mac, dev_type, fw, caps, nonce_n, seq, ts=0, flags=0):
    body = node_mac + bytes([dev_type]) + bytes(fw) + struct.pack("<H", caps) + nonce_n
    f = header(T_PAIR_REQ, flags, seq, ts) + tlv(TAG_PAIR_REQ, body)
    return f + tlv(TAG_MIC, mic(k_auth(secret), f))

def pair_resp(secret, gw_mac, nonce_n, nonce_g, channel, unix, pmk, lmk, seq):
    ks = hmac.new(k_enc(secret), b"ks" + nonce_n + nonce_g, hashlib.sha256).digest()
    enc = bytes(a ^ b for a, b in zip(pmk + lmk, ks))
    body = gw_mac + nonce_n + nonce_g + bytes([channel]) + struct.pack("<I", unix) + enc
    f = header(T_PAIR_RESP, F_TIME_VALID, seq, unix) + tlv(TAG_PAIR_RESP, body)
    return f + tlv(TAG_MIC, mic(k_auth(secret), f))

def pair_resp_open(secret, f):
    """Node side: verify MIC and recover PMK/LMK."""
    assert hmac.compare_digest(f[-MIC_LEN:], mic(k_auth(secret), f[:-MIC_LEN - 2])), "MIC fail"
    b = f[14:14 + f[13]]
    gw, nn, ng, ch = b[:6], b[6:14], b[14:22], b[22]
    unix = struct.unpack_from("<I", b, 23)[0]
    ks = hmac.new(k_enc(secret), b"ks" + nn + ng, hashlib.sha256).digest()
    keys = bytes(a ^ c for a, c in zip(b[27:59], ks))
    return dict(gw_mac=gw.hex(), channel=ch, unix=unix, pmk=keys[:16].hex(), lmk=keys[16:].hex())

def ack(seq, acked, channel, unix, result=0):
    return frame(T_ACK, F_TIME_VALID, seq, unix,
                 [tlv(TAG_ACK_INFO, struct.pack("<IBIB", acked, channel, unix, result))])

# ---------- decoder ----------
def dec_value(t, b, o):
    if t == VT_BOOL: return bool(b[o]), o + 1
    if t == VT_I32: return struct.unpack_from("<i", b, o)[0], o + 4
    if t == VT_I64: return struct.unpack_from("<q", b, o)[0], o + 8
    if t == VT_F32: return round(struct.unpack_from("<f", b, o)[0], 6), o + 4
    if t == VT_F64: return struct.unpack_from("<d", b, o)[0], o + 8
    if t in (VT_STR, VT_BYTES):
        n = b[o]; s = b[o + 1:o + 1 + n]
        return (s.decode() if t == VT_STR else s.hex()), o + 1 + n
    raise ValueError("vtype %d" % t)

def dec_kid(b, o):
    if b[o] & 0x80: return ((b[o] & 0x7F) << 8) | b[o + 1], o + 2
    return b[o], o + 1

def dec_kv(v, o, out_key):
    vt = v[o]; o += 1
    val, o = dec_value(vt & 0x0F, v, o)
    d = dict(key=out_key, type=vt & 0x0F, q=(vt >> 4) & 3, value=val)
    if vt & 0x40: d["ts_ofs"] = struct.unpack_from("<h", v, o)[0]
    return d

def decode(f, secret=None, keymap_tbl=None):
    keymap_tbl = dict(keymap_tbl or {})
    assert len(f) >= 12 and len(f) <= MAX_FRAME
    ver, typ, flags, rsv, seq, ts = struct.unpack_from("<BBBBII", f)
    r = dict(ver=ver, type=typ, flags=flags, rsv=rsv, seq=seq, ts=ts, items=[])
    o = 12
    while o < len(f):
        tag, n = f[o], f[o + 1]; v = f[o + 2:o + 2 + n]; assert len(v) == n, "truncated TLV"
        if tag == TAG_KV_TEXT:
            k = v[1:1 + v[0]].decode(); r["items"].append(dec_kv(v, 1 + v[0], k))
        elif tag == TAG_KV_ID:
            kid, p = dec_kid(v, 0); r["items"].append(dec_kv(v, p, keymap_tbl.get(kid, "id_%d" % kid)))
        elif tag == TAG_KEYMAP:
            kid, p = dec_kid(v, 0); keymap_tbl[kid] = v[p + 1:p + 1 + v[p]].decode()
            r["items"].append(dict(keymap=kid, key=keymap_tbl[kid]))
        elif tag == TAG_CMD:
            r["items"].append(dict(cmd=v.hex()))
        elif tag == TAG_ACK_INFO:
            a, ch, t, res = struct.unpack("<IBIB", v); r["items"].append(dict(acked=a, channel=ch, unix=t, result=res))
        elif tag == TAG_PAIR_REQ:
            r["items"].append(dict(pair_req=dict(mac=v[:6].hex(), dev_type=v[6], fw="%d.%d.%d" % tuple(v[7:10]),
                                   caps=struct.unpack_from("<H", v, 10)[0], nonce=v[12:20].hex())))
        elif tag == TAG_PAIR_RESP:
            r["items"].append(dict(pair_resp=dict(gw_mac=v[:6].hex(), channel=v[22])))
        elif tag == TAG_MIC:
            ok = None
            if secret is not None: ok = hmac.compare_digest(v, mic(k_auth(secret), f[:o]))
            r["items"].append(dict(mic=v.hex(), mic_ok=ok))
        else:
            r["items"].append(dict(unknown_tag=tag, len=n))  # skipped, forward compatible
        o += 2 + n
    return r

# ---------- vectors ----------
TEST_SECRET = bytes(range(16))                 # 000102..0f  TEST ONLY
NODE_MAC = bytes.fromhex("F0F5BD010203")
GW_MAC = bytes.fromhex("DCDA0C112233")
NONCE_N = bytes.fromhex("1122334455667788")
NONCE_G = bytes.fromhex("8877665544332211")
TEST_PMK = bytes.fromhex("a0a1a2a3a4a5a6a7a8a9aaabacadaeaf")  # TEST ONLY
TEST_LMK = bytes.fromhex("b0b1b2b3b4b5b6b7b8b9babbbcbdbebf")  # TEST ONLY
TS = 1793000000  # fixed test timestamp

def vectors():
    v = {}
    v["qr"] = qr_make(NODE_MAC, TEST_SECRET)
    v["k_auth"] = k_auth(TEST_SECRET).hex(); v["k_enc"] = k_enc(TEST_SECRET).hex()
    v["pair_req"] = pair_req(TEST_SECRET, NODE_MAC, DEV_CII, (0, 4, 0),
                             CAP_KV_TEXT | CAP_KV_ID | CAP_CMD | CAP_MAINS | CAP_TEST, NONCE_N, seq=1)
    v["pair_resp"] = pair_resp(TEST_SECRET, GW_MAC, NONCE_N, NONCE_G, 11, TS, TEST_PMK, TEST_LMK, seq=500)
    v["kv_text"] = frame(T_DATA, F_ACK_REQ | F_TIME_VALID, 2, TS, [
        kv_text("p.imp", VT_F32, 1234.5), kv_text("u.l1", VT_F32, 230.1), kv_text("f", VT_F32, 50.0)])
    v["keymap"] = frame(T_DATA, F_ACK_REQ, 3, 0, [keymap(1, "e.imp"), keymap(2, "p.imp"), keymap(200, "u.l1")])
    v["kv_id"] = frame(T_DATA, F_TIME_VALID, 4, TS, [
        kv_id(1, VT_I64, 12345678), kv_id(2, VT_F32, 1234.5), kv_id(200, VT_F32, 230.1, ts_ofs=-5)])
    v["ack"] = ack(501, 2, 11, TS + 1)
    return v

def main():
    if len(sys.argv) > 1 and sys.argv[1] == "decode":
        f = bytes.fromhex(sys.argv[2].replace(" ", ""))
        sec = None
        if "--secret" in sys.argv:
            s = sys.argv[sys.argv.index("--secret") + 1]; s += "=" * (-len(s) % 8); sec = base64.b32decode(s.upper())
        print(decode(f, sec)); return
    v = vectors()
    for k, x in v.items():
        if isinstance(x, bytes):
            print("## %s (%d B)\n%s\n%s\n" % (k, len(x), x.hex(" ").upper(), decode(x, TEST_SECRET,
                  {1: "e.imp", 2: "p.imp", 200: "u.l1"})))
        else:
            print("## %s\n%s\n" % (k, x))
    print("## pair_resp opened by node\n%s" % pair_resp_open(TEST_SECRET, v["pair_resp"]))

if __name__ == "__main__":
    main()
