////////////////////////////////////////////////////////////////
//
//      Logic Analyzer Configurations
//
//  
//  Several configurations have been deleted from this document as they
//  are no longer relevant. The production V3.0 EBTKS has built in logic
//  analyzer ports for 3 pre-wired 8 channel pods, and 1 uncommited 8
//  channel pod. The physical layout for each pod has 8 ground pins
//  towards the back edge of the PCB, and 8 active pins on the side closer
//  to Teensy 4.1 . i.e. pins 1,3,5,7,9,11,13,15 are grounds. Designed for
//  Tektronix P6418 logic analyzer pods, used with a TLA5201 or similar.
//
//. Note: moved out of the main code for readability.  
//  
//      Channel         Signal
//
//      C3-7            B7X
//      C3-6            B6X
//      C3-5            B5X
//      C3-4            B4X
//      C3-3            B3X
//      C3-2            B2X
//      C3-1            B1X
//      C3-0            B0X
//
//      C2-7            IFETCH
//      C2-6            /LMA
//      C2-5            /RD
//      C2-4            /WR
//      C2-3            RC
//      C2-2            BUFEN
//      C2-1            SCOPE_1
//      C2-0            SCOPE_2
//
//      A3-7            IRLX
//      A3-6            CTRLEN
//      A3-5            CTRLDIR
//      A3-4            HALTX
//      A3-3            INTPRI
//      A3-2            IPRIH
//      A3-1            PWO
//      A3-0            Phase_1
//
//      A2-7
//      A2-6
//      A2-5
//      A2-4
//      A2-3
//      A2-2
//      A2-1
//      A2-0
//
//
////////////////////////////////////////////////////////////////        Assumptions, Expectation, Requirements.
//
//      This code involves very carefully crafted real-time sequences
//      and interrupt handlers.
//
//      The Interrupt handlers must be very fast, and lock out other
//      activity for the minimal amount of time. At the time of writing,
//      the ONLY interrupt handlers are this file's routines that handle
//      all HP-85 bus transaction, and the interrupts that occur in
//      the TeensyDuino USB handler (for serial over USB).
//      The highest priority is the Temperature Monitor (IRQ 63 and 64)
//         (don't know if there is a handler)
//      The next highest is the HP-85 bus transaction related interrupts
//      and then everything else.
//      There is code in the TeensyDuino library that uses the global
//      __disable_irq() and __enable_irq() . This is unacceptable.
//      The USB is an example (at the time of this writing) with this
//      issue. So it needs to be edited as described in
//              Teensy_4.0_Notes.txt
//      So no long 'if chains' allowed in the bus transaction code.
//      Where necessary, use tables to jump directly to required
//      code, even if very inefficient use of memory. If this leads to
//      tasks that will take more than 100ns to execute, then the
//      right approach is to set a flag, and have the task performed
//      in the background. That code should do round-robin testing of
//      all possible such flags
//
//      All of the code here, which depends on Pin Change interrupts
//         (Specifcally Phi 1 rising and Phi 2 Rising)
//      assume that the ONLY pin interrupts are these 2. There is code
//      that makes changes to mask registers, and NVIC registers based
//      this assumption. Changing the code to support more than these
//      two interrupts, would probably require a wholesale rewrite, so
//      a redesign would be required.
//
//      More needs to be written here!
//
//
////////////////////////////////////////////////////////////////

