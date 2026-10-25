//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//      EBTKS Firmware Version 0.1
//
//      06/25/2020      Derived from Nirvana V1.0 Firmware running on Teensy 4.0
//
//      Authors:        Russell Bull
//                      Philip Freidin
//
//      06/25/2020      Migrate all code from prototype of this project, code name Nirvana. New project name is EBTKS
//                      1)      Scan through all comments to check for references to Teensy V4.0 Most will not change for Teensy V4.1
//                      2)      Redo all the documentation for the Pin Mapping
//                      3)      Update all references to Pin Mapping
//
//      06/26/2020      1) Integrate Russell's first version of tape drive emulation.
//                      Functionality is Read Only , with a minimal pre-defined tape that has 3 files, including two small BASIC programs.
//                      The HP-85 System boot test for Tape drive passes, thus eliminating the "Error 23" when power is turned and there is no tape drive
//                      The CAT command shows the directory correctly
//                      The two little BASIC programs can be LOADed and RUN successfully
//                      2) Start the process of splitting the project into multiple files. (was one monolithic file of 2760 lines + 4132 in ROMS.h)
//
//      06/29/2020      Work on startup code and startup timing. Make measurements
//
//      Changes to core usb.c           Use a less heavy handed approach to disabling interrupts
//                      See L:\BUILD_A_SYS_Scripts-Bug_Hunts-Workarounds-Other_Notes\Teensy_4.0_Notes.txt for details
//
//      Note: See Logic_analyser.md for respective info.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <Arduino.h>
#include <setjmp.h>
#include "SdFat.h"

//  The next line must exist in just one compilation unit, which is this file.
#define ALLOCATE 1

#include "Inc_Common_Headers.h"
#include "EBTKS_CPM85_Rmidle_Probe.h"
#include "EBTKS_ESP.h"
#include <TimeLib.h>
// uncomment to quick access file with editor. Do not leave uncommented
// #include "core_pins.h"
ESPComms esp32;

// Reference to the device configuration
#include "HpibDevice.h"
#include "HpibPrintUSB.h"
extern HpibDevice *devices[];
HpibDevice* findFirstDevice(Hpdev_t type) {
  for(int d=0; d<NUM_HPIB_DEVICES; d++) {
    if (devices[d]!=NULL && devices[d]->isType(type)) return devices[d];
  }
  return NULL;
}

// TB: Add support for USB printer
// The below adds a lot of debug output which slows things down. Only uncomment if you wish to debug the USB enumeration.
// #define USBHOST_PRINT_DEBUG
#include <USBHost_t36.h>
#include "USBSIIPrinter.h"

USBHost usb;
// The USB Hub is absolutely required to make enumeration work as it contributes essential USBHost_t36 memory resources.
// It does not require a physical hub to be attached.
USBHub usbhub1(usb);
USBSIIPrinter seikoPrinter(usb);

// TB: no keyboard support yet
// KeyboardController keyboard1(myusb);
// KeyboardController keyboard2(myusb);

//  Messages for startup indicating LOGLEVEL
#if LOGLEVEL_GEN == LOG_NONE
#define LOGLEVEL_GEN_MESSAGE "LOGLEVEL_GEN is  None\n"
#elif LOGLEVEL_GEN == LOG_FILE
#define LOGLEVEL_GEN_MESSAGE "LOGLEVEL_GEN is  File on SD card\n"
#elif LOGLEVEL_GEN == LOG_SERIAL
#define LOGLEVEL_GEN_MESSAGE "LOGLEVEL_GEN is  Serial via USB\n"
#else
#define LOGLEVEL_GEN_MESSAGE "LOGLEVEL_GEN is  unknown\n"
#endif

#if LOGLEVEL_AUX == LOG_NONE
#define LOGLEVEL_AUX_MESSAGE "LOGLEVEL_AUX is  None\n"
#elif LOGLEVEL_AUX == LOG_FILE
#define LOGLEVEL_AUX_MESSAGE "LOGLEVEL_AUX is  File on SD card\n"
#elif LOGLEVEL_AUX == LOG_SERIAL
#define LOGLEVEL_AUX_MESSAGE "LOGLEVEL_AUX is  Serial via USB\n"
#else
#define LOGLEVEL_AUX_MESSAGE "LOGLEVEL_AUX is  unknown\n"
#endif

#if LOGLEVEL_1MB5 == LOG_NONE
#define LOGLEVEL_1MB5_MESSAGE "LOGLEVEL_1MB5 is None\n"
#elif LOGLEVEL_1MB5 == LOG_FILE
#define LOGLEVEL_1MB5_MESSAGE "LOGLEVEL_1MB5 is File on SD card\n"
#elif LOGLEVEL_1MB5 == LOG_SERIAL
#define LOGLEVEL_1MB5_MESSAGE "LOGLEVEL_1MB5 is Serial via USB\n"
#else
#define LOGLEVEL_1MB5_MESSAGE "LOGLEVEL_1MB5 is unknown\n"
#endif

