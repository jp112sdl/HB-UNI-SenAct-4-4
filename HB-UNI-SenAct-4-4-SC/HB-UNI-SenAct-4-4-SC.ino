//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2016-10-31 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
// 2018-08-13 jp112sdl Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
// special thanks to "klassisch" from homematic-forum.de
//- -----------------------------------------------------------------------------------------------------------------------
// ci-test=yes board=328p aes=no

// define this to read the device id, serial and device type from bootloader section
// #define USE_OTA_BOOTLOADER
#define USE_WOR

#define EI_NOTEXTERNAL
#include <EnableInterrupt.h>
#include <AskSinPP.h>
#include <LowPower.h>
#include <Switch.h>
#include <ContactState.h>

//#define USE_BATTERY_MODE       // bei Batteriebetrieb
#define LOWBAT_VOLTAGE     22    // Batterie-Leermeldung bei Unterschreiten der Spannung von U * 10

#define RELAY_PIN_1 A0
#define RELAY_PIN_2 A1
#define RELAY_PIN_3 A2
#define RELAY_PIN_4 A3

#define SENS_PIN_1  5
#define SENS_PIN_2  6
#define SENS_PIN_3  7
#define SENS_PIN_4  9

#define SABOTAGE_PIN_1    3

#define LED_PIN           4
#define CONFIG_BUTTON_PIN 8

// number of available peers per channel
//#define CREATE_INTERNAL_PEERINGS
#define PEERS_PER_SwitchChannel  6
#define PEERS_PER_SENSCHANNEL    6

#ifdef USE_BATTERY_MODE
#define battOp_ARGUMENT BatterySensor
#define DEV_MODEL 0x33
#define CYCLETIME seconds2ticks(60UL * 60 * 12 * 0.88) // 60 seconds * 60 (= minutes) * 12 (=hours) * corrective factor
#else
#define battOp_ARGUMENT NoBattery
#define DEV_MODEL 0x31
#define CYCLETIME seconds2ticks(60UL * 3 * 0.88)  // every 3 minutes
#endif

// all library classes are placed in the namespace 'as'
using namespace as;

// define all device properties
const struct DeviceInfo PROGMEM devinfo = {
  {0xf3, DEV_MODEL, 0x01},// Device ID
  "JPSENACT01",           // Device Serial
  {0xf3, DEV_MODEL},      // Device Model
  0x10,                   // Firmware Version
  as::DeviceType::Switch, // Device Type
  {0x01, 0x00}            // Info Bytes
};

/**
   Configure the used hardware
*/
typedef AvrSPI<10, 11, 12, 13> RadioSPI;
typedef AskSin<StatusLed<LED_PIN>, battOp_ARGUMENT, Radio<RadioSPI, 2> > Hal;
Hal hal;

DEFREGISTER(Reg0, MASTERID_REGS, DREG_INTKEY, DREG_CYCLICINFOMSG, DREG_SABOTAGEMSG)
class SwList0 : public RegList0<Reg0> {
  public:
    SwList0(uint16_t addr) : RegList0<Reg0>(addr) {}
    void defaults() {
      clear();
      intKeyVisible(true);
      sabotageMsg(true);
      cycleInfoMsg(true);
    }
};


DEFREGISTER(Reg1, CREG_AES_ACTIVE, CREG_MSGFORPOS, CREG_EVENTDELAYTIME, CREG_LEDONTIME, CREG_TRANSMITTRYMAX)
class SensList1 : public RegList1<Reg1> {
  public:
    SensList1 (uint16_t addr) : RegList1<Reg1>(addr) {}
    void defaults () {
      clear();
      msgForPosA(1);
      msgForPosB(2);
      aesActive(false);
      eventDelaytime(0);
      ledOntime(100);
      transmitTryMax(6);
    }
};

typedef SwitchChannel<Hal, PEERS_PER_SwitchChannel, SwList0>  SwChannel;
typedef TwoStateChannel<Hal, SwList0, SensList1, DefList4, PEERS_PER_SENSCHANNEL> SensChannel;

class MixDevice : public ChannelDevice<Hal, VirtBaseChannel<Hal, SwList0>, 8, SwList0> {
    class CycleInfoAlarm : public Alarm {
        MixDevice& dev;
      public:
        CycleInfoAlarm (MixDevice& d) : Alarm (CYCLETIME), dev(d) {}
        virtual ~CycleInfoAlarm () {}

