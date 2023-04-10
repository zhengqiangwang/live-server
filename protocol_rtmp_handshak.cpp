#include "protocol_rtmp_handshak.h"
#include "buffer.h"
#include "core.h"
#include "error.h"
#include "protocol_io.h"
#include "protocol_rtmp_stack.h"
#include "protocol_utility.h"
#include "core_autofree.h"
#include "utility.h"
#include "io.h"
#include <cstring>


using namespace internal;

// for openssl_HMACsha256
#include <openssl/evp.h>
#include <openssl/hmac.h>
// for openssl_generate_key
#include <openssl/dh.h>

// For randomly generate the handshake bytes.
#define RTMP_SIG_SRS_HANDSHAKE RTMP_SIG_KEY "(" RTMP_SIG_VERSION ")"

// @see https://wiki.openssl.org/index.php/OpenSSL_1.1.0_Changes
#if OPENSSL_VERSION_NUMBER < 0x10100000L

HMAC_CTX *HMAC_CTX_new(void)
{
    HMAC_CTX *ctx = (HMAC_CTX *)malloc(sizeof(*ctx));
    if (ctx != NULL) {
        HMAC_CTX_init(ctx);
    }
    return ctx;
}

void HMAC_CTX_free(HMAC_CTX *ctx)
{
    if (ctx != NULL) {
        HMAC_CTX_cleanup(ctx);
        free(ctx);
    }
}

static void DH_get0_key(const DH *dh, const BIGNUM **pub_key, const BIGNUM **priv_key)
{
    if (pub_key != NULL) {
        *pub_key = dh->pub_key;
    }
    if (priv_key != NULL) {
        *priv_key = dh->priv_key;
    }
}

static int DH_set0_pqg(DH *dh, BIGNUM *p, BIGNUM *q, BIGNUM *g)
{
    /* If the fields p and g in d are NULL, the corresponding input
     * parameters MUST be non-NULL.  q may remain NULL.
     */
    if ((dh->p == NULL && p == NULL)
            || (dh->g == NULL && g == NULL))
        return 0;

    if (p != NULL) {
        BN_free(dh->p);
        dh->p = p;
    }
    if (q != NULL) {
        BN_free(dh->q);
        dh->q = q;
    }
    if (g != NULL) {
        BN_free(dh->g);
        dh->g = g;
    }

    if (q != NULL) {
        dh->length = BN_num_bits(q);
    }

    return 1;
}

static int DH_set_length(DH *dh, long length)
{
    dh->length = length;
    return 1;
}

#endif

namespace internal
{
    // 68bytes FMS key which is used to sign the sever packet.
    uint8_t GenuineFMSKey[] = {
        0x47, 0x65, 0x6e, 0x75, 0x69, 0x6e, 0x65, 0x20,
        0x41, 0x64, 0x6f, 0x62, 0x65, 0x20, 0x46, 0x6c,
        0x61, 0x73, 0x68, 0x20, 0x4d, 0x65, 0x64, 0x69,
        0x61, 0x20, 0x53, 0x65, 0x72, 0x76, 0x65, 0x72,
        0x20, 0x30, 0x30, 0x31, // Genuine Adobe Flash Media Server 001
        0xf0, 0xee, 0xc2, 0x4a, 0x80, 0x68, 0xbe, 0xe8,
        0x2e, 0x00, 0xd0, 0xd1, 0x02, 0x9e, 0x7e, 0x57,
        0x6e, 0xec, 0x5d, 0x2d, 0x29, 0x80, 0x6f, 0xab,
        0x93, 0xb8, 0xe6, 0x36, 0xcf, 0xeb, 0x31, 0xae
    }; // 68

    // 62bytes FP key which is used to sign the client packet.
    uint8_t GenuineFPKey[] = {
        0x47, 0x65, 0x6E, 0x75, 0x69, 0x6E, 0x65, 0x20,
        0x41, 0x64, 0x6F, 0x62, 0x65, 0x20, 0x46, 0x6C,
        0x61, 0x73, 0x68, 0x20, 0x50, 0x6C, 0x61, 0x79,
        0x65, 0x72, 0x20, 0x30, 0x30, 0x31, // Genuine Adobe Flash Player 001
        0xF0, 0xEE, 0xC2, 0x4A, 0x80, 0x68, 0xBE, 0xE8,
        0x2E, 0x00, 0xD0, 0xD1, 0x02, 0x9E, 0x7E, 0x57,
        0x6E, 0xEC, 0x5D, 0x2D, 0x29, 0x80, 0x6F, 0xAB,
        0x93, 0xB8, 0xE6, 0x36, 0xCF, 0xEB, 0x31, 0xAE
    }; // 62

    error DoOpensslHMACsha256(HMAC_CTX* ctx, const void* data, int data_size, void* digest, unsigned int* digest_size)
    {
        error err = SUCCESS;

        if (HMAC_Update(ctx, (unsigned char *) data, data_size) < 0) {
            return ERRORNEW(ERROR_OpenSslSha256Update, "hmac update");
        }

        if (HMAC_Final(ctx, (unsigned char *) digest, digest_size) < 0) {
            return ERRORNEW(ERROR_OpenSslSha256Final, "hmac final");
        }

        return err;
    }

