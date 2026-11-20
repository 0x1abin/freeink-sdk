#!/usr/bin/env python3
"""Compile the real S31 driver against recording EPDiy calls and a fallible heap."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
LIB = HERE.parents[1]
with tempfile.TemporaryDirectory(prefix='freeink-metalio-plus-') as directory:
    root = Path(directory)
    for folder in ('src/driver', 'src/bus', 'include'):
        (root / folder).mkdir(parents=True)
    for name in ('EpdiyDriver.cpp', 'EpdiyDriver.h', 'PanelDriver.h'):
        shutil.copy2(LIB / 'src/driver' / name, root / 'src/driver' / name)
    for name in ('GrayscaleCapabilities.h', 'RefreshContext.h'):
        shutil.copy2(LIB / 'include' / name, root / 'include' / name)
    for name, dest in (('Arduino.h', 'Arduino.h'), ('EpdBus.h', 'src/bus/EpdBus.h')):
        shutil.copy2(HERE / 'pro_stubs' / name, root / dest)
    (root / 'BoardConfig.h').write_text('''#pragma once
#include <cstdint>
namespace BoardConfig {
struct Profile {uint16_t displayWidth=1216, displayHeight=684;};
inline constexpr Profile ACTIVE;
}
''')
    (root / 'esp_heap_caps.h').write_text('''#pragma once
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <unordered_set>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_8BIT 4
inline int failAllocation=-1;
inline std::unordered_set<void*> allocations;
inline void* heap_caps_aligned_alloc(size_t align,size_t size,int) {
  if(failAllocation==0)return nullptr;
  if(failAllocation>0)--failAllocation;
  void* p=nullptr;
  if(posix_memalign(&p,align<sizeof(void*)?sizeof(void*):align,size))return nullptr;
  allocations.insert(p);return p;
}
inline void* heap_caps_malloc(size_t size,int cap){return heap_caps_aligned_alloc(8,size,cap);}
inline void* heap_caps_calloc(size_t n,size_t size,int cap){
  void* p=heap_caps_malloc(n*size,cap);if(p)std::memset(p,0,n*size);return p;
}
inline void heap_caps_free(void* p){if(p){assert(allocations.erase(p)==1);std::free(p);}}
''')
    (root / 'esp_log.h').write_text('#pragma once\n#define ESP_LOGE(...) ((void)0)\n#define ESP_LOGI(tag, ...) ((void)(tag), (void)std::printf(__VA_ARGS__))\n')
    (root / 'epdiy.h').write_text('''#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
struct EpdBoardDefinition {};
struct EpdWaveform {};
struct EpdDisplay_t {const EpdWaveform* default_waveform;};
enum EpdInitOptions {EPD_LUT_64K};
enum EpdDrawMode {MODE_DU,MODE_GC16,MODE_GL16};
enum EpdDrawError {EPD_DRAW_SUCCESS=0,EPD_DRAW_EMPTY_LINE_QUEUE=1,EPD_DRAW_FAILED=2};
inline constexpr int EPD_ROT_LANDSCAPE=1;
struct EpdiyHighlevelState {
 uint8_t *front_fb,*back_fb,*difference_fb;bool* dirty_lines;uint8_t* dirty_columns;const EpdWaveform* waveform;
};
inline std::string events;
inline bool drawOk=true, powerReady=true;
inline EpdDrawMode lastMode=MODE_DU;
inline int inits=0, draws=0;
inline uint8_t lastGray[8];
inline void epd_init(const EpdBoardDefinition*,const EpdDisplay_t*,EpdInitOptions){++inits;}
inline void epd_set_vcom(uint16_t){}
inline void epd_set_rotation(int){}
inline void epd_set_leading_skip(bool){}
inline int epd_last_leading_skip(){return 0;}
inline void epd_hl_set_all_white(EpdiyHighlevelState* s){std::memset(s->front_fb,0xff,415872);}
inline void epd_hl_waveform(EpdiyHighlevelState* s,const EpdWaveform* w){s->waveform=w;}
inline void epd_poweron(){events+='+';}
inline void epd_poweroff(){events+='-';}
inline float epd_ambient_temperature(){return 25;}
inline uint8_t* epd_hl_get_framebuffer(EpdiyHighlevelState* s){return s->front_fb;}
inline EpdDrawError epd_clear_checked(){events+='C';return drawOk?EPD_DRAW_SUCCESS:EPD_DRAW_FAILED;}
inline EpdDrawError draw(EpdiyHighlevelState* s,EpdDrawMode mode,char event){
 ++draws;lastMode=mode;events+=event;
 std::memcpy(lastGray,s->front_fb,sizeof(lastGray));
 if(drawOk)std::memcpy(s->back_fb,s->front_fb,415872);
 return drawOk?EPD_DRAW_SUCCESS:EPD_DRAW_FAILED;
}
inline EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState* s,EpdDrawMode m,int){return draw(s,m,'W');}
inline EpdDrawError epd_hl_update_screen_full(EpdiyHighlevelState* s,EpdDrawMode m,int){return draw(s,m,'F');}
inline EpdDrawError epd_hl_update_screen(EpdiyHighlevelState* s,EpdDrawMode m,int){return draw(s,m,'P');}
''')
    (root / 'esp_timer.h').write_text('#pragma once\ninline long long esp_timer_get_time(){return 0;}\n')
    (root / 'epd_display.h').write_text('#include "epdiy.h"\ninline EpdDisplay_t E0470A14_AF{};\n')
    (root / 'epd_highlevel.h').write_text('#include "epdiy.h"\n')
    (root / 'waveforms').mkdir()
    (root / 'waveforms/my_waveform.h').write_text(r'''#include "epdiy.h"
inline EpdWaveform MY_WAVEFORM;
inline const EpdWaveform* my_waveform_fast(){return nullptr;}
inline void my_waveform_init(){}
''')
    (root / 'MetalioPlusEpdBoard.h').write_text(r'''#include "epdiy.h"
namespace freeink::metalioplus {
inline constexpr int DEFAULT_VCOM_MV=1200;
inline const EpdBoardDefinition& epdBoard(){static EpdBoardDefinition board;return board;}
inline bool panelPowerGood(){return powerReady;}
}
''')
    (root / 'test.cpp').write_text(r'''#include <cassert>
#include <vector>
#include <esp_heap_caps.h>
#include <epdiy.h>
#include "src/driver/EpdiyDriver.h"
int main(){
 using namespace freeink;
 EpdBus bus;auto& d=epdiyDriver();
 std::vector<uint8_t> bw(103968,0xFF);
 assert(!d.beginGrayscale16());
 for(int failure=0;failure<5;++failure){
  failAllocation=failure;d.begin(bus);
  assert(allocations.empty()&&inits==0&&!d.beginGrayscale16());
 }
 failAllocation=-1;d.begin(bus);assert(inits==1&&allocations.size()==5);
 d.begin(bus);assert(inits==1);
 assert(d.geometry().width==1216&&d.geometry().widthBytes==152&&d.geometry().bufferSize==103968);
 bw[0]=0x80;d.display(bus,bw.data(),nullptr,RefreshMode::Fast,false);
 assert(d.displayCommitted()&&events=="+C-+F-"&&lastGray[0]==0x0F);
 auto* native=d.beginGrayscale16();assert(native&&!d.beginGrayscale16());
 assert(!d.displayCommitted());
 d.cancelGrayscale16();assert(native[0]==0x0F&&native[1]==0x00);
 assert(!d.commitGrayscale16(bw.data()));
 native=d.beginGrayscale16();
 for(int i=0;i<8;++i)native[i]=uint8_t(2*i)|uint8_t((2*i+1)<<4);
 events.clear();assert(d.commitGrayscale16(bw.data()));
 assert(events=="+F-"&&lastMode==MODE_GC16&&draws==2);
 for(int i=0;i<8;++i)assert(lastGray[i]==(uint8_t(2*i)|uint8_t((2*i+1)<<4)));
 drawOk=false;assert(d.beginGrayscale16());
 events.clear();assert(!d.commitGrayscale16(bw.data()));
 assert(!d.displayCommitted()&&events=="+F-");
 drawOk=true;events.clear();d.display(bus,bw.data(),nullptr,RefreshMode::Fast,false);
 assert(events=="+CW-"&&d.displayCommitted());
 powerReady=false;assert(d.beginGrayscale16());
 events.clear();assert(!d.commitGrayscale16(bw.data())&&events=="+-");
 powerReady=true;events.clear();d.display(bus,bw.data(),nullptr,RefreshMode::Fast,false);
 assert(events=="+CW-"&&d.displayCommitted());
 events.clear();d.deepSleep(bus);assert(events=="+CW-"&&d.displayCommitted());
 for(void* p:allocations)std::free(p);
}
''')
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra',
                    '-Werror', '-Wno-unused-parameter', '-fno-exceptions',
                    '-DFREEINK_DRIVER_EPDIY=1',
                    '-I' + str(root), '-I' + str(root / 'include'),
                    str(root / 'test.cpp'), str(root / 'src/driver/EpdiyDriver.cpp'),
                    '-o', str(root / 'test')], check=True)
    subprocess.run([str(root / 'test')], check=True)
print('Metalio Plus native gray, bit order, failure recovery, OOM and sleep passed')