#if LOGLEVEL_TAPE == LOG_NONE
#define LOGLEVEL_TAPE_MESSAGE "LOGLEVEL_TAPE is None\n"
#elif LOGLEVEL_TAPE == LOG_FILE
#define LOGLEVEL_TAPE_MESSAGE "LOGLEVEL_TAPE is File on SD card\n"
#elif LOGLEVEL_TAPE == LOG_SERIAL
#define LOGLEVEL_TAPE_MESSAGE "LOGLEVEL_TAPE is Serial via USB\n"
#else
#define LOGLEVEL_TAPE_MESSAGE "LOGLEVEL_TAPE is unknows\n"
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////    Handlers for I/O device support
//  Setup Tasks
//    Configure direction and state of all I/O pins
//    Replace the TeensyDuino SysTick handler with our own, with guaranteed minimum execution time
//    Deal with holding the HP-85 in reset if until this setup code is complete
//      Scenario 1: HP-85 is already running.
//                  We pull down on PWO for 500 ms, and reset it
//      Scenario 2: Teensy code here is running while PWO is still low on HP85
//                  We pull down on PWO for 500 ms anyway, then release our pulling it low
//                  and wait for whatever else is pulling it low to stop. i.e. wait for high

bool is_hp85_idle(void);
void Boot_Messages_to_CRT(void);

static uint32_t one_second_timer = 0;
static bool SD_begin_OK;
static bool CRT_Boot_Message_Pending;
static bool CRT_Boot_Message_Pending_to_Serial;
static bool Serial_Log_Message_Pending;
static uint32_t last_pin_isr_count;                  //  These are used to detect that the HP85 power is off, and Teensy should be reset
static uint32_t count_of_pin_isr_count_not_changing; //  or if we aren't in loop(), we should stall boot.
static bool HP85_has_been_off;
// TB: added
bool usbPrinter_OK = false;

//  Call back for file timestamps.  Only called for file create and sync().
void dateTime(uint16_t* date, uint16_t* time, uint8_t* ms10) {

  // Return date using FS_DATE macro to format fields.
  *date = FS_DATE(year(), month(), day());

  // Return time using FS_TIME macro to format fields.
  *time = FS_TIME(hour(), minute(), second());

  // Return low time bits in units of 10 ms.
  *ms10 = second() & 1 ? 100 : 0;
}

// LEDs on the board initialisation delay.
bool LEDs_Initialized = false;

void Initial_LED_load(bool set_initial_values) {

  if (set_initial_values) {
    setLedColor(0, 10, 0, 0);
    setLedColor(1, 0, 10, 0);
    WS2812_update();
    LEDs_Initialized = true;
  }
  else {
    setLedColor(0, 0, 0, 0);
    setLedColor(1, 0, 0, 0);
    WS2812_update();
  }
}

