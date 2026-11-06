"""Execute GPIO6 selection from SDCardManager::begin with gauge and bus faults."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[5]
source = (root / 'libs/hardware/SDCardManager/src/SDCardManager.cpp').read_text()
start = source.index('  // Adapted from upstream ed7c5c3')
end = source.index('#endif', start)
program = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
namespace BoardConfig {
constexpr int8_t PIN_UNASSIGNED=-1;
struct Gauge {int i2cSda=2,i2cScl=1;uint32_t i2cHz=400000;uint8_t gaugeAddr=0x62;};
struct Profile {
 Gauge batteryGauge;
 struct {int8_t powerEnable=6;} display;
 struct {int8_t powerEnable=-1;bool powerActiveHigh=true;} sd;
} ACTIVE;
bool isEegoA4(){return true;}
}
struct {
 bool ready=true;uint8_t status=0;int begins=0,probes=0;
 bool begin(int sda,int scl,uint32_t hz){assert(sda==2 && scl==1 && hz==400000);++begins;return ready;}
 void beginTransmission(uint8_t addr){assert(ready && addr==0x62);++probes;}
 uint8_t endTransmission(){return status;}
} Wire;
struct {
 explicit operator bool()const{return false;}
 template<class... Args> void printf(Args...){}
} Serial;
unsigned long millis(){return 0;}
bool begin(){
''' + source[start:end] + r'''
return true;
}
int main(int argc,char** argv){
 assert(argc==2);
 const bool absent=!strcmp(argv[1],"absent");
 if(!strcmp(argv[1],"init-fault")){
  Wire.ready=false;assert(!begin());assert(Wire.probes==0);Wire.ready=true;
 }
 if(!strcmp(argv[1],"bus-fault")){
  Wire.status=4;assert(!begin());
  assert(BoardConfig::ACTIVE.display.powerEnable==6 && BoardConfig::ACTIVE.sd.powerEnable==-1);
 }
 Wire.status=absent?2:0;assert(begin());
 assert(BoardConfig::ACTIVE.display.powerEnable==(absent?6:-1));
 assert(BoardConfig::ACTIVE.sd.powerEnable==(absent?-1:6));
 if(!absent)assert(!BoardConfig::ACTIVE.sd.powerActiveHigh);
 int before=Wire.begins;assert(begin());assert(Wire.begins==before);
}
'''
with tempfile.TemporaryDirectory(prefix='eego-gpio6-') as folder:
    cpp = Path(folder)/'check.cpp'
    exe = Path(folder)/'check'
    cpp.write_text(program)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True)
    for case in ('present', 'absent', 'init-fault', 'bus-fault'):
        subprocess.run([str(exe), case], check=True)
print('EEGO GPIO6 gauge-present/absent, initialization retry and transient bus fault passed')
