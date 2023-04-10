#ifndef PROTOCOL_RTMP_HANDSHAK_H
#define PROTOCOL_RTMP_HANDSHAK_H


#include "log.h"

class IProtocolReadWriter;
class ComplexHandshake;
class HandshakeBytes;
class Buffer;

// For openssl.
#include <openssl/hmac.h>

// @see https://wiki.openssl.org/index.php/OpenSSL_1.1.0_Changes
#if OPENSSL_VERSION_NUMBER < 0x10100000L
extern HMAC_CTX *HMAC_CTX_new(void);
extern void HMAC_CTX_free(HMAC_CTX *ctx);
#endif

namespace internal
{
    // The digest key generate size.
    #define OpensslHashSize 512
    extern uint8_t GenuineFMSKey[];
    extern uint8_t GenuineFPKey[];
    error OpensslHMACsha256(const void* key, int key_size, const void* data, int data_size, void* digest);
    error OpensslGenerateKey(char* public_key, int32_t size);

    // The DH wrapper.
    class SDH
    {
    private:
        DH* m_pdh;
    public:
        SDH();
        virtual ~SDH();
    private:
        virtual void Close();
    public:
        // Initialize dh, generate the public and private key.
        // @param ensure_128bytes_public_key whether ensure public key is 128bytes,
        //       sometimes openssl generate 127bytes public key.
        //       default to false to donot ensure.
        virtual error Initialize(bool ensure_128bytes_public_key = false);
        // Copy the public key.
        // @param pkey the bytes to copy the public key.
        // @param pkey_size the max public key size, output the actual public key size.
        //       user should never ignore this size.
        // @remark, when ensure_128bytes_public_key, the size always 128.
        virtual error CopyPublicKey(char* pkey, int32_t& pkey_size);
        // Generate and copy the shared key.
        // Generate the shared key with peer public key.
        // @param ppkey peer public key.
        // @param ppkey_size the size of ppkey.
        // @param skey the computed shared key.
        // @param skey_size the max shared key size, output the actual shared key size.
        //       user should never ignore this size.
        virtual error CopySharedKey(const char* ppkey, int32_t ppkey_size, char* skey, int32_t& skey_size);
    private:
        virtual error DoInitialize();
    };
    // The schema type.
    enum schema_type
    {
        schema_invalid = 2,

        // The key-digest sequence
        schema0 = 0,

        // The digest-key sequence
        // @remark, FMS requires the schema1(digest-key), or connect failed.
        //
        schema1 = 1,
    };

    // The 764bytes key structure
    //     random-data: (offset)bytes
    //     key-data: 128bytes
    //     random-data: (764-offset-128-4)bytes
    //     offset: 4bytes
    // @see also: http://blog.csdn.net/win_lin/article/details/13006803
    class KeyBlock
    {
    public:
        // (offset)bytes
        char* m_random0;
        int m_random0Size;

        // 128bytes
        char m_key[128];

        // (764-offset-128-4)bytes
        char* m_random1;
        int m_random1Size;

        // 4bytes
        int32_t m_offset;
    public:
        KeyBlock();
        virtual ~KeyBlock();
    public:
        // Parse key block from c1s1.
        // if created, user must free it by srs_key_block_free
        // @stream contains c1s1_key_bytes the key start bytes
        error Parse(Buffer* stream);
    private:
        // Calculate the offset of key,
        // The key->offset cannot be used as the offset of key.
        int CalcValidOffset();
    };

    // The 764bytes digest structure
    //     offset: 4bytes
    //     random-data: (offset)bytes
    //     digest-data: 32bytes
    //     random-data: (764-4-offset-32)bytes
    // @see also: http://blog.csdn.net/win_lin/article/details/13006803
    class DigestBlock
    {
    public:
        // 4bytes
        int32_t m_offset;

        // (offset)bytes
        char* m_random0;
        int m_random0Size;

        // 32bytes
        char m_digest[32];

        // (764-4-offset-32)bytes
        char* m_random1;
        int m_random1Size;
    public:
        DigestBlock();
        virtual ~DigestBlock();
    public:
        // Parse digest block from c1s1.
        // if created, user must free it by srs_digest_block_free
        // @stream contains c1s1_digest_bytes the digest start bytes
        error Parse(Buffer* stream);
    private:
        // Calculate the offset of digest,
        // The key->offset cannot be used as the offset of digest.
        int CalcValidOffset();
    };

    class c1s1;