    /**
     * sha256 digest algorithm.
     * @param key the sha256 key, NULL to use EVP_Digest, for instance,
     *       hashlib.sha256(data).digest().
     */
    error OpensslHMACsha256(const void* key, int key_size, const void* data, int data_size, void* digest)
    {
        error err = SUCCESS;

        unsigned int digest_size = 0;

        unsigned char* temp_key = (unsigned char*)key;
        unsigned char* temp_digest = (unsigned char*)digest;

        if (key == NULL) {
            // use data to digest.
            // @see ./crypto/sha/sha256t.c
            // @see ./crypto/evp/digest.c
            if (EVP_Digest(data, data_size, temp_digest, &digest_size, EVP_sha256(), NULL) < 0) {
                return ERRORNEW(ERROR_OpenSslSha256EvpDigest, "evp digest");
            }
        } else {
            // use key-data to digest.
            HMAC_CTX *ctx = HMAC_CTX_new();
            if (ctx == NULL) {
                return ERRORNEW(ERROR_OpenSslCreateHMAC, "hmac new");
            }
            // @remark, if no key, use EVP_Digest to digest,
            // for instance, in python, hashlib.sha256(data).digest().
            if (HMAC_Init_ex(ctx, temp_key, key_size, EVP_sha256(), NULL) < 0) {
                HMAC_CTX_free(ctx);
                return ERRORNEW(ERROR_OpenSslSha256Init, "hmac init");
            }

            err = DoOpensslHMACsha256(ctx, data, data_size, temp_digest, &digest_size);
            HMAC_CTX_free(ctx);

            if (err != SUCCESS) {
                return ERRORWRAP(err, "hmac sha256");
            }
        }

        if (digest_size != 32) {
            return ERRORNEW(ERROR_OpenSslSha256DigestSize, "digest size %d", digest_size);
        }

        return err;
    }

    #define RFC2409_PRIME_1024 \
        "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1" \
        "29024E088A67CC74020BBEA63B139B22514A08798E3404DD" \
        "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245" \
        "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED" \
        "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE65381" \
        "FFFFFFFFFFFFFFFF"

    SDH::SDH()
    {
        m_pdh = nullptr;
    }

    SDH::~SDH()
    {
        Close();
    }

    void SDH::Close()
    {
        if(m_pdh != nullptr){
            DH_free(m_pdh);
            m_pdh = nullptr;
        }
    }

    error SDH::Initialize(bool ensure_128bytes_public_key)
    {
        error err = SUCCESS;

        for (;;) {
            if ((err = DoInitialize()) != SUCCESS) {
                return ERRORWRAP(err, "init");
            }

            if (ensure_128bytes_public_key) {
                const BIGNUM *pub_key = NULL;
                DH_get0_key(m_pdh, &pub_key, NULL);
                int32_t key_size = BN_num_bytes(pub_key);
                if (key_size != 128) {
                    warn("regenerate 128B key, current=%dB", key_size);
                    continue;
                }
            }

            break;
        }

        return err;
    }

    error SDH::CopyPublicKey(char *pkey, int32_t &pkey_size)
    {
        error err = SUCCESS;

        // copy public key to bytes.
        // sometimes, the key_size is 127, seems ok.
        const BIGNUM *pub_key = NULL;
        DH_get0_key(m_pdh, &pub_key, NULL);
        int32_t key_size = BN_num_bytes(pub_key);
        Assert(key_size > 0);

        // maybe the key_size is 127, but dh will write all 128bytes pkey,
        // so, donot need to set/initialize the pkey.
        // @see https://github.com/ossrs/srs/issues/165
        key_size = BN_bn2bin(pub_key, (unsigned char*)pkey);
        Assert(key_size > 0);

        // output the size of public key.
        // @see https://github.com/ossrs/srs/issues/165
        Assert(key_size <= pkey_size);
        pkey_size = key_size;

        return err;
    }

    error SDH::CopySharedKey(const char *ppkey, int32_t ppkey_size, char *skey, int32_t &skey_size)
    {
        error err = SUCCESS;

        BIGNUM* ppk = NULL;
        if ((ppk = BN_bin2bn((const unsigned char*)ppkey, ppkey_size, 0)) == NULL) {
            return ERRORNEW(ERROR_OpenSslGetPeerPublicKey, "bin2bn");
        }

        // if failed, donot return, do cleanup, @see ./test/dhtest.c:168
        // maybe the key_size is 127, but dh will write all 128bytes skey,
        // so, donot need to set/initialize the skey.
        // @see https://github.com/ossrs/srs/issues/165
        int32_t key_size = DH_compute_key((unsigned char*)skey, ppk, m_pdh);

        if (key_size < ppkey_size) {
            warn("shared key size=%d, ppk_size=%d", key_size, ppkey_size);
        }

        if (key_size < 0 || key_size > skey_size) {
            err = ERRORNEW(ERROR_OpenSslComputeSharedKey, "key size %d", key_size);
        } else {
            skey_size = key_size;
        }

        if (ppk) {
            BN_free(ppk);
        }

        return err;
    }

