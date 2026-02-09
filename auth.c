#include "auth.h"
#include "openssl/crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// TOOD: CLeanup for header file
#define KEY_LEN 256
#define KEY_LEN_BYTES (256 / 8)
#define SECRET_KEY_LOCATION "./client_secret.key"
#define DEBUG

// Load server secret key

unsigned char secret_key_buffer[KEY_LEN_BYTES];

// Hexbuf is buffer to decode , output will be places in out buffer ,
// out_len is expected byte length of the hexbuffer to be decoded
// This function is helper which decodes the given hex buffer from hex
// the out_len is the expected byte length of hex to be decoded
int decode_buf_from_hex(const char *hexbuf, unsigned char *out, size_t out_len) {
    if (!hexbuf || !out) {
        fprintf(stderr, "Error: Could not decode hex buffer: Buffer is null\n");
        return 1;
    }

    size_t len = strlen(hexbuf);

    // Sanitize input of hexbuffer
    // without mdoifying buffer in place
    if (len > 0 && hexbuf[len - 1] == '\n')
        len--;

    // Hex covers 4 bits and one char covers one byte ,
    // so we get a conversion factor of 2
    if (len != out_len * 2)
        return 1;

    // out_len is in bytes , same logic conversion factor of 2
    // and one byte for null terminator
    char tmp_hex[out_len * 2 + 1];
    memcpy(tmp_hex, hexbuf, len);
    tmp_hex[len] = '\0';

    // OPENSSL moifies the decoded output length in place
    size_t decoded_len = 0;
    unsigned char *tmp = OPENSSL_hexstr2buf(tmp_hex, (long *)&decoded_len);

    // Post decode sanity check
    if (!tmp || decoded_len != out_len) {
        OPENSSL_free(tmp);
        return 1;
    }

    // The output will be in out_buffer given to the function
    memcpy(out, tmp, out_len);

    // Teardown
    OPENSSL_cleanse(tmp, decoded_len);
    OPENSSL_free(tmp);

    return 0;
}

int init_auth() {

    // The server secret key is read from file and
    // loaded into memory once at the server boot ,
    // the secret key is in hex , and is first loaded in memory
    // and then we try to deocde hex ,
    // and then finally the hex is dumped inside global buffer

    FILE *fd = fopen(SECRET_KEY_LOCATION, "rb");

    if (!fd) {
        // TODO: handle file permissions gracefully
        perror("Critical: The client auth key could not be loaded\n");
        return 1;
    }

    // Gets the size of file contents
    fseek(fd, 0, SEEK_END);
    long size = ftell(fd);
    rewind(fd);

    size_t expected_hex_len = KEY_LEN_BYTES * 2;

    // If key isnt 256 bit , we exit
    if (size != (long)expected_hex_len && size != (long)expected_hex_len + 1) {
        fprintf(stderr, "Invlaid hex key size: %ld (expected %d)\n", size, KEY_LEN_BYTES);
        fclose(fd);
        return 1;
    }

    // populate hex from file contents buffer
    char hexbuf[KEY_LEN_BYTES * 2 + 2] = {0};

    if (fread(hexbuf, 1, size, fd) != (size_t)size) {
        fclose(fd);
        return 1;
    }
    // We are done with file atp
    fclose(fd);

    if (decode_buf_from_hex(hexbuf, secret_key_buffer, KEY_LEN_BYTES)) {
        fprintf(stderr, "Key decode failed\n");
        return 1;
    }

    fprintf(stderr, "The client auth key was succesfully loaded\n");
    return 0;
}

// Actual auth checker , checks in constant time
int authenticate(const char *request_key_hex) {

    unsigned const char *expected_key = secret_key_buffer;

    if (!request_key_hex) {
        fprintf(stderr, "The request auth key is null\n");
        return 1;
    }

    unsigned char req_key[KEY_LEN_BYTES];

    if (decode_buf_from_hex(request_key_hex, req_key, KEY_LEN_BYTES)) {
        fprintf(stderr, "Middleware: Authentication Error\n");
        return 1;
    }

    return CRYPTO_memcmp(expected_key, req_key, KEY_LEN_BYTES);
}

// int main(int argc, char *argv[]) {

//     if (init_auth()) {
//         fprintf(stderr, "There was an error loading the auth key\n");
//         return 1;
//     }

//     if (authenticate("hjkl")) {
//         fprintf(stderr, "Auth failed: Refusing client handshake\n");
//         return 1;
//     } else {
//         fprintf(stderr, "Client authenticated\n");
//     }

//     return 0;
// }
