#pragma once

#include <psa/crypto.h>

namespace freeink {
namespace content {
// IDF6/TF-PSA-Crypto exports RSA keys as PKCS#1 DER. The caller retains the
// existing SPKI / PKCS#8 wrapping. Default key lifetime is volatile; no NVS key
// or persistent key identifier is created. Buffers are the existing caller's
// 192/1024-byte scratch, with no additional allocation in this helper.
inline psa_status_t generatePsaRsaPkcs1(uint8_t* publicDer, size_t publicCap, size_t* publicLen, uint8_t* privateDer,
                                        size_t privateCap, size_t* privateLen) {
  *publicLen = 0;
  *privateLen = 0;
  psa_status_t status = psa_crypto_init();
  if (status != PSA_SUCCESS) return status;
  psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attributes, PSA_KEY_TYPE_RSA_KEY_PAIR);
  psa_set_key_bits(&attributes, 1024);
  psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_EXPORT);
  mbedtls_svc_key_id_t key = 0;
  status = psa_generate_key(&attributes, &key);
  psa_reset_key_attributes(&attributes);
  if (status != PSA_SUCCESS) return status;
  status = psa_export_public_key(key, publicDer, publicCap, publicLen);
  if (status == PSA_SUCCESS) status = psa_export_key(key, privateDer, privateCap, privateLen);
  const psa_status_t cleanup = psa_destroy_key(key);
  if (status == PSA_SUCCESS) status = cleanup;
  if (status != PSA_SUCCESS) {
    *publicLen = 0;
    *privateLen = 0;
  }
  return status;
}
}  // namespace content
}  // namespace freeink
