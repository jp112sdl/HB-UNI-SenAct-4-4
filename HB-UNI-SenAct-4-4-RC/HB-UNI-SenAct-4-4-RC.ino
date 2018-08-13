//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2016-10-31 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
// 2018-08-13 jp112sdl Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
// special thanks to "klassisch" from homematic-forum.de
//- -----------------------------------------------------------------------------------------------------------------------

// define this to read the device id, serial and device type from bootloader section
// #define USE_OTA_BOOTLOADER

#define EI_NOTEXTERNAL
#include <EnableInterrupt.h>
#include <AskSinPP.h>
#include <LowPower.h>
#include <Switch.h>
#include <MultiChannelDevice.h>

//#define USE_BATTERY_MODE       // bei Batteriebetrieb
#define LOWBAT_VOLTAGE     22    // Batterie-Leermeldung bei Unterschreiten der Spannung von U * 10

#define RELAY_PIN_1 14
#define RELAY_PIN_2 15
#define RELAY_PIN_3 16
#define RELAY_PIN_4 17

#define REMOTE_PIN_1 5
#define REMOTE_PIN_2 6
#define REMOTE_PIN_3 7
#define REMOTE_PIN_4 9

#define LED_PIN            4
#define CONFIG_BUTTON_PIN  8

// number of available peers per channel
#define PEERS_PER_SwitchChannel  4
#define PEERS_PER_RemoteChannel  4

#ifdef USE_BATTERY_MODE
#define battOp_ARGUMENT BatterySensor
#define DEV_MODEL 0x34
#define CYCLETIME seconds2ticks(60UL * 60 * 12 * 0.88) // 60 seconds * 60 (= minutes) * 12 (=hours) * corrective factor
#else
#define battOp_ARGUMENT NoBattery
#define DEV_MODEL 0x32
#define CYCLETIME seconds2ticks(60UL * 3 * 0.88)  // every 3 minutes
#endif

#define remISR(device,chan,pin) class device##chan##ISRHandler { \
    public: \
      static void isr () { device.remoteChannel(chan).irq(); } \
  }; \
  device.remoteChannel(chan).button().init(pin); \
  if( digitalPinToInterrupt(pin) == NOT_AN_INTERRUPT ) \
    enableInterrupt(pin,device##chan##ISRHandler::isr,CHANGE); \
  else \
    attachInterrupt(digitalPinToInterrupt(pin),device##chan##ISRHandler::isr,CHANGE);

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
#ifdef USE_BATTERY_MODE
BurstDetector<Hal> bd(hal); // to wake by remote burst, taken from HM-LC-SW1-BA-PCB.ino
#endif

DEFREGISTER(Reg0, MASTERID_REGS, DREG_INTKEY, DREG_CYCLICINFOMSG)
class SwList0 : public RegList0<Reg0> {
  public:
    SwList0(uint16_t addr) : RegList0<Reg0>(addr) {}
    void defaults() {
      clear();
      intKeyVisible(true);
      cycleInfoMsg(true);
    }
};

DEFREGISTER(RemoteReg1, CREG_LONGPRESSTIME, CREG_AES_ACTIVE, CREG_DOUBLEPRESSTIME)
class RemoteList1 : public RegList1<RemoteReg1> {
  public:
    RemoteList1 (uint16_t addr) : RegList1<RemoteReg1>(addr) {}
    void defaults () {
      clear();
      longPressTime(1);
      // aesActive(false);
      // doublePressTime(0);
    }
};
class RemoteChannel : public Channel<Hal, RemoteList1, EmptyList, DefList4, PEERS_PER_RemoteChannel, SwList0>, public Button {
  private:
    uint8_t       repeatcnt;

  public:
    typedef Channel<Hal, RemoteList1, EmptyList, DefList4, PEERS_PER_RemoteChannel, SwList0> BaseChannel;

    RemoteChannel () : BaseChannel() {}
    virtual ~RemoteChannel () {}

    Button& button () {
      return *(Button*)this;
    }

    uint8_t status () const {
      return 0;
    }

    uint8_t flags () const {
      return 0;
    }

    virtual void state(uint8_t s) {
      DHEX(BaseChannel::number());
      Button::state(s);
      RemoteEventMsg& msg = (RemoteEventMsg&)this->device().message();
      DPRINT("BATTERY IS LOW? "); DDECLN(this->device().battery().low());
      msg.init(this->device().nextcount(), this->number(), repeatcnt, (s == longreleased || s == longpressed), this->device().battery().low());
      if ( s == released || s == longreleased) {
        this->device().sendPeerEvent(msg, *this);
        repeatcnt++;
      }
      else if (s == longpressed) {
        this->device().broadcastPeerEvent(msg, *this);
      }
    }

    uint8_t state() const {
      return Button::state();
    }

    bool pressed () const {
      uint8_t s = state();
      return s == Button::pressed || s == Button::debounce || s == Button::longpressed;
    }
};

typedef SwitchChannel<Hal, PEERS_PER_SwitchChannel, SwList0>  SwChannel;

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
    VirtChannel<Hal, SwChannel, SwList0>     swChannel1,  swChannel2,  swChannel3,  swChannel4;
    VirtChannel<Hal, RemoteChannel, SwList0> remChannel5, remChannel6, remChannel7, remChannel8;
  public:
    typedef ChannelDevice<Hal, VirtBaseChannel<Hal, SwList0>, 8, SwList0> DeviceType;
    MixDevice (const DeviceInfo& info, uint16_t addr) : DeviceType(info, addr), cycle(*this) {
      DeviceType::registerChannel(swChannel1, 1);
      DeviceType::registerChannel(swChannel2, 2);
      DeviceType::registerChannel(swChannel3, 3);
      DeviceType::registerChannel(swChannel4, 4);

      DeviceType::registerChannel(remChannel5, 5);
      DeviceType::registerChannel(remChannel6, 6);
      DeviceType::registerChannel(remChannel7, 7);
      DeviceType::registerChannel(remChannel8, 8);
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

    RemoteChannel& remoteChannel (uint8_t num)  {
      switch (num) {
        case 5:
          return remChannel5;
          break;
        case 6:
          return remChannel6;
          break;
        case 7:
          return remChannel7;
          break;
        case 8:
          return remChannel8;
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
    HMID devid;
    sdev.getDeviceID(devid);
    for ( uint8_t i = 1; i <= 4; ++i ) {
      Peer ipeer(devid, i);
      sdev.channel(i).peer(ipeer);
    }
  }
}

void setup () {
  DINIT(57600, ASKSIN_PLUS_PLUS_IDENTIFIER);
  bool first = sdev.init(hal);
  sdev.switchChannel(1).init(RELAY_PIN_1, false);
  sdev.switchChannel(2).init(RELAY_PIN_2, false);
  sdev.switchChannel(3).init(RELAY_PIN_3, false);
  sdev.switchChannel(4).init(RELAY_PIN_4, false);

  remISR(sdev, 5, REMOTE_PIN_1);
  remISR(sdev, 6, REMOTE_PIN_2);
  remISR(sdev, 7, REMOTE_PIN_3);
  remISR(sdev, 8, REMOTE_PIN_4);

  buttonISR(cfgBtn, CONFIG_BUTTON_PIN);

  initPeerings(first);
#ifdef USE_BATTERY_MODE
  bd.enable(sysclock);
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