    error SDH::DoInitialize()
    {
        error err = SUCCESS;

        int32_t bits_count = 1024;

        Close();

        //1. Create the DH
        if ((m_pdh = DH_new()) == NULL) {
            return ERRORNEW(ERROR_OpenSslCreateDH, "dh new");
        }

        //2. Create his internal p and g
        BIGNUM *p, *g;
        if ((p = BN_new()) == NULL) {
            return ERRORNEW(ERROR_OpenSslCreateP, "dh new");
        }
        if ((g = BN_new()) == NULL) {
            BN_free(p);
            return ERRORNEW(ERROR_OpenSslCreateG, "bn new");
        }
        DH_set0_pqg(m_pdh, p, NULL, g);

        //3. initialize p and g, @see ./test/ectest.c:260
        if (!BN_hex2bn(&p, RFC2409_PRIME_1024)) {
            return ERRORNEW(ERROR_OpenSslParseP1024, "hex2bn");
        }
        // @see ./test/bntest.c:1764
        if (!BN_set_word(g, 2)) {
            return ERRORNEW(ERROR_OpenSslSetG, "set word");
        }

        // 4. Set the key length
        DH_set_length(m_pdh, bits_count);

        // 5. Generate private and public key
        // @see ./test/dhtest.c:152
        if (!DH_generate_key(m_pdh)) {
            return ERRORNEW(ERROR_OpenSslGenerateDHKeys, "dh generate key");
        }

        return err;
    }

    KeyBlock::KeyBlock()
    {
        m_offset = (int32_t)Random();
        m_random0 = NULL;
        m_random1 = NULL;

        int valid_offset = CalcValidOffset();
        Assert(valid_offset >= 0);

        m_random0Size = valid_offset;
        if (m_random0Size > 0) {
            m_random0 = new char[m_random0Size];
            RandomGenerate(m_random0, m_random0Size);
            snprintf(m_random0, m_random0Size, "%s", RTMP_SIG_SRS_HANDSHAKE);
        }

        RandomGenerate(m_key, sizeof(m_key));

        m_random1Size = 764 - valid_offset - 128 - 4;
        if (m_random1Size > 0) {
            m_random1 = new char[m_random1Size];
            RandomGenerate(m_random1, m_random1Size);
            snprintf(m_random1, m_random1Size, "%s", RTMP_SIG_SRS_HANDSHAKE);
        }
    }

    KeyBlock::~KeyBlock()
    {
        Freepa(m_random0);
        Freepa(m_random1);
    }

    error KeyBlock::Parse(Buffer *stream)
    {
        error err = SUCCESS;

        // the key must be 764 bytes.
        Assert(stream->Require(764));

        // read the last offset first, 760-763
        stream->Skip(764 - sizeof(int32_t));
        m_offset = stream->Read4Bytes();

        // reset stream to read others.
        stream->Skip(-764);

        int valid_offset = CalcValidOffset();
        Assert(valid_offset >= 0);

        m_random0Size = valid_offset;
        if (m_random0Size > 0) {
            Freepa(m_random0);
            m_random0 = new char[m_random0Size];
            stream->ReadBytes(m_random0, m_random0Size);
        }

        stream->ReadBytes(m_key, 128);

        m_random1Size = 764 - valid_offset - 128 - 4;
        if (m_random1Size > 0) {
            Freepa(m_random1);
            m_random1 = new char[m_random1Size];
            stream->ReadBytes(m_random1, m_random1Size);
        }

        return err;
    }

    int KeyBlock::CalcValidOffset()
    {
        int max_offset_size = 764 - 128 - 4;

        int valid_offset = 0;
        uint8_t* pp = (uint8_t*)&m_offset;
        valid_offset += *pp++;
        valid_offset += *pp++;
        valid_offset += *pp++;
        valid_offset += *pp++;

        return valid_offset % max_offset_size;
    }

    DigestBlock::DigestBlock()
    {
        m_offset = (int32_t)Random();
        m_random0 = NULL;
        m_random1 = NULL;

        int valid_offset = CalcValidOffset();
        Assert(valid_offset >= 0);

        m_random0Size = valid_offset;
        if (m_random0Size > 0) {
            m_random0 = new char[m_random0Size];
            RandomGenerate(m_random0, m_random0Size);
            snprintf(m_random0, m_random0Size, "%s", RTMP_SIG_SRS_HANDSHAKE);
        }

        RandomGenerate(m_digest, sizeof(m_digest));

        m_random1Size = 764 - 4 - valid_offset - 32;
        if (m_random1Size > 0) {
            m_random1 = new char[m_random1Size];
            RandomGenerate(m_random1, m_random1Size);
            snprintf(m_random1, m_random1Size, "%s", RTMP_SIG_SRS_HANDSHAKE);
        }
    }

    DigestBlock::~DigestBlock()
    {
        Freepa(m_random0);
        Freepa(m_random1);
    }

    error DigestBlock::Parse(Buffer *stream)
    {
        error err = SUCCESS;

        // the digest must be 764 bytes.
        Assert(stream->Require(764));

        m_offset = stream->Read4Bytes();

        int valid_offset = CalcValidOffset();
        Assert(valid_offset >= 0);

        m_random0Size = valid_offset;
        if (m_random0Size > 0) {
            Freepa(m_random0);
            m_random0 = new char[m_random0Size];
            stream->ReadBytes(m_random0, m_random0Size);
        }

        stream->ReadBytes(m_digest, 32);

        m_random1Size = 764 - 4 - valid_offset - 32;
        if (m_random1Size > 0) {
            Freepa(m_random1);
            m_random1 = new char[m_random1Size];
            stream->ReadBytes(m_random1, m_random1Size);
        }

        return err;
    }

