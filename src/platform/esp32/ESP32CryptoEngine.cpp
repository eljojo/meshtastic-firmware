#include "CryptoEngine.h"
#include "configuration.h"

#include "mbedtls/aes.h"
#include "mbedtls/md.h"

class ESP32CryptoEngine : public CryptoEngine
{

    mbedtls_aes_context aes;
    mbedtls_md_context_t md_ctx;

  public:
    ESP32CryptoEngine() {
      mbedtls_aes_init(&aes);

      // TODO: add if NARA
      mbedtls_md_init(&md_ctx);
      const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
      mbedtls_md_setup(&md_ctx, md_info, 0);
    }

    ~ESP32CryptoEngine() {
      mbedtls_aes_free(&aes);

      // TODO: add if NARA
      mbedtls_md_free(&md_ctx);
    }

    /**
     * Set the key used for encrypt, decrypt.
     *
     * As a special case: If all bytes are zero, we assume _no encryption_ and send all data in cleartext.
     *
     * @param numBytes must be 16 (AES128), 32 (AES256) or 0 (no crypt)
     * @param bytes a _static_ buffer that will remain valid for the life of this crypto instance (i.e. this class will cache the
     * provided pointer)
     */
    virtual void setKey(const CryptoKey &k) override
    {
        CryptoEngine::setKey(k);

        if (key.length != 0) {
            auto res = mbedtls_aes_setkey_enc(&aes, key.bytes, key.length * 8);
            assert(!res);
        }
    }

    /**
     * Encrypt a packet
     *
     * @param bytes is updated in place
     */
    virtual void encrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes) override
    {
        if (key.length > 0) {
            LOG_DEBUG("ESP32 crypt fr=%x, num=%x, numBytes=%d!\n", fromNode, (uint32_t)packetId, numBytes);
            initNonce(fromNode, packetId);
            if (numBytes <= MAX_BLOCKSIZE) {
                static uint8_t scratch[MAX_BLOCKSIZE];
                uint8_t stream_block[16];
                size_t nc_off = 0;
                memcpy(scratch, bytes, numBytes);
                memset(scratch + numBytes, 0,
                       sizeof(scratch) - numBytes); // Fill rest of buffer with zero (in case cypher looks at it)

                auto res = mbedtls_aes_crypt_ctr(&aes, numBytes, &nc_off, nonce, stream_block, scratch, bytes);
                assert(!res);
            } else {
                LOG_ERROR("Packet too large for crypto engine: %d. noop encryption!\n", numBytes);
            }
        }
    }

    virtual void decrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes) override
    {
        // For CTR, the implementation is the same
        encrypt(fromNode, packetId, numBytes, bytes);
    }

    /**
     * Perform a naive "hashcash" function.
     *
     * @param seed Initial seed string to be hashed.
     * @param numZeros Number of trailing zeros required in the hash.
     * @return The counter value when the required hash is found.
     */
    int performHashcash(const char* seed, int numZeros, int counter, int maxIterations)
    {
      char input[128];
      unsigned char hash[32];
      int numTrailingBits = numZeros * 4;  // Since each hex digit represents 4 bits
      int iterations = 0;

      LOG_INFO("Searching for hash for seed: \"%s\"\n", seed);

      while (true) {
        // Create input string
        sprintf(input, "%s%d", seed, counter);

        // Compute SHA-256 hash
        mbedtls_md_starts(&md_ctx);
        mbedtls_md_update(&md_ctx, (const unsigned char*)input, strlen(input));
        mbedtls_md_finish(&md_ctx, hash);

        // Check if hash ends with the required number of zeros
        if (hashEndsWithZeros(hash, numTrailingBits)) {
          char hashString[65];  // 64 hex digits + null terminator
          // Convert hash to a hex string
          for (int i = 0; i < 32; i++) {
            sprintf(&hashString[i * 2], "%02x", hash[i]);
          }

          LOG_INFO("Proof found! input: \"%s\", Hash: %s\n", input, hashString);
          return counter;
        }

        counter++;
        iterations++;

        if (maxIterations > 0 && iterations >= maxIterations) {
          LOG_INFO("No hash found in %d iterations, counter = %ld\n", maxIterations, counter);
          return 0;
        }
      }
    }

    void calculateHash(unsigned char* hash, const char* seed, int counter)
    {
      char input[128];
      //unsigned char hash[32];

      // Create input string
      sprintf(input, "%s%d", seed, counter);

      // Compute SHA-256 hash
      mbedtls_md_starts(&md_ctx);
      mbedtls_md_update(&md_ctx, (const unsigned char*)input, strlen(input));
      mbedtls_md_finish(&md_ctx, hash);
    }

};

CryptoEngine *crypto = new ESP32CryptoEngine();
