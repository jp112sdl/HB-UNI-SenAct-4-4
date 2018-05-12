//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2016-10-31 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//- -----------------------------------------------------------------------------------------------------------------------

// define this to read the device id, serial and device type from bootloader section
// #define USE_OTA_BOOTLOADER

#define EI_NOTEXTERNAL
#include <EnableInterrupt.h>
#include <AskSinPP.h>
#include <LowPower.h>

#include <Switch.h>
#include <MultiChannelDevice.h>

const uint8_t RELAY_PINS[4]  = {14, 15, 16, 17}; // 14 = A0 ... 17 = A3
const uint8_t REMOTE_PINS[4] = { 5,  6,  7,  9};
#define       LED_PIN            4
#define       CONFIG_BUTTON_PIN  8

// number of available peers per channel
#define PEERS_PER_SwitchChannel  4
#define PEERS_PER_RemoteChannel  4
#define CYCLETIME seconds2ticks(60UL * 24 * 0.88) // at least one message per day

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
  {0xf3, 0x32, 0x01},     // Device ID
  "JPSENACT11",           // Device Serial
  {0xf3, 0x32},           // Device Model
  0x10,                   // Firmware Version
  as::DeviceType::Switch, // Device Type
  {0x01, 0x00}            // Info Bytes
};

/**
   Configure the used hardware
*/
typedef AvrSPI<10, 11, 12, 13> RadioSPI;
typedef AskSin<StatusLed<LED_PIN>, NoBattery, Radio<RadioSPI, 2> > Hal;
Hal hal;

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
    VirtChannel<Hal, SwChannel, SwList0>   swChannel[4];
    VirtChannel<Hal, RemoteChannel, SwList0> remChannel[4];
  public:
    typedef ChannelDevice<Hal, VirtBaseChannel<Hal, SwList0>, 8, SwList0> DeviceType;
    MixDevice (const DeviceInfo& info, uint16_t addr) : DeviceType(info, addr), cycle(*this) {
      for (int i = 0; i < 4; i++)
        DeviceType::registerChannel(swChannel[i], i + 1);
        
      for (int i = 0; i < 4; i++)
        DeviceType::registerChannel(remChannel[i], i + 5);
    }
    virtual ~MixDevice () {}

    SwChannel& switchChannel (uint8_t num)  {
      return swChannel[ num - 1 ];
    }
    
    RemoteChannel& remoteChannel (uint8_t num)  {
      return remChannel[num - 5];
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
  for (uint8_t i = 1; i <= 4; i++) {
    sdev.switchChannel(i).init(RELAY_PINS[i], false);
  }

  remISR(sdev, 5, REMOTE_PINS[0]);
  remISR(sdev, 6, REMOTE_PINS[1]);
  remISR(sdev, 7, REMOTE_PINS[2]);
  remISR(sdev, 8, REMOTE_PINS[3]);

  buttonISR(cfgBtn, CONFIG_BUTTON_PIN);

  initPeerings(first);
  sdev.initDone();
}

void loop() {
  bool worked = hal.runready();
  bool poll = sdev.pollRadio();
  if ( worked == false && poll == false ) {
    hal.activity.savePower<Idle<> >(hal);
  }
}