    int DigestBlock::CalcValidOffset()
    {
        int max_offset_size = 764 - 32 - 4;

        int valid_offset = 0;
        uint8_t* pp = (uint8_t*)&m_offset;
        valid_offset += *pp++;
        valid_offset += *pp++;
        valid_offset += *pp++;
        valid_offset += *pp++;

        return valid_offset % max_offset_size;
    }

    c1s1Strategy::c1s1Strategy()
    {

    }

    c1s1Strategy::~c1s1Strategy()
    {

    }

    char *c1s1Strategy::GetDigest()
    {
        return m_digest.m_digest;
    }

    char *c1s1Strategy::GetKey()
    {
        return m_key.m_key;
    }

    error c1s1Strategy::Dump(c1s1 *owner, char *_c1s1, int size)
    {
        Assert(size == 1536);
        return CopyTo(owner, _c1s1, size, true);
    }

    error c1s1Strategy::C1Create(c1s1 *owner)
    {
        error err = SUCCESS;

        // generate digest
        char* c1_digest = NULL;

        if ((err = CalcC1Digest(owner, c1_digest)) != SUCCESS) {
            return ERRORWRAP(err, "sign c1");
        }

        Assert(c1_digest != NULL);
        AutoFreeA(char, c1_digest);

        memcpy(m_digest.m_digest, c1_digest, 32);

        return err;
    }

    error c1s1Strategy::C1ValidateDigest(c1s1 *owner, bool &is_valid)
    {
        error err = SUCCESS;

        char* c1_digest = NULL;

        if ((err = CalcC1Digest(owner, c1_digest)) != SUCCESS) {
            return ERRORWRAP(err, "validate c1");
        }

        Assert(c1_digest != NULL);
        AutoFreeA(char, c1_digest);

        is_valid = BytesEquals(m_digest.m_digest, c1_digest, 32);

        return err;
    }

    error c1s1Strategy::S1Create(c1s1 *owner, c1s1 *c1)
    {
        error err = SUCCESS;

        SDH dh;

        // ensure generate 128bytes public key.
        if ((err = dh.Initialize(true)) != SUCCESS) {
            return ERRORWRAP(err, "dh init");
        }

        // directly generate the public key.
        int pkey_size = 128;
        if ((err = dh.CopySharedKey(c1->GetKey(), 128, m_key.m_key, pkey_size)) != SUCCESS) {
            return ERRORWRAP(err, "copy shared key");
        }

        // although the public key is always 128bytes, but the share key maybe not.
        // we just ignore the actual key size, but if need to use the key, must use the actual size.
        // TODO: FIXME: use the actual key size.
        //Assert(pkey_size == 128);

        char* s1_digest = NULL;
        if ((err = CalcS1Digest(owner, s1_digest))  != SUCCESS) {
            return ERRORWRAP(err, "calc s1 digest");
        }

        Assert(s1_digest != NULL);
        AutoFreeA(char, s1_digest);

        memcpy(m_digest.m_digest, s1_digest, 32);

        return err;
    }

    error c1s1Strategy::S1ValidateDigest(c1s1 *owner, bool &is_valid)
    {
        error err = SUCCESS;

        char* s1_digest = NULL;

        if ((err = CalcS1Digest(owner, s1_digest)) != SUCCESS) {
            return ERRORWRAP(err, "validate s1");
        }

        Assert(s1_digest != NULL);
        AutoFreeA(char, s1_digest);

        is_valid = BytesEquals(m_digest.m_digest, s1_digest, 32);

        return err;
    }

    error c1s1Strategy::CalcC1Digest(c1s1 *owner, char *&c1_digest)
    {
        error err = SUCCESS;

        /**
         * c1s1 is splited by digest:
         *     c1s1-part1: n bytes (time, version, key and digest-part1).
         *     digest-data: 32bytes
         *     c1s1-part2: (1536-n-32)bytes (digest-part2)
         * @return a new allocated bytes, user must free it.
         */
        char* c1s1_joined_bytes = new char[1536 -32];
        AutoFreeA(char, c1s1_joined_bytes);
        if ((err = CopyTo(owner, c1s1_joined_bytes, 1536 - 32, false)) != SUCCESS) {
            return ERRORWRAP(err, "copy bytes");
        }

        c1_digest = new char[OpensslHashSize];
        if ((err = OpensslHMACsha256(GenuineFPKey, 30, c1s1_joined_bytes, 1536 - 32, c1_digest)) != SUCCESS) {
            Freepa(c1_digest);
            return ERRORWRAP(err, "calc c1 digest");
        }

        return err;
    }