/*    Signal Name <-> Pin Bit Number <-> Bit Position <-> GPIO Group <-> IOMUX Control

The #defines for these are in core_pins.h at line 516 (for teensy 4.1)

The Signal Names come from the schematic for EKBTS V2.0

Core Pin Numbers physical position on Teensy 4.1

                  GND  USB    Vin     Vin
     BUFEN        0           GND     GND
  CTRL_DIR        1           3.3V    3.3V
NEOPIX_TSY        2           23      RD
    INTPRI        3           22      LMA
       T04        4           21      PHASE1
       T05        5           20      WR
       T06        6           19      DB0
       TXD        7           18      DB1
       RXD        8           17      DB6
       T09        9           16      DB7
       T10        10          15      DB3
       T11        11          14      DB2
       T12        12          13      T13  (LED)
      3.3V        3.3V        GND     GND
       SCL        24          41      DB5
       SDA        25          40      DB4
       T26        26          39      SCOPE_2
    DIR_RC        27          38      PHASE2
     HALTX        28          37      INTERRUPT
    CTRLEN        29          36      PWO_O
  IPRIH_IN        30          35      PHASE21
      PWO_L       31   SD     34      PHASE12
     IFETCH       32  Card    33      SCOPE_1


EBTKS           Core               Bit      GPIO        IOMUX
Signal          Pin                         group
Name            Number

BUFEN           CORE_PIN0_BIT       3       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B0_03
CTRL_DIR        CORE_PIN1_BIT       2       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B0_02
NEOPIX_TSY      CORE_PIN2_BIT       4       GPIO9       IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_04
INTPRI          CORE_PIN3_BIT       5       GPIO9       IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_05
T04             CORE_PIN4_BIT       6       GPIO9       IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_06
T05             CORE_PIN5_BIT       8       GPIO9       IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_08
T06             CORE_PIN6_BIT      10       GPIO7       IOMUXC_SW_MUX_CTL_PAD_GPIO_B0_10
TXD             CORE_PIN7_BIT      17       GPIO7       IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_01
RXD             CORE_PIN8_BIT      16       GPIO7       IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_00
T09             CORE_PIN9_BIT      11       GPIO7       IOMUXC_SW_MUX_CTL_PAD_GPIO_B0_11
T10             CORE_PIN10_BIT      0       GPIO7       IOMUXC_SW_MUX_CTL_PAD_GPIO_B0_00
T11             CORE_PIN11_BIT      2       GPIO7       IOMUXC_SW_MUX_CTL_PAD_GPIO_B0_02
T12             CORE_PIN12_BIT      1       GPIO7       IOMUXC_SW_MUX_CTL_PAD_GPIO_B0_01
T13             CORE_PIN13_BIT      3       GPIO7       IOMUXC_SW_MUX_CTL_PAD_GPIO_B0_03
DB2             CORE_PIN14_BIT     18       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_02
DB3             CORE_PIN15_BIT     19       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_03
DB7             CORE_PIN16_BIT     23       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_07
DB6             CORE_PIN17_BIT     22       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_06
DB1             CORE_PIN18_BIT     17       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_01
DB0             CORE_PIN19_BIT     16       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_00
WR              CORE_PIN20_BIT     26       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_10
PHASE1          CORE_PIN21_BIT     27       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_11
LMA             CORE_PIN22_BIT     24       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_08
RD              CORE_PIN23_BIT     25       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_09
SCL             CORE_PIN24_BIT     12       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B0_12
SDA             CORE_PIN25_BIT     13       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B0_13
T26             CORE_PIN26_BIT     30       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_14
DIR_RC          CORE_PIN27_BIT     31       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_15             NOTE: This DIR/RC on the schematic
HALTX           CORE_PIN28_BIT     18       GPIO8       IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_32
CTRLEN          CORE_PIN29_BIT     31       GPIO9       IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_31
IPRIH_IN        CORE_PIN30_BIT     23       GPIO8       IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_37
PWO_L           CORE_PIN31_BIT     22       GPIO8       IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_36
IFETCH          CORE_PIN32_BIT     12       GPIO7       IOMUXC_SW_MUX_CTL_PAD_GPIO_B0_12
SCOPE_1         CORE_PIN33_BIT      7       GPIO9       IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_07
PHASE12         CORE_PIN34_BIT     29       GPIO7       IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_13
PHASE21         CORE_PIN35_BIT     28       GPIO7       IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_12
PWO_O           CORE_PIN36_BIT     18       GPIO7       IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_02
INTERRUPT       CORE_PIN37_BIT     19       GPIO7       IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_03
PHASE2          CORE_PIN38_BIT     28       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_12
SCOPE_2         CORE_PIN39_BIT     29       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_13
DB4             CORE_PIN40_BIT     20       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_04
DB5             CORE_PIN41_BIT     21       GPIO6       IOMUXC_SW_MUX_CTL_PAD_GPIO_AD_B1_05

This table sorts the Core Pin Number by GPIO group, and bit position within the 32 bit GPIO port
Blank lines separate a change in GPIO port and also  where bits are not contiguous

GPIO    Core      Bit          Pre defined
Group   Pin       Position     GPIO Data
        Number    in DR Reg    Register pointer

GPIO6    1         2           CORE_PIN1_PORTREG
GPIO6    0         3           CORE_PIN0_PORTREG

GPIO6   24        12           CORE_PIN24_PORTREG
GPIO6   25        13           CORE_PIN25_PORTREG

GPIO6   19        16           CORE_PIN19_PORTREG
GPIO6   18        17           CORE_PIN18_PORTREG
GPIO6   14        18           CORE_PIN14_PORTREG
GPIO6   15        19           CORE_PIN15_PORTREG
GPIO6   40        20           CORE_PIN40_PORTREG
GPIO6   41        21           CORE_PIN41_PORTREG
GPIO6   17        22           CORE_PIN17_PORTREG
GPIO6   16        23           CORE_PIN16_PORTREG
GPIO6   22        24           CORE_PIN22_PORTREG
GPIO6   23        25           CORE_PIN23_PORTREG
GPIO6   20        26           CORE_PIN20_PORTREG
GPIO6   21        27           CORE_PIN21_PORTREG
GPIO6   38        28           CORE_PIN38_PORTREG
GPIO6   39        29           CORE_PIN39_PORTREG
GPIO6   26        30           CORE_PIN26_PORTREG
GPIO6   27        31           CORE_PIN27_PORTREG

GPIO7   10         0           CORE_PIN10_PORTREG
GPIO7   12         1           CORE_PIN12_PORTREG
GPIO7   11         2           CORE_PIN11_PORTREG
GPIO7   13         3           CORE_PIN13_PORTREG

GPIO7    6        10           CORE_PIN6_PORTREG
GPIO7    9        11           CORE_PIN9_PORTREG
GPIO7   32        12           CORE_PIN32_PORTREG

GPIO7    8        16           CORE_PIN8_PORTREG
GPIO7    7        17           CORE_PIN7_PORTREG
GPIO7   36        18           CORE_PIN36_PORTREG
GPIO7   37        19           CORE_PIN37_PORTREG

GPIO7   35        28           CORE_PIN35_PORTREG
GPIO7   34        29           CORE_PIN34_PORTREG

GPIO8   28        18           CORE_PIN28_PORTREG

GPIO8   31        22           CORE_PIN31_PORTREG
GPIO8   30        23           CORE_PIN30_PORTREG

GPIO9    2         4           CORE_PIN2_PORTREG
GPIO9    3         5           CORE_PIN3_PORTREG
GPIO9    4         6           CORE_PIN4_PORTREG
GPIO9    5         8           CORE_PIN5_PORTREG

GPIO9   29        31           CORE_PIN29_PORTREG
GPIO9   33         7           CORE_PIN33_PORTREG


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                Main Uses of DMAMEM
//
//    256 KB      EMC for the HP85B, HP86/87                EMC_MAX_BANKS * 32kB EBTKS_Bus_Interface_ISR.cpp
//
//    144 KB      roms[][]                                  MAX_ROMS            EBTKS_Config.h (assumes MAX_ROMS is 18)
//                                                          ROM_PAGE_SIZE       EBTKS_Config.h
//      0.5 KB    Copy Buffer for MOUNT/copy_sd_file()      COPY_BUFFER_SIZE    EBTKS_AUXROM_SD_Services.cpp
//      1 KB      sprintf_result                            SCRATCHLENGTH       EBTKS_AUXROM_SD_Services.cpp
//      1 KB      string_arg                                SCRATCHLENGTH       EBTKS_AUXROM_SD_Services.cpp
//      0.1 KB    format_segment                                                EBTKS_AUXROM_SD_Services.cpp
//
//    436,324 B   Total.  Actual total from linker on 12/15/2020 is 473,312
//    412,160 B   Total.  Actual total from linker on  3/28/2021 is 424,672
//
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
