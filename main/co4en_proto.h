/* co4en.h - CO4EN ESP-NOW protocol v0.4 (v0.4 DRAFT, MAY CHANGE)
 * Spec: CO4EN_SPEC_v0.4.md. Reference: co4en_ref.py
 * All multi-byte fields LITTLE-ENDIAN. Structs are packed. One telegram per ESP-NOW frame.
 */
#pragma once
#include <stdint.h>

#define CO4EN_VER              0x01
#define CO4EN_MAX_FRAME        250   /* ESP-NOW v1 max payload; default frames must fit */
#define CO4EN_HDR_LEN          12
#define CO4EN_MAX_TLV_BYTES    (CO4EN_MAX_FRAME - CO4EN_HDR_LEN)  /* 238 */
#define CO4EN_QR_PREFIX        "CO4EN1"
#define CO4EN_SECRET_LEN       16    /* 128-bit device secret */
#define CO4EN_MIC_LEN          16    /* HMAC-SHA256 truncated */
#define CO4EN_NONCE_LEN        8
#define CO4EN_HKDF_SALT        "CO4EN1"
#define CO4EN_HKDF_INFO_AUTH   "co4en auth"      /* -> K_auth 32 B */
#define CO4EN_HKDF_INFO_ENC    "co4en pair-enc"  /* -> K_enc  32 B */

/* message types (hdr.type) */
enum {
    CO4EN_T_DATA = 0x01, CO4EN_T_CMD = 0x02, CO4EN_T_ACK = 0x03, CO4EN_T_BEACON = 0x04,
    CO4EN_T_PAIR_REQ = 0x05, CO4EN_T_PAIR_RESP = 0x06, CO4EN_T_FRAG = 0x07,
};

/* hdr.flags */
#define CO4EN_F_ACK_REQ     (1u << 0)
#define CO4EN_F_TIME_VALID  (1u << 1)
#define CO4EN_F_RAW         (1u << 2)
#define CO4EN_F_LOW_ENERGY  (1u << 3)
#define CO4EN_F_MORE        (1u << 4)
#define CO4EN_F_APP_ENC     (1u << 5)   /* Modus B, not used in v1: receivers drop */

/* TLV tags */
enum {
    CO4EN_TAG_OBIS     = 0x10, /* optional profile */
    CO4EN_TAG_RSV_11   = 0x11, /* superseded, reserved */
    CO4EN_TAG_RSV_12   = 0x12, /* superseded, reserved */
    CO4EN_TAG_KV_TEXT  = 0x13, /* default */
    CO4EN_TAG_KV_ID    = 0x14,
    CO4EN_TAG_KEYMAP   = 0x15,
    CO4EN_TAG_STATUS   = 0x20,
    CO4EN_TAG_RAW      = 0x30,
    CO4EN_TAG_CMD      = 0x40, /* only in CO4EN_T_CMD */
    CO4EN_TAG_ACK_INFO = 0x41,
    CO4EN_TAG_PAIR_REQ = 0x50,
    CO4EN_TAG_PAIR_RESP= 0x51,
    CO4EN_TAG_MIC      = 0x5F, /* must be the last TLV; MIC over all preceding bytes */
};

/* vtype byte: bits0-3 type, bits4-5 quality, bit6 TS_OFS (int16 follows value), bit7 0 */
enum { CO4EN_VT_BOOL=0, CO4EN_VT_I32=1, CO4EN_VT_I64=2, CO4EN_VT_F32=3, CO4EN_VT_F64=4,
       CO4EN_VT_STR=5 /* len(1)+<=64 */, CO4EN_VT_BYTES=6 /* len(1)+data */ };
enum { CO4EN_Q_GOOD=0, CO4EN_Q_UNCERTAIN=1, CO4EN_Q_INVALID=2, CO4EN_Q_STALE=3 };
#define CO4EN_VT(type, q)     ((uint8_t)(((type) & 0x0F) | (((q) & 3) << 4)))
#define CO4EN_VT_TS_OFS       0x40
#define CO4EN_STR_MAX         64

/* keys: [a-z0-9_.-], 1..32 chars (v0.4 draft) */
#define CO4EN_KEY_MAX         32
/* key id: 1 byte 0..127, or 2 bytes if bit7 of first byte: id = ((b0&0x7F)<<8)|b1 (0..32767) */
#define CO4EN_KEYID_2B        0x80

/* device types (PAIR_REQ) */
enum { CO4EN_DEV_CII = 0x01, CO4EN_DEV_GATEWAY = 0x02, CO4EN_DEV_TEST = 0x7F };
/* capability bits (PAIR_REQ caps, uint16 LE) */
#define CO4EN_CAP_KV_TEXT  (1u<<0)
#define CO4EN_CAP_KV_ID    (1u<<1)
#define CO4EN_CAP_OBIS     (1u<<2)
#define CO4EN_CAP_RAW      (1u<<3)
#define CO4EN_CAP_CMD      (1u<<4)
#define CO4EN_CAP_V2       (1u<<5)
#define CO4EN_CAP_MAINS    (1u<<6)
#define CO4EN_CAP_TEST     (1u<<7)   /* test sender (esp-touch) */