    // The c1s1 strategy, use schema0 or schema1.
    // The template method class to defines common behaviors,
    // while the concrete class to implements in schema0 or schema1.
    class c1s1Strategy
    {
    protected:
        KeyBlock m_key;
        DigestBlock m_digest;
    public:
        c1s1Strategy();
        virtual ~c1s1Strategy();
    public:
        // Get the scema.
        virtual schema_type Schema() = 0;
        // Get the digest.
        virtual char* GetDigest();
        // Get the key.
        virtual char* GetKey();
        // Copy to bytes.
        // @param size must be 1536.
        virtual error Dump(c1s1* owner, char* _c1s1, int size);
        // For server: parse the c1s1, discovery the key and digest by schema.
        // use the c1_validate_digest() to valid the digest of c1.
        virtual error Parse(char* _c1s1, int size) = 0;
    public:
        // For client: create and sign c1 by schema.
        // sign the c1, generate the digest.
        //         calc_c1_digest(c1, schema) {
        //            get c1s1-joined from c1 by specified schema
        //            digest-data = HMACsha256(c1s1-joined, FPKey, 30)
        //            return digest-data;
        //        }
        //        random fill 1536bytes c1 // also fill the c1-128bytes-key
        //        time = time() // c1[0-3]
        //        version = [0x80, 0x00, 0x07, 0x02] // c1[4-7]
        //        schema = choose schema0 or schema1
        //        digest-data = calc_c1_digest(c1, schema)
        //        copy digest-data to c1
        virtual error C1Create(c1s1* owner);
        // For server:  validate the parsed c1 schema
        virtual error C1ValidateDigest(c1s1* owner, bool& is_valid);
        // For server:  create and sign the s1 from c1.
        //       // decode c1 try schema0 then schema1
        //       c1-digest-data = get-c1-digest-data(schema0)
        //       if c1-digest-data equals to calc_c1_digest(c1, schema0) {
        //           c1-key-data = get-c1-key-data(schema0)
        //           schema = schema0
        //       } else {
        //           c1-digest-data = get-c1-digest-data(schema1)
        //           if c1-digest-data not equals to calc_c1_digest(c1, schema1) {
        //               switch to simple handshake.
        //               return
        //           }
        //           c1-key-data = get-c1-key-data(schema1)
        //           schema = schema1
        //       }
        //
        //       // Generate s1
        //       random fill 1536bytes s1
        //       time = time() // c1[0-3]
        //       version = [0x04, 0x05, 0x00, 0x01] // s1[4-7]
        //       s1-key-data=shared_key=DH_compute_key(peer_pub_key=c1-key-data)
        //       get c1s1-joined by specified schema
        //       s1-digest-data = HMACsha256(c1s1-joined, FMSKey, 36)
        //       copy s1-digest-data and s1-key-data to s1.
        // @param c1, to get the peer_pub_key of client.
        virtual error S1Create(c1s1* owner, c1s1* c1);
        // For server:  validate the parsed s1 schema
        virtual error S1ValidateDigest(c1s1* owner, bool& is_valid);
    public:
        // Calculate the digest for c1
        virtual error CalcC1Digest(c1s1* owner, char*& c1_digest);
        // Calculate the digest for s1
        virtual error CalcS1Digest(c1s1* owner, char*& s1_digest);
        // Copy whole c1s1 to bytes.
        // @param size must always be 1536 with digest, and 1504 without digest.
        virtual error CopyTo(c1s1* owner, char* bytes, int size, bool with_digest) = 0;
        // Copy time and version to stream.
        virtual void CopyTimeVersion(Buffer* stream, c1s1* owner);
        // Copy key to stream.
        virtual void CopyKey(Buffer* stream);
        // Copy digest to stream.
        virtual void CopyDigest(Buffer* stream, bool with_digest);
    };

    // The c1s1 schema0
    //     key: 764bytes
    //     digest: 764bytes
    class c1s1StrategySchema0 : public c1s1Strategy
    {
    public:
        c1s1StrategySchema0();
        virtual ~c1s1StrategySchema0();
    public:
        virtual schema_type Schema();
        virtual error Parse(char* _c1s1, int size);
    public:
        virtual error CopyTo(c1s1* owner, char* bytes, int size, bool with_digest);
    };

    // The c1s1 schema1
    //     digest: 764bytes
    //     key: 764bytes
    class c1s1StrategySchema1 : public c1s1Strategy
    {
    public:
        c1s1StrategySchema1();
        virtual ~c1s1StrategySchema1();
    public:
        virtual schema_type Schema();
        virtual error Parse(char* _c1s1, int size);
    public:
        virtual error CopyTo(c1s1* owner, char* bytes, int size, bool with_digest);
    };