    error c1s1Strategy::CalcS1Digest(c1s1 *owner, char *&s1_digest)
    {
        error err = SUCCESS;

        /**
         * c1s1 is splited by digest:
         *     c1s1-part1: n bytes (time, version, key and digest-part1).
         *     digest-data: 32bytes
         *     c1s1-part2: (1536-n-32)bytes (digest-part2)
         * @return a new allocated bytes, user must free it.
         */
        char* c1s1_joined_bytes = new char[1536 -32];
        AutoFreeA(char, c1s1_joined_bytes);
        if ((err = CopyTo(owner, c1s1_joined_bytes, 1536 - 32, false)) != SUCCESS) {
            return ERRORWRAP(err, "copy bytes");
        }

        s1_digest = new char[OpensslHashSize];
        if ((err = OpensslHMACsha256(GenuineFMSKey, 36, c1s1_joined_bytes, 1536 - 32, s1_digest)) != SUCCESS) {
            Freepa(s1_digest);
            return ERRORWRAP(err, "calc s1 digest");
        }

        return err;
    }

    void c1s1Strategy::CopyTimeVersion(Buffer *stream, c1s1 *owner)
    {
        Assert(stream->Require(8));

        // 4bytes time
        stream->Write4Bytes(owner->m_time);

        // 4bytes version
        stream->Write4Bytes(owner->m_version);
    }

    void c1s1Strategy::CopyKey(Buffer *stream)
    {
        Assert(m_key.m_random0Size >= 0);
        Assert(m_key.m_random1Size >= 0);

        int total = m_key.m_random0Size + 128 + m_key.m_random1Size + 4;
        Assert(stream->Require(total));

        // 764bytes key block
        if (m_key.m_random0Size > 0) {
            stream->WriteBytes(m_key.m_random0, m_key.m_random0Size);
        }

        stream->WriteBytes(m_key.m_key, 128);

        if (m_key.m_random1Size > 0) {
            stream->WriteBytes(m_key.m_random1, m_key.m_random1Size);
        }

        stream->Write4Bytes(m_key.m_offset);
    }

    void c1s1Strategy::CopyDigest(Buffer *stream, bool with_digest)
    {
        Assert(m_key.m_random0Size >= 0);
        Assert(m_key.m_random1Size >= 0);

        int total = 4 + m_digest.m_random0Size + m_digest.m_random1Size;
        if (with_digest) {
            total += 32;
        }
        Assert(stream->Require(total));

        // 732bytes digest block without the 32bytes digest-data
        // nbytes digest block part1
        stream->Write4Bytes(m_digest.m_offset);

        // digest random padding.
        if (m_digest.m_random0Size > 0) {
            stream->WriteBytes(m_digest.m_random0, m_digest.m_random0Size);
        }

        // digest
        if (with_digest) {
            stream->WriteBytes(m_digest.m_digest, 32);
        }

        // nbytes digest block part2
        if (m_digest.m_random1Size > 0) {
            stream->WriteBytes(m_digest.m_random1, m_digest.m_random1Size);
        }
    }

    c1s1StrategySchema0::c1s1StrategySchema0()
    {

    }

    c1s1StrategySchema0::~c1s1StrategySchema0()
    {

    }

    schema_type c1s1StrategySchema0::Schema()
    {
        return schema0;
    }

    error c1s1StrategySchema0::Parse(char *_c1s1, int size)
    {
        error err = SUCCESS;

        Assert(size == 1536);

        if (true) {
            Buffer stream(_c1s1 + 8, 764);

            if ((err = m_key.Parse(&stream)) != SUCCESS) {
                return ERRORWRAP(err, "parse the c1 key");
            }
        }

        if (true) {
            Buffer stream(_c1s1 + 8 + 764, 764);

            if ((err = m_digest.Parse(&stream)) != SUCCESS) {
                return ERRORWRAP(err, "parse the c1 digest");
            }
        }

        return err;
    }

    error c1s1StrategySchema0::CopyTo(c1s1 *owner, char *bytes, int size, bool with_digest)
    {
        error err = SUCCESS;

        if (with_digest) {
            Assert(size == 1536);
        } else {
            Assert(size == 1504);
        }

        Buffer stream(bytes, size);

        CopyTimeVersion(&stream, owner);
        CopyKey(&stream);
        CopyDigest(&stream, with_digest);

        Assert(stream.Empty());

        return err;
    }

    c1s1StrategySchema1::c1s1StrategySchema1()
    {

    }

    c1s1StrategySchema1::~c1s1StrategySchema1()
    {

    }

    schema_type c1s1StrategySchema1::Schema()
    {
        return schema1;
    }

    error c1s1StrategySchema1::Parse(char *_c1s1, int size)
    {
        error err = SUCCESS;

        Assert(size == 1536);

        if (true) {
            Buffer stream(_c1s1 + 8, 764);

            if ((err = m_digest.Parse(&stream)) != SUCCESS) {
                return ERRORWRAP(err, "parse c1 digest");
            }
        }

        if (true) {
            Buffer stream(_c1s1 + 8 + 764, 764);

            if ((err = m_key.Parse(&stream)) != SUCCESS) {
                return ERRORWRAP(err, "parse c1 key");
            }
        }

        return err;
    }

    error c1s1StrategySchema1::CopyTo(c1s1 *owner, char *bytes, int size, bool with_digest)
    {
        error err = SUCCESS;

        if (with_digest) {
            Assert(size == 1536);
        } else {
            Assert(size == 1504);
        }

        Buffer stream(bytes, size);

        CopyTimeVersion(&stream, owner);
        CopyDigest(&stream, with_digest);
        CopyKey(&stream);

        Assert(stream.Empty());

        return err;
    }

