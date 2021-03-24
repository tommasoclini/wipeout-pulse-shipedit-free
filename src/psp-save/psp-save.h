#pragma once

#include <stdio.h>
#include <stdlib.h>

FILE *try_open(const char *name, const char *mode);
int filesize(FILE *fp);
void die(const char *format, ...);
void decrypt_file(FILE *in, FILE *out, unsigned char *key, unsigned int mode);
int decrypt_data(unsigned int mode, unsigned char *data, int *len,
  int *aligned_len, unsigned char *key);
/* name is usually the final component of the output name, e.g. "DATA.BIN" */
void encrypt_file(FILE *in, FILE *out, const char *name, FILE *sfo_in,
  FILE *sfo_out, unsigned char *key, unsigned int mode);
int encrypt_data(unsigned int mode, unsigned char *data, int *dataLen,
  int *alignedLen, unsigned char *hash, unsigned char *cryptkey);
