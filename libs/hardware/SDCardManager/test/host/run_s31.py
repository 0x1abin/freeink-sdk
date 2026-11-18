#!/usr/bin/env python3
"""Exercise production SDMMC allocation, host configuration and block I/O on S31/S3."""
from pathlib import Path
import os, subprocess, tempfile
HERE=Path(__file__).resolve().parent
LIB=HERE.parents[1]
with tempfile.TemporaryDirectory(prefix='freeink-s31-sd-') as tmp:
    p=Path(tmp)
    (p/'driver').mkdir(); (p/'esp_private').mkdir()
    (p/'BoardConfig.h').write_text(r"""#pragma once
#include <cstdint>
#define FREEINK_SD_SDMMC 1
namespace BoardConfig {
struct SdmmcPins { int8_t clk,cmd,d0,d1,d2,d3; uint8_t busWidth; };
struct Profile { struct { int8_t powerEnable=-1; bool powerCycleOnMount=false,powerActiveHigh=false; } sd; };
inline Profile ACTIVE;
}
""")
    (p/'SdFat.h').write_text(r"""#pragma once
#include <cstddef>
#include <cstdint>
using Sector_t=uint32_t;
struct FsBlockDeviceInterface {
 virtual ~FsBlockDeviceInterface()=default;
 virtual void end()=0; virtual bool isBusy()=0;
 virtual bool readSector(Sector_t,uint8_t*)=0; virtual bool readSectors(Sector_t,uint8_t*,size_t)=0;
 virtual bool writeSector(Sector_t,const uint8_t*)=0; virtual bool writeSectors(Sector_t,const uint8_t*,size_t)=0;
 virtual Sector_t sectorCount()=0; virtual bool syncDevice()=0;
};
""")
    (p/'native.h').write_text(r"""#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <initializer_list>
using gpio_num_t=int;
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1,SDMMC_FREQ_DEFAULT=20000,SDMMC_FREQ_HIGHSPEED=40000;
constexpr int SDMMC_SLOT_FLAG_INTERNAL_PULLUP=1,MALLOC_CAP_DMA=1,MALLOC_CAP_8BIT=2,MALLOC_CAP_INTERNAL=4,SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF=8;
struct SerialStub { explicit operator bool() { return false; } template<class...T> void printf(T...) {} };
inline SerialStub Serial;
constexpr int HIGH=1,LOW=0,OUTPUT=1;
inline unsigned long millis() { return 0; }
inline void delay(int) {}
inline void pinMode(int,int) {}
inline void digitalWrite(int,int) {}
inline void gpio_hold_dis(int) {}
inline void gpio_pullup_en(int) {}
inline const char* esp_err_to_name(int) { return "failure"; }
extern int failure,hostInits,hostEnds,frees,reads,writes;
extern void* dma;
extern size_t allocAlignment,allocBytes;
extern unsigned allocCaps;
struct sdmmc_host_t { int slot,max_freq_khz=0,flags=0;
#if FREEINK_MCU_S31
 bool (*check_buffer_alignment)(int,const void*,size_t)=nullptr;
 void* dma_aligned_buffer=nullptr;
 size_t unaligned_multi_block_rw_max_chunk_size=0;
#endif
};
inline sdmmc_host_t defaultHost() { sdmmc_host_t h{}; h.slot=FREEINK_MCU_S31?0:1; return h; }
#define SDMMC_HOST_DEFAULT() defaultHost()
struct sdmmc_slot_config_t { int width=0,clk=-1,cmd=-1,d0=-1,d1=-1,d2=-1,d3=-1,flags=0; };
#define SDMMC_SLOT_CONFIG_DEFAULT() sdmmc_slot_config_t{}
struct sdmmc_card_t { struct { unsigned capacity=0; } csd; sdmmc_host_t host; };
inline int sdmmc_host_init() { ++hostInits; return failure==1?ESP_FAIL:ESP_OK; }
inline void sdmmc_host_deinit() { ++hostEnds; }
int sdmmc_host_init_slot(int,const sdmmc_slot_config_t*);
int sdmmc_card_init(const sdmmc_host_t*,sdmmc_card_t*);
int sdmmc_read_sectors(sdmmc_card_t*,void*,unsigned,size_t);
int sdmmc_write_sectors(sdmmc_card_t*,const void*,unsigned,size_t);
inline bool esp_ptr_internal(const void* p) { return p==dma; }
inline bool sdmmc_host_check_buffer_alignment(int slot,const void* p,size_t n) { return slot==0 && uintptr_t(p)%64==0 && n%64==0; }
inline int esp_cache_get_alignment(unsigned caps,size_t* out) { assert(caps&MALLOC_CAP_INTERNAL); *out=64; return failure==5?ESP_FAIL:ESP_OK; }
inline void* heap_caps_aligned_alloc(size_t a,size_t n,unsigned caps) { allocAlignment=a; allocBytes=n; allocCaps=caps; if(failure==6) return nullptr; assert(!dma); assert(!posix_memalign(&dma,a,n)); return dma; }
inline void* heap_caps_malloc(size_t n,unsigned caps) { allocBytes=n; allocCaps=caps; if(failure==6) return nullptr; assert(!dma); dma=malloc(n); return dma; }
inline void heap_caps_free(void* p) { if(p) { assert(p==dma); ++frees; free(p); dma=nullptr; } }
""")
    for n in ['Arduino.h','esp_heap_caps.h','esp_memory_utils.h','sdmmc_cmd.h','driver/gpio.h','driver/sdmmc_host.h','esp_private/esp_cache_private.h']:
        (p/n).write_text('#include "native.h"\n')
    for target in [1,0]:
        cmd=[os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
             '-DFREEINK_MCU_S31='+str(target),'-DFREEINK_DEVICE_METALIO_EINK4_PLUS='+str(target),'-I'+str(p),'-I'+str(LIB/'src'),str(HERE/'test_s31.cpp'),str(LIB/'src/SdmmcBlockDevice.cpp'),'-o',str(p/'test')]
        subprocess.run(cmd,check=True); subprocess.run([str(p/'test')],check=True)
