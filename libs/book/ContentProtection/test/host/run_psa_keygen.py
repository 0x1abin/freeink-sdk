#!/usr/bin/env python3
"""Exercise production PSA key export/cleanup; this does not simulate RSA mathematics."""
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
LIB = HERE.parents[1]
assert (LIB / "src/EspPsaRsaKeygen.h").is_file(), "IDF6 PSA RSA export helper is not implemented"
with tempfile.TemporaryDirectory(prefix="freeink-psa-keygen-") as tmp:
    root = Path(tmp)
    (root / "psa").mkdir()
    (root / "psa/crypto.h").write_text('''#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
using psa_status_t=int;
using mbedtls_svc_key_id_t=unsigned;
constexpr int PSA_SUCCESS=0, PSA_ERROR_GENERIC_ERROR=-1;
constexpr unsigned PSA_KEY_TYPE_RSA_KEY_PAIR=1, PSA_KEY_USAGE_EXPORT=2;
struct psa_key_attributes_t { unsigned type=0,bits=0,usage=0; };
#define PSA_KEY_ATTRIBUTES_INIT {}
inline int stage=0, destroyed=0, generated=0;
inline bool exportedPublic=false, exportedPrivate=false;
inline psa_status_t psa_crypto_init(){ return stage==1 ? -1 : 0; }
inline void psa_set_key_type(psa_key_attributes_t* a,unsigned v){a->type=v;}
inline void psa_set_key_bits(psa_key_attributes_t* a,unsigned v){a->bits=v;}
inline void psa_set_key_usage_flags(psa_key_attributes_t* a,unsigned v){a->usage=v;}
inline void psa_reset_key_attributes(psa_key_attributes_t*){}
inline psa_status_t psa_generate_key(const psa_key_attributes_t* a,mbedtls_svc_key_id_t* key){
 if(a->type!=PSA_KEY_TYPE_RSA_KEY_PAIR || a->bits!=1024 || a->usage!=PSA_KEY_USAGE_EXPORT)return -1;
 if(stage==2)return -1; *key=17; ++generated; return 0;
}
inline psa_status_t psa_export_public_key(mbedtls_svc_key_id_t key,uint8_t* out,size_t cap,size_t* len){
 if(key!=17||cap<3||stage==3)return -1;out[0]=0x30;out[1]=1;out[2]=2;*len=3;exportedPublic=true;return 0;
}
inline psa_status_t psa_export_key(mbedtls_svc_key_id_t key,uint8_t* out,size_t cap,size_t* len){
 if(key!=17||cap<3||stage==4)return -1;out[0]=0x30;out[1]=1;out[2]=3;*len=3;exportedPrivate=true;return 0;
}
inline psa_status_t psa_destroy_key(mbedtls_svc_key_id_t key){if(key!=17)return -1;++destroyed;return stage==5?-1:0;}
''')
    source = root / "test.cpp"
    source.write_text('''#include <EspPsaRsaKeygen.h>
#include <cassert>
#include <cstdio>
int main(){
 for(stage=0;stage<=5;++stage){
  uint8_t pub[192]={},priv[1024]={};size_t pubLen=99,privLen=99;
  destroyed=generated=0;exportedPublic=exportedPrivate=false;
  const int result=freeink::content::generatePsaRsaPkcs1(pub,sizeof(pub),&pubLen,priv,sizeof(priv),&privLen);
  assert((result==PSA_SUCCESS)==(stage==0));
  assert(destroyed==(stage==1||stage==2 ? 0:1));
  if(stage==0){assert(pubLen==3&&privLen==3&&pub[2]==2&&priv[2]==3);}
  else{assert(pubLen==0&&privLen==0);}
 }
 std::puts("PSA RSA export/cleanup success and five failure stages: passed");
}
''')
    exe = root / "test"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I"+str(root),
                    "-I"+str(LIB / "src"), str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
