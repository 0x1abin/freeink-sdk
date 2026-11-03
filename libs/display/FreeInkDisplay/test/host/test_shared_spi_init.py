"""Run facade SPI selection and real EpdBus initialization with shared/dedicated SD."""
from pathlib import Path
import os
import subprocess
import tempfile

here = Path(__file__).resolve().parent
lib = here.parents[1]
facade = (lib/'src/FreeInkDisplay.cpp').read_text()
start = facade.index('    const auto &d = BoardConfig::ACTIVE.display;')
end = facade.index(');', facade.index('    _bus.begin(', start))+2
bus = (lib/'src/bus/EpdBus.cpp').read_text()
start_bus = bus.index('void EpdBus::begin(')
end_bus = bus.index('\n}\n', start_bus)+2
program = r'''
#include <cassert>
#include <cstdint>
#include <vector>
constexpr int HIGH=1,OUTPUT=2,INPUT=3,INPUT_PULLUP=4,MSBFIRST=1,SPI_MODE0=0;
using gpio_num_t=int;
std::vector<int> raised;
void gpio_hold_dis(int){} void pinMode(int,int){} void delay(int){}
void digitalWrite(int pin,int level){if(level==HIGH)raised.push_back(pin);}
bool boardEpdPowerAvailable(){return false;} void boardEpdPower(bool){}
void* s_epdRefreshDone=nullptr;
void* xSemaphoreCreateBinary(){return reinterpret_cast<void*>(1);}
struct SPISettings {SPISettings()=default;SPISettings(uint32_t,int,int){}};
struct {int miso=-2,begins=0;void begin(int,int m,int,int){miso=m;++begins;}} SPI;
enum class BusyPolarity {ActiveHigh,ActiveLow,ActiveLowReady};
struct EpdPins {int8_t sclk=10,mosi=11,cs=6,dc=3,rst=4,busy=5,powerEnable=-1;};
namespace BoardConfig {
struct Profile {EpdPins display;struct {int8_t miso=12,cs=8;bool separateSpi=false;} sd;} ACTIVE;
}
struct EpdBus {
 EpdPins _pins;uint32_t _spiHz=0;BusyPolarity _busy;int8_t _coCs=-1;SPISettings _spi;
 void createPmLocks(){}
 void begin(const EpdPins&,uint32_t,BusyPolarity,int8_t,int8_t);
};
struct NoLightSleepLockGuard{explicit NoLightSleepLockGuard(EpdBus&) {}};
struct SpiApbLockGuard{explicit SpiApbLockGuard(EpdBus&) {}};
''' + bus[start_bus:end_bus] + r'''
struct Driver {
 int8_t spiMiso(){return 29;} int8_t coCs(){return 30;}
 uint32_t spiHz(){return 20000000;} BusyPolarity busyPolarity(){return BusyPolarity::ActiveHigh;}
};
struct FreeInkDisplay {
 Driver driver;Driver* _driver=&driver;EpdBus _bus;
 void beginBus(){
''' + facade[start:end] + r'''
 }
};
bool raisedPin(int pin){for(int p:raised)if(p==pin)return true;return false;}
int main(){
 FreeInkDisplay display;
 display.beginBus();
 assert(SPI.miso==12 && display._bus._coCs==8 && raisedPin(8));
 raised.clear();BoardConfig::ACTIVE.sd.separateSpi=true;
 display.beginBus();assert(SPI.miso==29 && display._bus._coCs==30 && raisedPin(30));
 assert(!raisedPin(8));
 BoardConfig::ACTIVE.sd.separateSpi=false;BoardConfig::ACTIVE.sd.miso=-1;
 display.beginBus();assert(SPI.miso==29 && display._bus._coCs==8);
 BoardConfig::ACTIVE.sd.cs=-1;display.beginBus();assert(display._bus._coCs==30);
 assert(SPI.begins==4);
}
'''
with tempfile.TemporaryDirectory(prefix='freeink-shared-spi-') as folder:
    cpp=Path(folder)/'check.cpp'
    exe=Path(folder)/'check'
    cpp.write_text(program)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('Shared SPI preserves MISO and parks SD CS; dedicated/missing-pin fallbacks passed')