void setup() {
  bool config_success;
  uint32_t Phi_1_high_count;
  uint32_t Phi_1_low_count;
  uint32_t i;

  //  All of the initial startup tasks are just configuring I/O pins, and none should cause a stall.
  pin_isr_count = 0;
  last_pin_isr_count = 0;
  count_of_pin_isr_count_not_changing = 0;
  CRT_Boot_Message_Pending = true;
  CRT_Boot_Message_Pending_to_Serial = true;
  Serial_Log_Message_Pending = true;

  //
  //  Diagnostic Pins
  //
  //	Name							Physical Pin		Suggested use
  //	CORE_PIN_SCOPE_1  25							ISR Entry and Exit. SCOPE_1
  //																		Also used by SCOPE_1_Pulser, which is used to track timing of the Boot sequence,
  //                                    and should only be called before Pin Change Interrupt is activated
  //	CORE_PIN_SCOPE_2	31							General entry and exit of whatever is currently being debugged, currently DMA, SCOPE_2
  //	CORE_PIN_T13			35							Also the on board LED. Toggle when errors are detected in diagnostic tests

  pinMode(CORE_PIN_ESP32_RXD, OUTPUT); //  Communications with the ESP32
  pinMode(CORE_PIN_ESP32_TXD, INPUT);  //  Communications with the ESP32

  digitalWrite(CORE_PIN_ESP_EN, 1);
  digitalWrite(CORE_PIN_ESP_BOOT, 1);
  pinMode(CORE_PIN_ESP_EN, OUTPUT);
  pinMode(CORE_PIN_ESP_BOOT, OUTPUT);

  pinMode(CORE_PIN_T13, OUTPUT);
  //  SET_LED;                        //  T13 On Board LED
  CLEAR_LED;

  pinMode(CORE_PIN_SCOPE_1, OUTPUT);
  CLEAR_SCOPE_1; //  SCOPE_1 tracks entry and exit of the pin_change ISR, at the heart of the bus interface
                 //  and also pulses during the boot process, to measure the time various tasks take,
                 //  and to identify problems.
  pinMode(CORE_PIN_SCOPE_2, OUTPUT);
  CLEAR_SCOPE_2; //  SCOPE_2

  ASSERT_PWO_OUT;                  //  Desire glitch free switchover from resistor asserting PWO to Teensy asserting PWO
  pinMode(CORE_PIN_PWO_O, OUTPUT); //  Core Pin 36 (Physical pin 28) is pulled high by an external resistor, which turns on the FET,
                                   //  which pulls the PWO line low, which resets the HP-85
  ASSERT_PWO_OUT;                  //  Over-ride external pull up resistor, still put High on gate, thus holdimg PWO on I/O bus Low

  //  SCOPE_1 starts low (see above), pulses 5 times, ends low. Pulses are 10 us apart, High duration is 5 us
  SCOPE_1_Pulser(5);
  EBTKS_delay_ns(10000); //  10 us

  _VectorsRam[15] = mySystick_isr; //  Replace the system SysTick handler with our own, that doesn't call yield()

  //  Data Bus Transceiver
  pinMode(CORE_PIN_DIR_RC, OUTPUT); // Direction pin for U2, the 8 bit data bus level shifter
  pinMode(CORE_PIN_BUFEN, OUTPUT);  // Output enable for the U2 level shifter.  !OE
  BUS_DIR_FROM_HP;                  // Default state for the Data Bus transceiver is enabled and inbound
  ENABLE_BUS_BUFFER_U2;             // Default state for the Data Bus transceiver is enabled and inbound

  //  Three Control signals and two of the four Clock phases
  //  Clocks are always inbound. The 3 control lines are inbound except during DMA when this
  //  software generates these three signals
  pinMode(CORE_PIN_RD, INPUT);     //  /RD
  pinMode(CORE_PIN_WR, INPUT);     //  /WR
  pinMode(CORE_PIN_LMA, INPUT);    //  /LMA
  pinMode(CORE_PIN_PHASE1, INPUT); //  Phi 1
  pinMode(CORE_PIN_PHASE2, INPUT); //  Phi 2

  pinMode(CORE_PIN_T04, INPUT);    //  /IRLX     Used by the Logic Analyzer
  pinMode(CORE_PIN_T05, INPUT);    //  /HALTX    Used by the Logic Analyzer
  pinMode(CORE_PIN_IFETCH, INPUT); //  IFETCH    Used by the Logic Analyzer

  pinMode(CORE_PIN_CTRL_DIR, OUTPUT); //  bus control buffer direction /LMA,/WR,/RD
  CTRL_DIR_FROM_HP;

  pinMode(CORE_PIN_INTERRUPT, OUTPUT); //  INT inverted by hardware
  RELEASE_INT;

  pinMode(CORE_PIN_IPRIH_IN, INPUT); // PRIH input - interrupt priority chain input. low if someone above us is interrupting
  pinMode(CORE_PIN_INTPRI, OUTPUT);
  RELEASE_INTPRI;

  RELEASE_HALT;                    //  Make activating pin output glitch free
  pinMode(CORE_PIN_HALTX, OUTPUT); //  HALT inverted by hardware
  RELEASE_HALT;

  pinMode(CORE_PIN_PWO_L, INPUT); // PWO from HP85

  // data bus pins to input
  pinMode(CORE_PIN_DB0, INPUT); //  DB0
  pinMode(CORE_PIN_DB1, INPUT); //  DB1
  pinMode(CORE_PIN_DB2, INPUT); //  DB2
  pinMode(CORE_PIN_DB3, INPUT); //  DB3
  pinMode(CORE_PIN_DB4, INPUT); //  DB4
  pinMode(CORE_PIN_DB5, INPUT); //  DB5
  pinMode(CORE_PIN_DB6, INPUT); //  DB6
  pinMode(CORE_PIN_DB7, INPUT); //  DB7

  //
  //  Before we proceed with boot, make sure the HP85 is powered on and the clocks are running
  //  We do this by counting the number of times CORE_PIN_PHASE1 is high or low in a loop for 100 us
  //  Empirical measurement of this code shows values around 4733 for low count and 700 for high count
  //

  HP85_has_been_off = false; //  We haven't tested it yet
  loop_count = 0;            //  This normally used in loop() , but we need a counter, and loop() is not yet running, so ...

  //  End of initial setup (configuring pins). At this point, EBTKS is asserting PWO, so if what follows stalls for any reason,
  //  then it will also block the Series80 computer from starting, and the user wil not have a clue why, except unplugging EBTKS
  //  should return the Series80 computer to its native startup process.
  Serial.begin(115200);

  // Wait until the HP-80 machine has powered up
  while (1) {
    Phi_1_high_count = Phi_1_low_count = 0;
    loop_count++;
    //  This constant adjusted with SCOPE_2 to run for 100 us
    for (i = 0; i < 5435; i++) {
      if (IS_PHI_1_HIGH) {
        Phi_1_high_count++;
      }
      else {
        Phi_1_low_count++;
      }
    }

    if ((Phi_1_high_count < 350) || (Phi_1_low_count < 2000)) { //  Teensy is powered, but clock not running, so probably teensy powered over USB and HP85 is off.
      //  Flash the TEENSY Orange LED on Core pin 13
      HP85_has_been_off = true;

      //  Don't be too annoying, flash LED once approximately every 10 seconds
      if (loop_count % 10 == 0) {
        Serial.printf("Teensy powered by USB cable, Series80 computer is off\n");
        for (i = 0; i < 3; i++) {
          SET_LED;
          delay(3); //  Pulse LED for 3 ms, so not too many photons.
          CLEAR_LED;
          delay(50);
        }
      }
      delay(1000); //  Wait 1 seconds till we test again
    }
    else {
      if (HP85_has_been_off) {
        //  The HP85 has transitioned from off to on, so we need to force a reset of Teensy
        SCB_AIRCR = 0x05FA0004; //  Forum info indicates this does a processor reset, so we don't get to the next line.
      }
      //  If we get here, it means that the HP85 was already powered either before or concurrently with Teensy, so no need for a forced reset
      break; //  Looks like the HP85 clock is running, so we can proceed
    }
  }

  // See logging.md
  log_to_CRT_ptr = &CRT_Log_Buffer[0];
  log_to_serial_ptr = &Serial_Log_Buffer[0];

  *log_to_CRT_ptr = 0;
  *log_to_serial_ptr = 0;

  //  Setup io function calls
  initIOfuncTable();
  initRoms();

  //  Can't init CRT yet as we need to process CONFIG.TXT
  pinMode(CORE_PIN_NEOPIX_TSY, OUTPUT); //  The single pin interface to the two WS2812B/E RGB LEDs

  setIOWriteFunc(0340, &ioWriteAuxROM_Alert); //  AUXROM Alert that a Mailbox/Buffer has been updated
  // CPM85: adjacent, otherwise unused EBTKS extension I/O ports.
  // 0341=0177741 raw HP key; 0342=0177742 attach (1)/detach (0).
  // Both handlers only write to the bounded ISR FIFO: NO serial or SPI here.
  setIOWriteFunc(0341, &onWriteCpm85Key);
  setIOWriteFunc(0342, &onWriteCpm85Control);
  // Drive mailbox callbacks only copy bytes; all serial work stays in loop().
  setIOWriteFunc(0343, &onWriteCpm85DriveArgument);
  setIOWriteFunc(0344, &onWriteCpm85DriveOpcode);
  setIOReadFunc(0345, &onReadCpm85DriveState);
  setIOReadFunc(0346, &onReadCpm85DriveReply);
  setIOReadFunc(0340, &onReadAuxROM_Alert);   //  Use HEYEBTKS to return identification string

  SCOPE_1_Pulser(4);     //  2/7/2021 This occurs about 285 us after the previous call to Pulser(5) (inc 10 us delay)
  EBTKS_delay_ns(10000); //  10 us

  //    Copy the Time and Date from the Secure RTC to the general RTC
  setSyncProvider(getTeensy3Time);
  
  initialize_RMIDLE_processing(); //  This must be done before we get to CONFIG.TXT processing

  log_to_serial_ptr += sprintf(log_to_serial_ptr, "EBTKS Firmware built on %s\n\n", EBTKS_COMPILE_TIME);
  SCOPE_1_Pulser(3);     //  2/7/2021 This occurs about 18 us after the previous call to Pulser(4) (inc 10 us delay)
  EBTKS_delay_ns(10000); //  10 us

  log_to_serial_ptr += sprintf(log_to_serial_ptr, "%s", LOGLEVEL_GEN_MESSAGE);
  log_to_serial_ptr += sprintf(log_to_serial_ptr, "%s", LOGLEVEL_AUX_MESSAGE);
  log_to_serial_ptr += sprintf(log_to_serial_ptr, "%s", LOGLEVEL_1MB5_MESSAGE);
  log_to_serial_ptr += sprintf(log_to_serial_ptr, "%s", LOGLEVEL_TAPE_MESSAGE);

  log_to_serial_ptr += sprintf(log_to_serial_ptr, "Phi_1_high_count = %lu   Phi_1_low_count = %lu", Phi_1_high_count, Phi_1_low_count);

  //  Use CONFIG.TXT file on sd card for configuration

  log_to_serial_ptr += sprintf(log_to_serial_ptr, "\nDoing SD.begin\n");
  SCOPE_1_Pulser(2);
  SET_SCOPE_1; //  SCOPE_1_Pulser does 2 toggles, so this inverts things to give some additional info
               //  2/7/2021 This occurs about 16 us after the previous call to Pulser(3) (inc 10 us delay)

  SD_begin_OK = SD.begin(SdioConfig(DMA_SDIO)); //  This takes about 9 ms.
  CLEAR_SCOPE_1;                                //  2/7/2021 This occurs about 9 ms after the previous call to Pulser(2)
  EBTKS_delay_ns(1000);
  SCOPE_1_Pulser(2);

  log_to_serial_ptr += sprintf(log_to_serial_ptr, "Access to SD Card is %s\n", SD_begin_OK ? "true" : "false");

  if (SD_begin_OK) {
    logfile_active = open_logfile();
    log_to_serial_ptr += sprintf(log_to_serial_ptr, "logfile_active is %s\n", logfile_active ? "true" : "false");
    LOGPRINTF("\n----------------------------------------------------------------------------------------------------------\n");
    LOGPRINTF("\nBegin new Logging session at ");

    int now_day = day();
    int now_month = month();
    int now_year = year();
    int now_hour = hour();
    int now_minute = minute();
    int now_second = second();
    LOGPRINTF("%.2d/%.2d/%4d ", now_month, now_day, now_year);
    LOGPRINTF("%.2d:%.2d:%.2d\n", now_hour, now_minute, now_second);

    LOGPRINTF("\nDMA SDIO mode.\n");
    LOGPRINTF("Loading configuration...\n");
    flush_logfile();

    initialize_SD_functions(); //  Used by the AUXROM functions

    // the configuration will init the required devices in most cases.....
    //  Note: The "CONFIG.TXT" file name is specified in EBTKS_Global_Data.h
    config_success = loadConfiguration(Config_filename);
  }
  else { //  SD.begin failed
    log_to_serial_ptr += sprintf(log_to_serial_ptr, "Logfile is not active\n");
    logfile_active = false;
    config_success = false;
  }

  //
  //  CRT Emu initialization must occur after CONFIG.TXT processing, as it depends on parameters from that process.
  //  Note: if config failed (for example, SD Card not plugged in) then many parameters may be wrong
  //
  if (config_success) //  Can't emulate the CRT if we don't know if we have a HP85 style CRT controller, or HP86/87 CRT
  {
    initCrtEmu();
  }
  log_to_serial_ptr += sprintf(log_to_serial_ptr, "get_machineNum(): %3d\n", get_machineNum());

//
//  EMC initialization must occur after CONFIG.TXT processing, as it depends on parameters from that process.
//
#if ENABLE_EMC_SUPPORT
  if (config_success && get_EMC_Enable()) {
    emc_init();
  }
#endif

  Serial.begin(115200); //  USB Serial Baud value is irrelevant for this serial channel

  setupPinChange(); //  Set up the two critical interrupt handlers on Pin Change (rising) on Phi 1 and Phi 2

  EBTKS_delay_ns(10000); //  10 us
  SCOPE_1_Pulser(7);     //        We are about to enable Pin Change Interrupts. Once we do, we should not have any more calls to SCOPE_1_Pulser()
                         //        2/7/2021 This occurs about XXXX ms after the previous call to Pulser(2)
  EBTKS_delay_ns(10000); //  10 us

  RELEASE_PWO_OUT; // This should allow the HP-85 to start up, and we are ready to serve. Note that this asynchronous.
  //
  //  Hold off enabling interrupts until PWO has gone high (other devices or the Power supply may be asserting it)
  //
  WAIT_WHILE_PWO_LOW;

  //  Here we enable interrupts, but the end of PWO is asynchronous to the clocks. So first we sync
  //  up to the falling edge of Phi 2
  if (IS_PHI_2_HIGH) {
    WAIT_WHILE_PHI_2_HIGH;
  }
  //  Phi 2 is now low (or worst case has just gone high)
  //  but it may have been low, maybe even just before the rising edge, so sync needs to be sure we have a falling edge
  WAIT_WHILE_PHI_2_LOW;
  WAIT_WHILE_PHI_2_HIGH;
  //
  //  Now we are in sync, just after Phi 2 Falling edge, so there shouldn't be any edge interrupts while we clear out any pending ones
  //
  delayNanoseconds(100);               //  Split the 200 ns delay into two pieces, so we have 100 ns later (see 3 lines below)
  PHI_1_and_2_ISR = PHI_1_and_2_ISR;   //  Clear any pending interrupts
  PHI_1_and_2_IMR = 0;                 //  Still mask the pin interrupts.
  delayNanoseconds(100);               //  Other half of 200 ns delay. Put here in case there are synchronizer delays after changing
                                       //  ISR/IMR registers.
  NVIC_CLEAR_PENDING(IRQ_GPIO6789);    //  Even though interrupts have been disabled, the NVIC can still set its pending flags
                                       //  thus leading to the undesireable unexpected interrupt when the interrupts are enabled.
  NVIC_ENABLE_IRQ(IRQ_GPIO6789);       //  Enable interrupt processing, and processing of all bus requests to ROM and RAM, etc.
                                       //  At this point, the ISR's are active, bus cycles are being monitored, and if matched
                                       //  to bank-switched ROM, or RAM that we are supporting, those are handled in the ISRs
                                       //  There appears to be a short PWO high for 2 ms, so let's wait a bit and make sure
                                       //  real PWO high is 187 ms later
  PHI_1_and_2_IMR = (BIT_MASK_PHASE1); //  Enable Phi 1 only.  2020_12_06

  delay(10); //  Wait 10 ms before falling into loop()  This might be BAD. What if a device op occurs while we are waiting?    ########
             //  Seems there are some issues of loop functions interfering with the Service ROM initial sanity check,
             //  Specifically the call to Three_Shift_Clicks_Poll() which does DMA every 10 ms  (code currently moved to Deprecated_Code.cpp)

  //
  //  We won't get to this point in the code if the HP-85 is not running with valid clocks and PWO
  //

  log_to_serial_ptr += sprintf(log_to_serial_ptr, "Config Success is %s\n", config_success ? "true" : "false");
  log_to_serial_ptr += sprintf(log_to_serial_ptr, "Parameters from the CONFIG.TXT (or defaults)\n");
  log_to_serial_ptr += sprintf(log_to_serial_ptr, "Machine number     %d\n", get_machineNum());
  log_to_serial_ptr += sprintf(log_to_serial_ptr, "Machine type       %s\n", get_machineType());
  log_to_serial_ptr += sprintf(log_to_serial_ptr, "CRT Verbose        %s\n", get_CRTVerbose() ? "true" : "false");
  log_to_serial_ptr += sprintf(log_to_serial_ptr, "Screen Emu.        %s\n", get_screenEmu() ? "true" : "false");
  log_to_serial_ptr += sprintf(log_to_serial_ptr, "Remote CRT         %s\n", get_CRTRemote() ? "true" : "false");
  log_to_serial_ptr += sprintf(log_to_serial_ptr, "85A 16 KB RAM      %s\n", getHP85RamExp() ? "true" : "false");
  log_to_serial_ptr += sprintf(log_to_serial_ptr, "HP85A/B Tape Emu.  %s\n", get_TapeEn() ? "true" : "false");

#if ENABLE_EMC_SUPPORT
  log_to_serial_ptr += sprintf(log_to_serial_ptr, "EMC Support        %s\n", get_EMC_Enable() ? "true" : "false");
  if (get_EMC_Enable()) {
    log_to_serial_ptr += sprintf(log_to_serial_ptr, "EMC Num Banks      %d, 32 KB each\n", get_EMC_NumBanks());
    log_to_serial_ptr += sprintf(log_to_serial_ptr, "EMC Start Bank     %d\n", get_EMC_StartBank());
    log_to_serial_ptr += sprintf(log_to_serial_ptr, "EMC Start Addr     %08o Octal\n", get_EMC_StartAddress());
    log_to_serial_ptr += sprintf(log_to_serial_ptr, "EMC End Addr       %08o Octal\n", get_EMC_EndAddress());
  }
#endif

  //
  //  This should be the end of our writing to this buffer for these startup messages.
  //  All future messages just use Serial.printf()
  //

  Serial.flush();
  Serial.printf("Waiting for HP85 to get to prompt\n");

  Initial_LED_load(false); //  turn the LEDs off, if they are on
  delay(1000);
  Initial_LED_load(false); //  turn the LEDs off, if they are on

  Logic_Analyzer_Event_Count_Init = -1000; // Use this to indicate the Logic analyzer has no default values.

  //
  //  hook in time/date to the filesystem
  //
  FsDateTime::setCallback(dateTime);

  // TB: Startup the USB Host
  usb.begin();

  //  Fall into loop()
  loop_count = 0;
}