        void trigger (AlarmClock& clock)  {
          set(CYCLETIME);
          clock.add(*this);
          dev.switchChannel(1).changed(true);
        }
    } cycle;

  public:
    VirtChannel<Hal, SwChannel, SwList0>   swChannel1,   swChannel2,   swChannel3,   swChannel4;
    VirtChannel<Hal, SensChannel, SwList0> sensChannel5, sensChannel6, sensChannel7, sensChannel8;
  public:
    typedef ChannelDevice<Hal, VirtBaseChannel<Hal, SwList0>, 8, SwList0> DeviceType;
    MixDevice (const DeviceInfo& info, uint16_t addr) : DeviceType(info, addr), cycle(*this) {
      DeviceType::registerChannel(swChannel1, 1);
      DeviceType::registerChannel(swChannel2, 2);
      DeviceType::registerChannel(swChannel3, 3);
      DeviceType::registerChannel(swChannel4, 4);

      DeviceType::registerChannel(sensChannel5, 5);
      DeviceType::registerChannel(sensChannel6, 6);
      DeviceType::registerChannel(sensChannel7, 7);
      DeviceType::registerChannel(sensChannel8, 8);
    }
    virtual ~MixDevice () {}


    SwChannel& switchChannel (uint8_t num)  {
      switch (num) {
        case 1:
          return swChannel1;
          break;
        case 2:
          return swChannel2;
          break;
        case 3:
          return swChannel3;
          break;
        case 4:
          return swChannel4;
          break;
      }
    }

    SensChannel& sensorChannel (uint8_t num)  {
      switch (num) {
        case 5:
          return sensChannel5;
          break;
        case 6:
          return sensChannel6;
          break;
        case 7:
          return sensChannel7;
          break;
        case 8:
          return sensChannel8;
          break;
      }
    }

    virtual void configChanged () {
      if ( /*this->getSwList0().cycleInfoMsg() ==*/ true ) {
        DPRINTLN("Activate Cycle Msg");
        sysclock.cancel(cycle);
        cycle.set(CYCLETIME);
        sysclock.add(cycle);
      }
      else {
        DPRINTLN("Deactivate Cycle Msg");
        sysclock.cancel(cycle);
      }
    }
};
MixDevice sdev(devinfo, 0x20);
ConfigButton<MixDevice> cfgBtn(sdev);

void initPeerings (bool first) {
  // create internal peerings - CCU2 needs this
  if ( first == true ) {
#ifdef CREATE_INTERNAL_PEERINGS    
    HMID devid;
    sdev.getDeviceID(devid);
    for ( uint8_t i = 1; i <= 4; ++i ) {
      Peer ipeer(devid, i + 4);
      sdev.switchChannel(i).peer(ipeer);
    }
    for ( uint8_t i = 1; i <= 4; ++i ) {
      Peer ipeer(devid, i);
      sdev.sensorChannel(i + 4).peer(ipeer);
    }
#endif    
  }
}

void setup () {
  DINIT(57600, ASKSIN_PLUS_PLUS_IDENTIFIER);
  bool first = sdev.init(hal);
  sdev.switchChannel(1).init(RELAY_PIN_1, false);
  sdev.switchChannel(2).init(RELAY_PIN_2, false);
  sdev.switchChannel(3).init(RELAY_PIN_3, false);
  sdev.switchChannel(4).init(RELAY_PIN_4, false);

  sdev.sensorChannel(5).init(SENS_PIN_1, SABOTAGE_PIN_1);
  sdev.sensorChannel(6).init(SENS_PIN_2, SABOTAGE_PIN_1);
  sdev.sensorChannel(7).init(SENS_PIN_3, SABOTAGE_PIN_1);
  sdev.sensorChannel(8).init(SENS_PIN_4, SABOTAGE_PIN_1);

  buttonISR(cfgBtn, CONFIG_BUTTON_PIN);

  initPeerings(first);

#ifdef USE_BATTERY_MODE
  hal.activity.stayAwake(seconds2ticks(15));
  hal.battery.low(LOWBAT_VOLTAGE);
  // measure battery every 12 hours
  hal.battery.init(seconds2ticks(60UL * 60 * 12 * 0.88), sysclock);
#endif

  sdev.initDone();
}

void loop() {
  bool worked = hal.runready();
  bool poll = sdev.pollRadio();
  if ( worked == false && poll == false ) {
#ifdef USE_BATTERY_MODE
    hal.activity.savePower<Sleep<> >(hal);
#else
    hal.activity.savePower<Idle<> >(hal);
#endif
  }
}