    c1s1::c1s1()
    {
        m_payload = NULL;
        m_version = 0;
        m_time = 0;
    }

    c1s1::~c1s1()
    {
        Freep(m_payload);
    }

    schema_type c1s1::Schema()
    {
        Assert(m_payload != NULL);
        return m_payload->Schema();
    }

    char *c1s1::GetDigest()
    {
        Assert(m_payload != NULL);
        return m_payload->GetDigest();
    }

    char *c1s1::GetKey()
    {
        Assert(m_payload != NULL);
        return m_payload->GetKey();
    }

    error c1s1::Dump(char *_c1s1, int size)
    {
        Assert(size == 1536);
        Assert(m_payload != NULL);
        return m_payload->Dump(this, _c1s1, size);
    }

    error c1s1::Parse(char *_c1s1, int size, schema_type schema)
    {
        Assert(size == 1536);

        if (schema != schema0 && schema != schema1) {
            return ERRORNEW(ERROR_RTMP_CH_SCHEMA, "parse c1 failed. invalid schema=%d", schema);
        }

        Buffer stream(_c1s1, size);

        m_time = stream.Read4Bytes();
        m_version = stream.Read4Bytes(); // client c1 version

        Freep(m_payload);
        if (schema == schema0) {
            m_payload = new c1s1StrategySchema0();
        } else {
            m_payload = new c1s1StrategySchema1();
        }

        return m_payload->Parse(_c1s1, size);
    }

    error c1s1::C1Create(schema_type schema)
    {
        if (schema != schema0 && schema != schema1) {
            return ERRORNEW(ERROR_RTMP_CH_SCHEMA, "create c1 failed. invalid schema=%d", schema);
        }

        // client c1 time and version
        m_time = (int32_t)::time(NULL);
        m_version = 0x80000702; // client c1 version

        // generate signature by schema
        Freep(m_payload);
        if (schema == schema0) {
            m_payload = new c1s1StrategySchema0();
        } else {
            m_payload = new c1s1StrategySchema1();
        }

        return m_payload->C1Create(this);
    }

    error c1s1::C1ValidateDigest(bool &is_valid)
    {
        is_valid = false;
        Assert(m_payload);
        return m_payload->C1ValidateDigest(this, is_valid);
    }

    error c1s1::S1Create(c1s1 *c1)
    {
        if (c1->Schema() != schema0 && c1->Schema() != schema1) {
            return ERRORNEW(ERROR_RTMP_CH_SCHEMA, "create s1 failed. invalid schema=%d", c1->Schema());
        }

        m_time = ::time(NULL);
        m_version = 0x01000504; // server s1 version

        Freep(m_payload);
        if (c1->Schema() == schema0) {
            m_payload = new c1s1StrategySchema0();
        } else {
            m_payload = new c1s1StrategySchema1();
        }

        return m_payload->S1Create(this, c1);
    }

    error c1s1::S1ValidateDigest(bool &is_valid)
    {
        is_valid = false;
        Assert(m_payload);
        return m_payload->S1ValidateDigest(this, is_valid);
    }

    c2s2::c2s2()
    {
        RandomGenerate(m_random, 1504);

        int size = snprintf(m_random, 1504, "%s", RTMP_SIG_SRS_HANDSHAKE);
        Assert(size > 0 && size < 1504);
        snprintf(m_random + 1504 - size, size, "%s", RTMP_SIG_SRS_HANDSHAKE);

        RandomGenerate(m_digest, 32);
    }

    c2s2::~c2s2()
    {

    }

    error c2s2::Dump(char *_c2s2, int size)
    {
        Assert(size == 1536);

        memcpy(_c2s2, m_random, 1504);
        memcpy(_c2s2 + 1504, m_digest, 32);

        return SUCCESS;
    }

    error c2s2::Parse(char *_c2s2, int size)
    {
        Assert(size == 1536);

        memcpy(m_random, _c2s2, 1504);
        memcpy(m_digest, _c2s2 + 1504, 32);

        return SUCCESS;
    }

    error c2s2::C2Create(c1s1 *s1)
    {
        error err = SUCCESS;

        char temp_key[OpensslHashSize];
        if ((err = OpensslHMACsha256(GenuineFPKey, 62, s1->GetDigest(), 32, temp_key)) != SUCCESS) {
            return ERRORWRAP(err, "create c2 temp key");
        }

        char _digest[OpensslHashSize];
        if ((err = OpensslHMACsha256(temp_key, 32, m_random, 1504, _digest)) != SUCCESS) {
            return ERRORWRAP(err, "create c2 digest");
        }

        memcpy(m_digest, _digest, 32);

        return err;
    }

    error c2s2::C2Validate(c1s1 *s1, bool &is_valid)
    {
        is_valid = false;
        error err = SUCCESS;

        char temp_key[OpensslHashSize];
        if ((err = OpensslHMACsha256(GenuineFPKey, 62, s1->GetDigest(), 32, temp_key)) != SUCCESS) {
            return ERRORWRAP(err, "create c2 temp key");
        }

        char _digest[OpensslHashSize];
        if ((err = OpensslHMACsha256(temp_key, 32, m_random, 1504, _digest)) != SUCCESS) {
            return ERRORWRAP(err, "create c2 digest");
        }

        is_valid = BytesEquals(m_digest, _digest, 32);

        return err;
    }

