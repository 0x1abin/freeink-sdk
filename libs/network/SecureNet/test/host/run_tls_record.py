#!/usr/bin/env python3
"""Compile the real TLS/HTTP implementation against deterministic wolfSSL I/O.

The double models allocator callbacks and negotiated MFL, not cryptography or
radio timing. It checks ownership, lifetime, request ordering and failure paths.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
LIB = HERE.parents[1]
with tempfile.TemporaryDirectory(prefix="tls-record-") as directory:
    root = Path(directory)
    shutil.copytree(HERE / "stubs", root, dirs_exist_ok=True)
    arduino = root / "Arduino.h"
    arduino.write_text(arduino.read_text().replace("class IPAddress {};", "class IPAddress { public: unsigned operator[](int) const { return 127; } };") + """
struct FakeSerial { explicit operator bool() const { return false; }
 template<class... T> void printf(const char*, T...) {} void println(const char*) {} };
inline FakeSerial Serial;
struct FakeEsp { unsigned getFreeHeap() const { return 100000; } unsigned getMaxAllocHeap() const { return 50000; } };
inline FakeEsp ESP;
""")
    client = root / "Client.h"
    client.write_text(client.read_text().replace("void setTimeout(unsigned long) {}", "void setTimeout(unsigned long n) { timeout=n; } unsigned long getTimeout() const { return timeout; } unsigned long timeout=1000;"))
    for folder in ("freertos", "wolfssl/wolfcrypt"):
        (root / folder).mkdir(parents=True, exist_ok=True)
    (root / "freertos/FreeRTOS.h").write_text("#pragma once\nusing TaskHandle_t = void*;\n")
    (root / "freertos/task.h").write_text("#pragma once\n#include <freertos/FreeRTOS.h>\ninline TaskHandle_t fakeTask = reinterpret_cast<void*>(1);\ninline TaskHandle_t xTaskGetCurrentTaskHandle() { return fakeTask; }\n")
    (root / "wolfssl/internal.h").write_text("#pragma once\n#include <wolfssl/ssl.h>\n")
    (root / "wolfssl/wolfcrypt/memory.h").write_text("#pragma once\n#include <wolfssl/ssl.h>\n")
    (root / "wolfssl/ssl.h").write_text(r"""#pragma once
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
constexpr int MAX_RECORD_SIZE=16384, WOLFSSL_SUCCESS=1, WOLFSSL_VERIFY_NONE=0,
 WOLFSSL_FILETYPE_PEM=1, WOLFSSL_SNI_HOST_NAME=0, WOLFSSL_MFL_2_11=2,
 WOLFSSL_ERROR_WANT_READ=2, WOLFSSL_ERROR_WANT_WRITE=3, WOLFSSL_ERROR_ZERO_RETURN=6,
 WOLFSSL_CBIO_ERR_CONN_CLOSE=-5, WOLFSSL_CBIO_ERR_WANT_READ=-2, WOLFSSL_CBIO_ERR_WANT_WRITE=-2;
struct WOLFSSL_METHOD {};
struct WOLFSSL_CTX {};
struct WOLFSSL { int max_fragment=MAX_RECORD_SIZE; void* input=nullptr; size_t cursor=0; };
inline void* (*fakeMalloc)(size_t)=std::malloc;
inline void (*fakeFree)(void*)=std::free;
inline void* (*fakeRealloc)(void*,size_t)=std::realloc;
inline bool fakeWroteRequest=false, fakeHandshakeFails=false;
inline int fakeNegotiatedFragment=MAX_RECORD_SIZE;
inline std::string fakeRequest;
inline const std::string fakeResponse="HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nbook";
inline int wolfSSL_SetAllocators(void*(*a)(size_t),void(*f)(void*),void*(*r)(void*,size_t)) { fakeMalloc=a; fakeFree=f; fakeRealloc=r; return 0; }
inline WOLFSSL_METHOD* wolfSSLv23_client_method() { static WOLFSSL_METHOD m; return &m; }
inline WOLFSSL_METHOD* wolfTLSv1_2_client_method() { return wolfSSLv23_client_method(); }
inline WOLFSSL_CTX* wolfSSL_CTX_new(WOLFSSL_METHOD*) { return static_cast<WOLFSSL_CTX*>(std::calloc(1,sizeof(WOLFSSL_CTX))); }
inline void wolfSSL_CTX_free(WOLFSSL_CTX* p) { std::free(p); }
inline WOLFSSL* wolfSSL_new(WOLFSSL_CTX*) { auto* p=static_cast<WOLFSSL*>(std::calloc(1,sizeof(WOLFSSL))); p->max_fragment=fakeNegotiatedFragment; return p; }
inline void wolfSSL_free(WOLFSSL* p) { if(p->input) fakeFree(p->input); std::free(p); }
inline void wolfSSL_CTX_set_verify(WOLFSSL_CTX*,int,void*) {}
inline void wolfSSL_CTX_load_verify_buffer(WOLFSSL_CTX*,const unsigned char*,size_t,int) {}
template<class T> inline void wolfSSL_SetIORecv(WOLFSSL_CTX*,T) {}
template<class T> inline void wolfSSL_SetIOSend(WOLFSSL_CTX*,T) {}
inline void wolfSSL_SetIOReadCtx(WOLFSSL*,void*) {}
inline void wolfSSL_SetIOWriteCtx(WOLFSSL*,void*) {}
inline void wolfSSL_UseSNI(WOLFSSL*,int,const char*,size_t) {}
inline void wolfSSL_UseMaxFragment(WOLFSSL*,int) {}
inline int wolfSSL_connect(WOLFSSL*) { return fakeHandshakeFails ? -1 : 1; }
inline int wolfSSL_get_error(WOLFSSL*,int) { return -125; }
inline const char* wolfSSL_get_version(WOLFSSL*) { return "TLS double"; }
inline const char* wolfSSL_get_cipher(WOLFSSL*) { return "double"; }
inline int wolfSSL_write(WOLFSSL*,const void* p,size_t n) { fakeWroteRequest=true; fakeRequest.append(static_cast<const char*>(p),n); return static_cast<int>(n); }
inline int wolfSSL_read(WOLFSSL* p,void* b,size_t n) { n=std::min(n,fakeResponse.size()-p->cursor); std::memcpy(b,fakeResponse.data()+p->cursor,n);p->cursor+=n;return static_cast<int>(n); }
inline int wolfSSL_pending(WOLFSSL* p) { return static_cast<int>(fakeResponse.size()-p->cursor); }
""")
    executable = root / "tls-record"
    command = [os.environ.get("CXX", "c++"), "-std=c++17", "-fno-exceptions", "-Wall", "-Wextra", "-Werror",
               "-DFREEINK_NET_WOLFSSL=1", "-DHAVE_MAX_FRAGMENT=1", "-I" + str(root), "-I" + str(LIB / "include"),
               '-DSDK_CPP="' + str(LIB / "src/SecureClient.cpp") + '"', str(HERE / "test_tls_record.cpp"),
               "-Wl,--wrap=malloc", "-o", str(executable)]
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