#define TRACE_LOOPTRANSLATOR_TIMING (0)

#if TRACE_LOOPTRANSLATOR_TIMING
uint32_t loopTranslator_entry_time;
uint32_t loopTranslator_duration_ms;
#endif

// Main loop
void loop() {
  if (IS_PWO_LOW) {
    SCB_AIRCR = 0x05FA0004; //  Do a processor reset, so we don't get to the next line.
  }
  Serial_Command_Poll(); //  84 ns when idle, but 800 ns ish if Phi 1 interrupt occurs while running
  tape.poll();           //  72 ns when idle, but 800 ns ish if Phi 1 interrupt occurs while running
  AUXROM_Poll();         //  32 ns when idle, but 760 ns ish if Phi 1 interrupt occurs while running
  Cpm85RmidleProbe::service(); // optional task-side CRT redraw, NEVER inside HP bus ISR
  Logic_Analyzer_Poll(); //  54 ns when idle, but 780 ns ish if Phi 1 interrupt occurs while running

  // Handle USB communication, slow this down a bit to every 10ms
  if ((systick_millis_count % 2) == 0) usb.Task();

#if TRACE_LOOPTRANSLATOR_TIMING
  loopTranslator_entry_time = systick_millis_count;
  // Serial.printf("SDWRITE entry. Time since last exit %d ms  ,", loopTranslator_entry_time - loopTranslator_prior_exit_time);
#endif

  loopTranslator(); //  1MB5 / HPIB / DISK poll
                    //  150 ns when idle, but 920 ns ish if Phi 1 interrupt occurs while running
#if TRACE_LOOPTRANSLATOR_TIMING
  loopTranslator_duration_ms = systick_millis_count - loopTranslator_entry_time;
  if (loopTranslator_duration_ms > 1) {
    Serial.printf("loopTranslator() took %d ms\n", loopTranslator_duration_ms);
  }
#endif

  // Always service an incoming ESP32 request, even if remote CRT is
  // disabled in CONFIG.TXT: CP/M disk service and CPM85 keys need Serial2.
  // Poll only if bytes are waiting; no idle JSON/deserialization overhead.
  if (Serial2.available()) esp32.poll();
  // One bounded 4-KiB SD write per loop during CPDMK. No UART image payload.
  Cpm85Image::service();

  // TB added USB Printer
  if (seikoPrinter.connected() && !usbPrinter_OK) {
    usbPrinter_OK = true;

    Serial.printf("SII printer connected VID:PID=%04X:%04X, OUT=0x%02X packet=%u\n",
                  seikoPrinter.idVendor(),
                  seikoPrinter.idProduct(),
                  seikoPrinter.outEndpointAddress(),
                  seikoPrinter.outPacketSize());
    /*
    const char message[] = "Hello World\n";
    const size_t written = seikoPrinter.write(
        reinterpret_cast<const uint8_t *>(message), sizeof(message) - 1);

    Serial.printf("Printed %u of %u bytes\n",
                  static_cast<unsigned>(written),
                  static_cast<unsigned>(sizeof(message) - 1));
    */
    // Find a Hpip USB printer in the device map and inject the seiko usb printer.
    
    HpibPrintUSB* prt = (HpibPrintUSB*)findFirstDevice(HPDEV_PRTUSB);
    if (prt!=NULL) {
      Serial.printf("attaching to HPIB printer at address ");
      Serial.println((unsigned int)prt, HEX);
      prt->connectUSB(&seikoPrinter);
    } 
    else {
      Serial.printf("Configuration entry for HPIB-USB printer (type=1) not found.\n");
    }
  }

  if (!seikoPrinter.connected() && usbPrinter_OK) {
    usbPrinter_OK = false;
    HpibPrintUSB* prt = (HpibPrintUSB*)findFirstDevice(HPDEV_PRTUSB);
    if (prt!=NULL) {
      prt->disconnectUSB();
    }
    Serial.printf("SII printer disconnected\n");
  }
  // End TB added USB Printer

  if (!LEDs_Initialized) {
    if (systick_millis_count > 2000) {
      Initial_LED_load(true); //  Set LEDs to initial value
    }
    else {
      Initial_LED_load(false); //  Turn LEDs off
    }
  }

  if (CRT_Boot_Message_Pending) //  8 ns test is false, but 800 ns ish if Phi 1 interrupt occurs while running
  {                             //  which is quite rare because the window is so narrow
    if (is_hp85_idle()) {
      if (!SD_begin_OK) {
        no_SD_card_message();
      }
      else {
        Boot_Messages_to_CRT();
      }
      CRT_Boot_Message_Pending = false;
    }
    else {
      //  Nothing to do, keep waiting
    }
  }

  //
  //  Here are things we do no more than once per second
  //

  if ((one_second_timer + 1000) < systick_millis_count) //  systick_millis_count rolls over about every 49 days and so does one_second_timer
  {                                                     //  12 ns mostly, but 800 ns ish if Phi 1 interrupt occurs while running
    one_second_timer = systick_millis_count;            //  which is quite rare because the window is so narrow
    //
    //  Things to check
    //
    //  1   If Serial is active and the serial log has not been sent, send it now.
    //  2   If the text sent to the CRT has not yet been also sent to Serial, send it now.
    //
    if (Serial) {
      if (Serial_Log_Message_Pending) {
        if (strlen(Serial_Log_Buffer) > SERIAL_LOG_BUFFER_SIZE) {
          Serial.printf("\nWarning Warning Warning Warning Warning Warning Warning SERIAL_LOG_BUFFER_SIZE is too small\n");
          LOGPRINTF("\nWarning Warning Warning Warning Warning Warning Warning SERIAL_LOG_BUFFER_SIZE is too small\n"); //  Clearly this is inefficient. fix it later
        }
        Serial.printf("\nSerial Log buffer usage is %d of %d charcters\n\n", strlen(Serial_Log_Buffer), SERIAL_LOG_BUFFER_SIZE);
        LOGPRINTF("\nSerial Log buffer usage is %d of %d charcters\n", strlen(Serial_Log_Buffer), SERIAL_LOG_BUFFER_SIZE); //  Clearly this is inefficient. fix it later
        Serial.printf("%s\n", Serial_Log_Buffer);
        Serial_Log_Message_Pending = false;
      }
      if (CRT_Boot_Message_Pending_to_Serial) {
        if (strlen(CRT_Log_Buffer) > CRT_LOG_BUFFER_SIZE) {
          Serial.printf("\nWarning Warning Warning Warning Warning Warning Warning CRT_LOG_BUFFER_SIZE is too small\n");
          LOGPRINTF("\nWarning Warning Warning Warning Warning Warning Warning CRT_LOG_BUFFER_SIZE is too small\n"); //  Clearly this is inefficient. fix it later
        }
        Serial.printf("\nCRT Log buffer usage is %d of %d characters\n\n", strlen(CRT_Log_Buffer), CRT_LOG_BUFFER_SIZE);
        LOGPRINTF("CRT Log buffer usage is %d of %d characters\n", strlen(CRT_Log_Buffer), CRT_LOG_BUFFER_SIZE); //  Clearly this is inefficient. fix it later
        Serial.printf("%s\n", CRT_Log_Buffer);
        Serial.printf("EBTKS> ");
        CRT_Boot_Message_Pending_to_Serial = false;
      }
    }
    flush_logfile();
  }

  loop_count++; //  Keep track of how many times we loop. We don't currently use this for anything,
                //  and it probably overflows somewhere between hourly to daily.
}