    error c2s2::S2Create(c1s1 *c1)
    {
        error err = SUCCESS;

        char temp_key[OpensslHashSize];
        if ((err = OpensslHMACsha256(GenuineFMSKey, 68, c1->GetDigest(), 32, temp_key)) != SUCCESS) {
            return ERRORWRAP(err, "create s2 temp key");
        }

        char _digest[OpensslHashSize];
        if ((err = OpensslHMACsha256(temp_key, 32, m_random, 1504, _digest)) != SUCCESS) {
            return ERRORWRAP(err, "create s2 digest");
        }

        memcpy(m_digest, _digest, 32);

        return err;
    }

    error c2s2::S2Validate(c1s1 *c1, bool &is_valid)
    {
        is_valid = false;
        error err = SUCCESS;

        char temp_key[OpensslHashSize];
        if ((err = OpensslHMACsha256(GenuineFMSKey, 68, c1->GetDigest(), 32, temp_key)) != SUCCESS) {
            return ERRORWRAP(err, "create s2 temp key");
        }

        char _digest[OpensslHashSize];
        if ((err = OpensslHMACsha256(temp_key, 32, m_random, 1504, _digest)) != SUCCESS) {
            return ERRORWRAP(err, "create s2 digest");
        }

        is_valid = BytesEquals(m_digest, _digest, 32);

        return err;
    }
}


SimpleHandshake::SimpleHandshake()
{

}

SimpleHandshake::~SimpleHandshake()
{

}

error SimpleHandshake::HandshakeWithClient(HandshakeBytes *hs_bytes, IProtocolReadWriter *io)
{
    error err = SUCCESS;

    ssize_t nsize;

    if ((err = hs_bytes->ReadC0c1(io)) != SUCCESS) {
        return ERRORWRAP(err, "read c0c1");
    }

    // plain text required.
    if (hs_bytes->m_c0c1[0] != 0x03) {
        return ERRORNEW(ERROR_RTMP_PLAIN_REQUIRED, "only support rtmp plain text, version=%X", (uint8_t)hs_bytes->m_c0c1[0]);
    }

    if ((err = hs_bytes->CreateS0s1s2(hs_bytes->m_c0c1 + 1)) != SUCCESS) {
        return ERRORWRAP(err, "create s0s1s2");
    }

    if ((err = io->Write(hs_bytes->m_s0s1s2, 3073, &nsize)) != SUCCESS) {
        return ERRORWRAP(err, "write s0s1s2");
    }

    if ((err = hs_bytes->ReadC2(io)) != SUCCESS) {
        return ERRORWRAP(err, "read c2");
    }

    trace("simple handshake success.");

    return err;
}

error SimpleHandshake::HandshakeWithServer(HandshakeBytes *hs_bytes, IProtocolReadWriter *io)
{
    error err = SUCCESS;

    ssize_t nsize;

    // simple handshake
    if ((err = hs_bytes->CreateC0c1()) != SUCCESS) {
        return ERRORWRAP(err, "create c0c1");
    }

    if ((err = io->Write(hs_bytes->m_c0c1, 1537, &nsize)) != SUCCESS) {
        return ERRORWRAP(err, "write c0c1");
    }

    if ((err = hs_bytes->ReadS0s1s2(io)) != SUCCESS) {
        return ERRORWRAP(err, "read s0s1s2");
    }

    // plain text required.
    if (hs_bytes->m_s0s1s2[0] != 0x03) {
        return ERRORNEW(ERROR_RTMP_HANDSHAKE, "handshake failed, plain text required, version=%X", (uint8_t)hs_bytes->m_s0s1s2[0]);
    }

    if ((err = hs_bytes->CreateC2()) != SUCCESS) {
        return ERRORWRAP(err, "create c2");
    }

    // for simple handshake, copy s1 to c2.
    // @see https://github.com/ossrs/srs/issues/418
    memcpy(hs_bytes->m_c2, hs_bytes->m_s0s1s2 + 1, 1536);

    if ((err = io->Write(hs_bytes->m_c2, 1536, &nsize)) != SUCCESS) {
        return ERRORWRAP(err, "write c2");
    }

    trace("simple handshake success.");

    return err;
}

ComplexHandshake::ComplexHandshake()
{

}

ComplexHandshake::~ComplexHandshake()
{

}

