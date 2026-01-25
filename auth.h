#include "openssl/crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEY_LEN 256
#define KEY_LEN_BYTES (256 / 8)
#define SECRET_KEY_LOCATION "./client_secret.key"


// Hexbuf is buffer to decode , output will be places in out buffer ,
// out_len is expected byte length of the hexbuffer to be decoded
// Note: incorrect out_len will result in failure to decode buffer
int decode_buf_from_hex(const char *hexbuf, unsigned char *out, size_t out_len);

//This is the initialiser that will be called on the server an does all the
// neccessarey things needed for auth
// It does the following things:
  // The server secret key is read from file and
  // loaded into memory once at the server boot ,
  // the secret key is in hex , and is first loaded in memory
  // and then we try to deocde hex ,
  // and then finally the hex is dumped inside global buffer
int init_auth();


// The method shall return 0 on sucessfull auth , >0 otherwise
//This method can be called to authenticate
// the user against the given request key which is in hex
int authenticate(const char *request_key_hex); 