    // The c1s1 schema0
    //     time: 4bytes
    //     version: 4bytes
    //     key: 764bytes
    //     digest: 764bytes
    // The c1s1 schema1
    //     time: 4bytes
    //     version: 4bytes
    //     digest: 764bytes
    //     key: 764bytes
    // @see also: http://blog.csdn.net/win_lin/article/details/13006803
    class c1s1
    {
    public:
        // 4bytes
        int32_t m_time;
        // 4bytes
        int32_t m_version;
        // 764bytes+764bytes
        c1s1Strategy* m_payload;
    public:
        c1s1();
        virtual ~c1s1();
    public:
        // Get the scema.
        virtual schema_type Schema();
        // Get the digest key.
        virtual char* GetDigest();
        // Get the key.
        virtual char* GetKey();
    public:
        // Copy to bytes.
        // @param size, must always be 1536.
        virtual error Dump(char* _c1s1, int size);
        // For server:  parse the c1s1, discovery the key and digest by schema.
        // @param size, must always be 1536.
        // use the c1_validate_digest() to valid the digest of c1.
        // use the s1_validate_digest() to valid the digest of s1.
        virtual error Parse(char* _c1s1, int size, schema_type schema);
    public:
        // For client:  create and sign c1 by schema.
        // sign the c1, generate the digest.
        //         calc_c1_digest(c1, schema) {
        //            get c1s1-joined from c1 by specified schema
        //            digest-data = HMACsha256(c1s1-joined, FPKey, 30)
        //            return digest-data;
        //        }
        //        random fill 1536bytes c1 // also fill the c1-128bytes-key
        //        time = time() // c1[0-3]
        //        version = [0x80, 0x00, 0x07, 0x02] // c1[4-7]
        //        schema = choose schema0 or schema1
        //        digest-data = calc_c1_digest(c1, schema)
        //        copy digest-data to c1
        virtual error C1Create(schema_type schema);
        // For server:  validate the parsed c1 schema
        virtual error C1ValidateDigest(bool& is_valid);
    public:
        // For server:  create and sign the s1 from c1.
        //       // decode c1 try schema0 then schema1
        //       c1-digest-data = get-c1-digest-data(schema0)
        //       if c1-digest-data equals to calc_c1_digest(c1, schema0) {
        //           c1-key-data = get-c1-key-data(schema0)
        //           schema = schema0
        //       } else {
        //           c1-digest-data = get-c1-digest-data(schema1)
        //           if c1-digest-data not equals to calc_c1_digest(c1, schema1) {
        //               switch to simple handshake.
        //               return
        //           }
        //           c1-key-data = get-c1-key-data(schema1)
        //           schema = schema1
        //       }
        //
        //       // Generate s1
        //       random fill 1536bytes s1
        //       time = time() // c1[0-3]
        //       version = [0x04, 0x05, 0x00, 0x01] // s1[4-7]
        //       s1-key-data=shared_key=DH_compute_key(peer_pub_key=c1-key-data)
        //       get c1s1-joined by specified schema
        //       s1-digest-data = HMACsha256(c1s1-joined, FMSKey, 36)
        //       copy s1-digest-data and s1-key-data to s1.
        virtual error S1Create(c1s1* c1);
        // For server:  validate the parsed s1 schema
        virtual error S1ValidateDigest(bool& is_valid);
    };

    // The c2s2 complex handshake structure.
    // random-data: 1504bytes
    // digest-data: 32bytes
    // @see also: http://blog.csdn.net/win_lin/article/details/13006803
    class c2s2
    {
    public:
        char m_random[1504];
        char m_digest[32];
    public:
        c2s2();
        virtual ~c2s2();
    public:
        // Copy to bytes.
        // @param size, must always be 1536.
        virtual error Dump(char* _c2s2, int size);
        // Parse the c2s2
        // @param size, must always be 1536.
        virtual error Parse(char* _c2s2, int size);
    public:
        // Create c2.
        // random fill c2s2 1536 bytes
        //
        // // client generate C2, or server valid C2
        // temp-key = HMACsha256(s1-digest, FPKey, 62)
        // c2-digest-data = HMACsha256(c2-random-data, temp-key, 32)
        virtual error C2Create(c1s1* s1);

        // Validate the c2 from client.
        virtual error C2Validate(c1s1* s1, bool& is_valid);
    public:
        // Create s2.
        // random fill c2s2 1536 bytes
        //
        // For server generate S2, or client valid S2
        // temp-key = HMACsha256(c1-digest, FMSKey, 68)
        // s2-digest-data = HMACsha256(s2-random-data, temp-key, 32)
        virtual error S2Create(c1s1* c1);

        // Validate the s2 from server.
        virtual error S2Validate(c1s1* c1, bool& is_valid);
    };
}

// Simple handshake.
// user can try complex handshake first,
// rollback to simple handshake if error ERROR_RTMP_TRY_SIMPLE_HS
class SimpleHandshake
{
public:
    SimpleHandshake();
    virtual ~SimpleHandshake();
public:
    // Simple handshake.
    virtual error HandshakeWithClient(HandshakeBytes* hs_bytes, IProtocolReadWriter* io);
    virtual error HandshakeWithServer(HandshakeBytes* hs_bytes, IProtocolReadWriter* io);
};

// Complex handshake,
// @see also crtmp(crtmpserver) or librtmp,
// @see also: http://blog.csdn.net/win_lin/article/details/13006803
class ComplexHandshake
{
public:
    ComplexHandshake();
    virtual ~ComplexHandshake();
public:
    // Complex hanshake.
    // @return user must:
    //     continue connect app if success,
    //     try simple handshake if error is ERROR_RTMP_TRY_SIMPLE_HS,
    //     otherwise, disconnect
    virtual error HandshakeWithClient(HandshakeBytes* hs_bytes, IProtocolReadWriter* io);
    virtual error HandshakeWithServer(HandshakeBytes* hs_bytes, IProtocolReadWriter* io);
};

#endif // PROTOCOL_RTMP_HANDSHAK_H