error ComplexHandshake::HandshakeWithClient(HandshakeBytes *hs_bytes, IProtocolReadWriter *io)
{
    error err = SUCCESS;

    ssize_t nsize;

    if ((err = hs_bytes->ReadC0c1(io)) != SUCCESS) {
        return ERRORWRAP(err, "read c0c1");
    }

    // decode c1
    c1s1 c1;
    // try schema0.
    // @remark, use schema0 to make flash player happy.
    if ((err = c1.Parse(hs_bytes->m_c0c1 + 1, 1536, schema0)) != SUCCESS) {
        return ERRORWRAP(err, "parse c1, schema=%d", schema0);
    }
    // try schema1
    bool is_valid = false;
    if ((err = c1.C1ValidateDigest(is_valid)) != SUCCESS || !is_valid) {
        Freep(err);

        if ((err = c1.Parse(hs_bytes->m_c0c1 + 1, 1536, schema1)) != SUCCESS) {
            return ERRORWRAP(err, "parse c0c1, schame=%d", schema1);
        }

        if ((err = c1.C1ValidateDigest(is_valid)) != SUCCESS || !is_valid) {
            Freep(err);
            return ERRORNEW(ERROR_RTMP_TRY_SIMPLE_HS, "all schema valid failed, try simple handshake");
        }
    }

    // encode s1
    c1s1 s1;
    if ((err = s1.S1Create(&c1)) != SUCCESS) {
        return ERRORWRAP(err, "create s1 from c1");
    }
    // verify s1
    if ((err = s1.S1ValidateDigest(is_valid)) != SUCCESS || !is_valid) {
        Freep(err);
        return ERRORNEW(ERROR_RTMP_TRY_SIMPLE_HS, "verify s1 failed, try simple handshake");
    }

    c2s2 s2;
    if ((err = s2.S2Create(&c1)) != SUCCESS) {
        return ERRORWRAP(err, "create s2 from c1");
    }
    // verify s2
    if ((err = s2.S2Validate(&c1, is_valid)) != SUCCESS || !is_valid) {
        Freep(err);
        return ERRORNEW(ERROR_RTMP_TRY_SIMPLE_HS, "verify s2 failed, try simple handshake");
    }

    // sendout s0s1s2
    if ((err = hs_bytes->CreateS0s1s2()) != SUCCESS) {
        return ERRORWRAP(err, "create s0s1s2");
    }
    if ((err = s1.Dump(hs_bytes->m_s0s1s2 + 1, 1536)) != SUCCESS) {
        return ERRORWRAP(err, "dump s1");
    }
    if ((err = s2.Dump(hs_bytes->m_s0s1s2 + 1537, 1536)) != SUCCESS) {
        return ERRORWRAP(err, "dump s2");
    }
    if ((err = io->Write(hs_bytes->m_s0s1s2, 3073, &nsize)) != SUCCESS) {
        return ERRORWRAP(err, "write s0s1s2");
    }

    // recv c2
    if ((err = hs_bytes->ReadC2(io)) != SUCCESS) {
        return ERRORWRAP(err, "read c2");
    }
    c2s2 c2;
    if ((err = c2.Parse(hs_bytes->m_c2, 1536)) != SUCCESS) {
        return ERRORWRAP(err, "parse c2");
    }

    // verify c2
    // never verify c2, for ffmpeg will failed.
    // it's ok for flash.

    trace("complex handshake success");

    return err;
}

error ComplexHandshake::HandshakeWithServer(HandshakeBytes *hs_bytes, IProtocolReadWriter *io)
{
    error err = SUCCESS;

    ssize_t nsize;

    // complex handshake
    if ((err = hs_bytes->CreateC0c1()) != SUCCESS) {
        return ERRORWRAP(err, "create c0c1");
    }

    // sign c1
    c1s1 c1;
    // @remark, FMS requires the schema1(digest-key), or connect failed.
    if ((err = c1.C1Create(schema1)) != SUCCESS) {
        return ERRORWRAP(err, "create c1");
    }
    if ((err = c1.Dump(hs_bytes->m_c0c1 + 1, 1536)) != SUCCESS) {
        return ERRORWRAP(err, "dump c1");
    }

    // verify c1
    bool is_valid;
    if ((err = c1.C1ValidateDigest(is_valid)) != SUCCESS || !is_valid) {
        Freep(err);
        return ERRORNEW(ERROR_RTMP_TRY_SIMPLE_HS, "try simple handshake");
    }

    if ((err = io->Write(hs_bytes->m_c0c1, 1537, &nsize)) != SUCCESS) {
        return ERRORWRAP(err, "write c0c1");
    }

    // s0s1s2
    if ((err = hs_bytes->ReadS0s1s2(io)) != SUCCESS) {
        return ERRORWRAP(err, "read s0s1s2");
    }

    // plain text required.
    if (hs_bytes->m_s0s1s2[0] != 0x03) {
        return ERRORNEW(ERROR_RTMP_HANDSHAKE,  "handshake failed, plain text required, version=%X", (uint8_t)hs_bytes->m_s0s1s2[0]);
    }

    // verify s1s2
    c1s1 s1;
    if ((err = s1.Parse(hs_bytes->m_s0s1s2 + 1, 1536, c1.Schema())) != SUCCESS) {
        return ERRORWRAP(err, "parse s1");
    }

    // never verify the s1,
    // for if forward to nginx-rtmp, verify s1 will failed,
    // TODO: FIXME: find the handshake schema of nginx-rtmp.

    // c2
    if ((err = hs_bytes->CreateC2()) != SUCCESS) {
        return ERRORWRAP(err, "create c2");
    }

    c2s2 c2;
    if ((err = c2.C2Create(&s1)) != SUCCESS) {
        return ERRORWRAP(err, "create c2");
    }

    if ((err = c2.Dump(hs_bytes->m_c2, 1536)) != SUCCESS) {
        return ERRORWRAP(err, "dump c2");
    }
    if ((err = io->Write(hs_bytes->m_c2, 1536, &nsize)) != SUCCESS) {
        return ERRORWRAP(err, "write c2");
    }

    trace("complex handshake success.");

    return err;
}
