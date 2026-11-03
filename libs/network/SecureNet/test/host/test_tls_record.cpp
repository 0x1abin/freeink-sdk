#include <cassert>
#include <cstdio>
#include <functional>
#include <string>
#define private public
#include <SecureClient.h>
#undef private
#include SDK_CPP
#include <SecureHttpClient.h>

static bool failReserve = false;
static bool requireWrittenRequest = false;
extern "C" void* __real_malloc(size_t);
extern "C" void* __wrap_malloc(size_t n) {
  if (n == freeink::kRecordBufSize) {
    if (requireWrittenRequest) assert(fakeWroteRequest);
    if (failReserve) return nullptr;
  }
  return __real_malloc(n);
}

static void connect(freeink::SecureClient& c) {
  FakeNet::replies().push_back("open");
  assert(c.connect("catalog.test", 443));
}

int main() {
  using namespace freeink;
  {
    SecureClient idle;
    idle.reserveFullRecord();
    assert(!idle._recordBuf && !g_recordBuf);
  }
  {
    SecureClient c;
    connect(c);
    fakeNegotiatedFragment = 2048;
    static_cast<WOLFSSL*>(c._ssl)->max_fragment = 2048;
    c.reserveFullRecord();
    assert(!c._recordBuf && !g_recordBuf);
    c.stop();
    fakeNegotiatedFragment = MAX_RECORD_SIZE;
  }
  {
    SecureClient c;
    connect(c);
    failReserve = true;
    c.reserveFullRecord();
    assert(!c._recordBuf && !g_recordBuf && c.connected());
    failReserve = false;
    c.reserveFullRecord();
    void* const buffer = c._recordBuf;
    assert(buffer && g_recordBuf == buffer);
    c.reserveFullRecord();
    assert(c._recordBuf == buffer);
    for (size_t size : {1025u, 2048u, 16384u, 16704u}) {
      void* p = fakeMalloc(size);
      assert(p == buffer && g_recordBufLent);
      void* second = fakeMalloc(size);
      assert(second && second != buffer);
      fakeFree(second);
      fakeFree(p);
      assert(!g_recordBufLent);
    }
    for (size_t size : {512u, 1024u, 16705u}) {
      void* p = fakeMalloc(size);
      assert(p && p != buffer && !g_recordBufLent);
      fakeFree(p);
    }
    fakeTask = reinterpret_cast<void*>(2);
    void* other = fakeMalloc(2048);
    assert(other && other != buffer && !g_recordBufLent);
    fakeFree(other);
    fakeTask = reinterpret_cast<void*>(1);
    void* p = fakeMalloc(2048);
    std::memset(p, 0x5a, kRecordBufSize);
    assert(fakeRealloc(p, 4096) == buffer && g_recordBufLent);
    void* grown = fakeRealloc(p, kRecordBufSize + 1);
    assert(grown && grown != buffer && !g_recordBufLent);
    for (size_t i = 0; i < kRecordBufSize; ++i) assert(static_cast<unsigned char*>(grown)[i] == 0x5a);
    fakeFree(grown);
    SecureClient second;
    connect(second);
    second.reserveFullRecord();
    assert(!second._recordBuf && g_recordBuf == buffer);
    second.stop();
    assert(g_recordBuf == buffer);
    // wolfSSL returns its input record during stop before the owner frees it.
    static_cast<WOLFSSL*>(c._ssl)->input = fakeMalloc(2048);
    assert(g_recordBufLent);
    c.stop();
    assert(!c._recordBuf && !g_recordBuf && !g_recordBufLent);
    c.stop();
  }
  {
    SecureClient c;
    c.setAbortCallback([] { return true; });
    assert(!c.connect("cancel.test", 443) && c.aborted());
    c.reserveFullRecord();
    assert(!c._recordBuf && !g_recordBuf);
    c.setAbortCallback(nullptr);
    fakeHandshakeFails = true;
    FakeNet::replies().push_back("auto");
    FakeNet::replies().push_back("retry");
    assert(!c.connect("fail.test", 443) && !c.aborted());
    assert(!c._ssl && !c._recordBuf && !g_recordBuf);
    fakeHandshakeFails = false;
  }
  for (int transfer = 0; transfer < 3; ++transfer) {
    fakeWroteRequest = false;
    fakeRequest.clear();
    FakeNet::replies().push_back("open");
    SecureHttpClient http;
    assert(http.begin("https://catalog.test/book"));
    assert(http.connect());  // optional handshake-before-large-headers API
    http.addHeader("X-Catalog", std::string(4096, 'x'));
    http.setReserveFullRecord(true);
    requireWrittenRequest = true;
    assert(http.GET() == 200 && http.responseComplete() && http.getString() == "book");
    assert(fakeRequest.find("X-Catalog: " + std::string(4096, 'x') + "\r\n") != std::string::npos);
    assert(g_recordBuf);
    http.end();
    assert(!g_recordBuf && !g_recordBufLent);
    requireWrittenRequest = false;
  }
  std::puts("PASS TLS record ownership, sizes, MFL, OOM, realloc, stop/cancel/failure and repeated real HTTP ordering");
}
