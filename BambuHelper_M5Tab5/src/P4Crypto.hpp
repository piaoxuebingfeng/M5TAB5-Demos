// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#if defined(ESP_PLATFORM) || defined(BAMBU_CAMERA_MBEDTLS)
#if defined(ESP_PLATFORM)
#include <esp_random.h>
inline void bambu_fill_random(void* out,size_t n){esp_fill_random(out,n);}
#else
#include <cstdlib>
inline void bambu_fill_random(void* out,size_t n){arc4random_buf(out,n);}
#endif
#include <mbedtls/aes.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>
#include <cstring>
using EVP_MD=mbedtls_md_info_t;
constexpr size_t EVP_MAX_MD_SIZE=64;
inline const EVP_MD* EVP_sha256(){return mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);}
inline const EVP_MD* EVP_sha384(){return mbedtls_md_info_from_type(MBEDTLS_MD_SHA384);}
inline size_t EVP_MD_size(const EVP_MD* md){return mbedtls_md_get_size(md);}
inline unsigned char* HMAC(const EVP_MD* md,const void* key,int keylen,const unsigned char* data,size_t len,unsigned char* out,unsigned int* olen){
  if(mbedtls_md_hmac(md,static_cast<const unsigned char*>(key),keylen,data,len,out)!=0)return nullptr;
  *olen=mbedtls_md_get_size(md);return out;
}
inline unsigned char* SHA256(const unsigned char* p,size_t n,unsigned char* out){return mbedtls_md(EVP_sha256(),p,n,out)==0?out:nullptr;}
inline unsigned char* SHA384(const unsigned char* p,size_t n,unsigned char* out){return mbedtls_md(EVP_sha384(),p,n,out)==0?out:nullptr;}
inline int RAND_bytes(unsigned char* out,int len){if(len<0)return 0;bambu_fill_random(out,len);return 1;}
inline int CRYPTO_memcmp(const void* a,const void* b,size_t n){
  const auto* x=static_cast<const unsigned char*>(a);const auto* y=static_cast<const unsigned char*>(b);volatile unsigned char d=0;
  for(size_t i=0;i<n;++i)d=static_cast<unsigned char>(d|(x[i]^y[i]));return d;
}
inline void OPENSSL_cleanse(void* p,size_t n){mbedtls_platform_zeroize(p,n);}
inline int bambu_rng(void*,unsigned char* p,size_t n){bambu_fill_random(p,n);return 0;}
inline bool bambu_cbc(bool encrypt,const uint8_t* key,const uint8_t* iv,const uint8_t* in,size_t n,uint8_t* out){
  if(!n||n%16)return false;
  mbedtls_aes_context ctx;mbedtls_aes_init(&ctx);uint8_t copy[16];memcpy(copy,iv,16);
  int rc=encrypt?mbedtls_aes_setkey_enc(&ctx,key,256):mbedtls_aes_setkey_dec(&ctx,key,256);
  if(rc==0)rc=mbedtls_aes_crypt_cbc(&ctx,encrypt?MBEDTLS_AES_ENCRYPT:MBEDTLS_AES_DECRYPT,n,copy,in,out);
  mbedtls_aes_free(&ctx);return rc==0;
}
inline bool bambu_x25519(const uint8_t* peer,uint8_t* pub,uint8_t* shared){
  mbedtls_ecdh_context ctx;mbedtls_ecdh_init(&ctx);size_t used=0;uint8_t local[33],remote[33];
  remote[0]=32;memcpy(remote+1,peer,32);
  int rc=mbedtls_ecdh_setup(&ctx,MBEDTLS_ECP_DP_CURVE25519);
  // make/read_public use the TLS ECPoint vector: length byte + 32 bytes.
  if(rc==0)rc=mbedtls_ecdh_make_public(&ctx,&used,local,sizeof(local),bambu_rng,nullptr);
  if(rc==0&&(used!=33||local[0]!=32))rc=-1;
  if(rc==0)memcpy(pub,local+1,32);
  if(rc==0)rc=mbedtls_ecdh_read_public(&ctx,remote,sizeof(remote));
  if(rc==0)rc=mbedtls_ecdh_calc_secret(&ctx,&used,shared,32,bambu_rng,nullptr);
  mbedtls_ecdh_free(&ctx);return rc==0&&used==32;
}
#endif
