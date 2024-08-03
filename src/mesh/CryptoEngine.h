#pragma once

#include "concurrency/LockGuard.h"
#include <Arduino.h>

extern concurrency::Lock *cryptLock;

struct CryptoKey {
    uint8_t bytes[32];

    /// # of bytes, or -1 to mean "invalid key - do not use"
    int8_t length;
};

/**
 * see docs/software/crypto.md for details.
 *
 */

#define MAX_BLOCKSIZE 256

class CryptoEngine
{
  protected:
    /** Our per packet nonce */
    uint8_t nonce[16] = {0};

    CryptoKey key = {};

  public:
    virtual ~CryptoEngine() {}

    /**
     * Set the key used for encrypt, decrypt.
     *
     * As a special case: If all bytes are zero, we assume _no encryption_ and send all data in cleartext.
     *
     * @param numBytes must be 16 (AES128), 32 (AES256) or 0 (no crypt)
     * @param bytes a _static_ buffer that will remain valid for the life of this crypto instance (i.e. this class will cache the
     * provided pointer)
     */
    virtual void setKey(const CryptoKey &k);

    /**
     * Encrypt a packet
     *
     * @param bytes is updated in place
     */
    virtual void encrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes);
    virtual void decrypt(uint32_t fromNode, uint64_t packetId, size_t numBytes, uint8_t *bytes);

    /**
     * Perform a naive "hashcash" function.
     *
     * @param seed Initial seed string to be hashed.
     * @param numZeros Number of trailing zeros required in the hash.
     * @return The counter value when the required hash is found.
     */
    virtual int performHashcash(const char* seed, int numZeros, int counter = 0, int maxIterations = 0);

    virtual void calculateHash(unsigned char* hash, const char* seed, int counter);

    String getHashString(const char* seed, int counter)
    {
      unsigned char hash[32];
      char hashString[65];  // 64 hex digits + null terminator

      calculateHash(hash, seed, counter);

      // Convert hash to a hex string
      for (int i = 0; i < 32; i++) {
        sprintf(&hashString[i * 2], "%02x", hash[i]);
      }

      return String(hashString);
    }

    bool checkHashcash(const char* seed, int counter, int numZeros) {
      unsigned char hash[32];
      calculateHash(hash, seed, counter);
      return hashEndsWithZeros(hash, numZeros);
    }

  protected:
    /**
     * Init our 128 bit nonce for a new packet
     *
     * The NONCE is constructed by concatenating (from MSB to LSB):
     * a 64 bit packet number (stored in little endian order)
     * a 32 bit sending node number (stored in little endian order)
     * a 32 bit block counter (starts at zero)
     */
    void initNonce(uint32_t fromNode, uint64_t packetId);

    // Utility function to check if the hash ends with the desired number of trailing zero bits
    bool hashEndsWithZeros(const unsigned char* hash, int numTrailingBits)
    {
        int numBytes = numTrailingBits / 8;
        int numBits = numTrailingBits % 8;

        // Check full zero bytes
        for (int i = 31; i >= 32 - numBytes; i--) {
            if (hash[i] != 0) {
                return false;
            }
        }

        // Check remaining bits
        if (numBits > 0) {
            unsigned char mask = (1 << numBits) - 1;
            if ((hash[31 - numBytes] & mask) != 0) {
                return false;
            }
        }

        return true;
    }
};

extern CryptoEngine *crypto;