/* ACK result */
enum { CO4EN_ACK_OK=0, CO4EN_ACK_ERR_DECODE=1, CO4EN_ACK_ERR_REPLAY=2, CO4EN_ACK_ERR_UNPAIRED=3,
       CO4EN_ACK_ERR_DENIED=4 /* e.g. espnow_outputs off */ };

#pragma pack(push, 1)
typedef struct {
    uint8_t  ver;      /* CO4EN_VER */
    uint8_t  type;
    uint8_t  flags;
    uint8_t  hdr_rsv;  /* 0 (Modus B: key id) */
    uint32_t seq;      /* per sender, strictly increasing, never reset */
    uint32_t ts;       /* unix s of measurement, 0 = unknown */
} co4en_hdr_t;         /* 12 B */

typedef struct { uint8_t tag, len; } co4en_tlv_t;  /* followed by len bytes */

typedef struct {
    uint32_t acked_seq;
    uint8_t  channel;
    uint32_t unix;
    uint8_t  result;
} co4en_ack_info_t;    /* 10 B, TLV 0x41 */

typedef struct {
    uint16_t vstore_mv;
    uint32_t uptime_s;
    uint16_t err_bits;
} co4en_status_t;      /* 8 B, TLV 0x20 */

typedef struct {
    uint8_t  mac[6];
    uint8_t  dev_type;
    uint8_t  fw[3];    /* major, minor, patch */
    uint16_t caps;
    uint8_t  nonce_n[CO4EN_NONCE_LEN];
} co4en_pair_req_t;    /* 20 B, TLV 0x50, followed by TLV 0x5F MIC */

typedef struct {
    uint8_t  gw_mac[6];
    uint8_t  nonce_n[CO4EN_NONCE_LEN];  /* echo */
    uint8_t  nonce_g[CO4EN_NONCE_LEN];
    uint8_t  channel;
    uint32_t unix;
    uint8_t  enc_keys[32];  /* (PMK16 || LMK16) XOR HMAC-SHA256(K_enc, "ks"||nonce_n||nonce_g) */
} co4en_pair_resp_t;   /* 59 B, TLV 0x51, followed by TLV 0x5F MIC */

typedef struct {
    uint8_t obis_c, obis_d, obis_e;  /* single bytes */
    int8_t  scaler;
    uint8_t dlms_unit;
    uint8_t quality;
    /* int32 or int64 LE follows (len 10 or 14) */
} co4en_obis_hdr_t;
#pragma pack(pop)

#if defined(__cplusplus)
#define CO4EN_SASSERT(c, m) static_assert(c, m)
#else
#define CO4EN_SASSERT(c, m) _Static_assert(c, m)
#endif
CO4EN_SASSERT(sizeof(co4en_hdr_t) == 12, "hdr");
CO4EN_SASSERT(sizeof(co4en_ack_info_t) == 10, "ack");
CO4EN_SASSERT(sizeof(co4en_pair_req_t) == 20, "preq");
CO4EN_SASSERT(sizeof(co4en_pair_resp_t) == 59, "presp");

/* frame sizes */
#define CO4EN_PAIR_REQ_LEN   (12 + 2 + 20 + 2 + 16)  /* 52 */
#define CO4EN_PAIR_RESP_LEN  (12 + 2 + 59 + 2 + 16)  /* 91 */
#define CO4EN_ACK_LEN        (12 + 2 + 10)           /* 24 */

/* timing (ms) - v0.4 draft */
#define CO4EN_ACK_DEADLINE_MS        10
#define CO4EN_ACK_WAIT_MS            20
#define CO4EN_PAIR_RESP_WAIT_MS      50
#define CO4EN_PAIR_FAST_PERIOD_MS    3000   /* mains sender */
#define CO4EN_PAIR_FAST_DURATION_MS  600000 /* 10 min, then every 10th / 30 s */
#define CO4EN_CMD_RETRY_1_MS 50
#define CO4EN_CMD_RETRY_2_MS 100
#define CO4EN_CMD_RETRY_3_MS 200
#define CO4EN_SEQ_NVS_STEP           256

/* NVS proposal (node): namespace "co4en" */
#define CO4EN_NVS_NS        "co4en"
#define CO4EN_NVS_SECRET    "secret"   /* blob 16, factory */
#define CO4EN_NVS_PAIRED    "paired"   /* u8 */
#define CO4EN_NVS_GW_MAC    "gw_mac"   /* blob 6 */
#define CO4EN_NVS_PMK       "pmk"      /* blob 16 */
#define CO4EN_NVS_LMK       "lmk"      /* blob 16 */
#define CO4EN_NVS_CHANNEL   "chan"     /* u8 */
#define CO4EN_NVS_CHLIST    "chlist"   /* blob <=13 */
#define CO4EN_NVS_SEQ_BASE  "seq_base" /* u32 */
#define CO4EN_NVS_KM_SENT   "km_sent"  /* u8: KEYMAP announced to this gw */