//
//  The HP85 idle loop includes label "EXEC" at address 000072. This is a relatively
//  tight loop. On any given call to this function, there is a random chance that the
//  current value of the address register is this address. If we call this function
//  in a "tight" loop, eventually we will get a hit. This is reasonable, since this
//  function is only called from one place, it is a tight loop, and we don't have
//  anything better to do, until the HP85 gets to its idle loop.
bool is_hp85_idle(void) {
  if (addReg == 000072) {
    return true;
  }
  else {
    return false;
  }
}

//
//  During boot the CRT is unavailable. So we store all the boot status info in
//  CRT_Log_Buffer.
//  Once the CRT is available (detected by seeing the HP85 making references
//  to 000072, which is part of the EXEC loop), we dump the array to the CRT
extern void Safe_Write_CRTBAD(uint16_t badAddr);
extern void Safe_Write_CRTSAD(uint16_t sadAddr);

void Boot_Messages_to_CRT(void) {

  int row, column;
  int seg_length;
  char segment[100];

  log_to_CRT_ptr = &CRT_Log_Buffer[0];
  if ((strlen(log_to_CRT_ptr) == 0) || (!get_CRTVerbose())) {
    return;
  }

  row = 1; //  Start at line 1 so there will be a blank line at the top of the screen
  column = 0;

  while (*log_to_CRT_ptr) {
    seg_length = strcspn(log_to_CRT_ptr, "\n");       //  Number of chars not in second string, so does not count the '\n'
    strlcpy(segment, log_to_CRT_ptr, seg_length + 1); //  Copies the segment, and appends a 0x00
    log_to_CRT_ptr += seg_length + 1;                 //  Skip over the segment and the trailing '\n'
    Write_on_CRT_Alpha(row++, column, segment);
    if (row < 16) // While on screen, add some delay between lines. After line 16, no need for delays, since it isn't seen
    {
      delay(40); //  A little delay so they see it happening
    }
    if (row > 63) //  ##########  These constants for line number are specific to HP85A/B.
    {             //  ##########  If we have a HP86/87, there are multiple ALPHA modes each with different max lines
      row = 63;   //  Just over-write the last line. Note we start on line 1, so max lines is 63. We leave line 0 alone.
    }
    if (row == 15) {
      Write_on_CRT_Alpha(row++, column, "Scroll up for more");
    }
  }
  //
  // Write_on_CRT_Alpha(0,0, first_char);
  //
  Safe_Write_CRTBAD(0);
  Safe_Write_CRTSAD(0);
}

int get_wifi_key() {
  return esp32.getKybdCh();
}
