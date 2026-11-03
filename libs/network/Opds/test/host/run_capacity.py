#!/usr/bin/env python3
"""Exercise production capacity helpers with the actual OpdsEntry layout."""
from pathlib import Path
import os
import subprocess
import tempfile

LIB = Path(__file__).resolve().parents[2]
source = (LIB / "src/OpdsParser.cpp").read_text()
helpers = source[source.index("namespace {"):source.index("OpdsParser::OpdsParser()")]
with tempfile.TemporaryDirectory(prefix="opds-capacity-") as directory:
    root = Path(directory)
    unit = root / "capacity.cpp"
    unit.write_text("""#include <OpdsEntry.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
struct FakeEsp { size_t block=0; size_t getMaxAllocHeap() const { return block; } } ESP;
#define LOG_ERR(...) ((void)0)
using namespace OpdsLimits;
""" + helpers + """
int main() {
  for (size_t slots=0; slots<=80; ++slots) {
    ESP.block=4096+slots*sizeof(OpdsEntry);
    std::vector<OpdsEntry> entries;
    size_t limit=reserveFeedEntries(entries);
    if (slots<8) { assert(limit==0 && entries.capacity()==0); continue; }
    const size_t wanted=std::min(slots,ENTRY_STORAGE_CAPACITY);
    assert(entries.capacity()==wanted && limit==wanted-(ENTRY_STORAGE_CAPACITY-MAX_ENTRIES));
    const OpdsEntry* const storage=entries.data();
    for(size_t i=0;i<limit;++i) entries.push_back(OpdsEntry{});
    assert(entries.data()==storage && entries.size()==limit);
    assert(ESP.block-entries.capacity()*sizeof(OpdsEntry)>=4096);
  }
  for (size_t block : {0u, 1u, 4095u, 4096u}) {
    ESP.block=block;
    std::vector<OpdsEntry> entries;
    assert(reserveFeedEntries(entries)==0 && entries.capacity()==0);
  }
  for(size_t slots=0;slots<=30;++slots) {
    ESP.block=4096+slots*sizeof(OpdsEntry);
    std::vector<OpdsEntry> facets;
    reserveFacetEntries(facets);
    assert(facets.capacity()==std::min(slots,MAX_FACET_ENTRIES));
  }
  std::puts("PASS OPDS capacity: below-minimum failure, exact headroom, bounded entries/facets and no vector growth");
}
""")
    executable = root / "capacity"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-fno-exceptions", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(LIB / "include"), str(unit), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
