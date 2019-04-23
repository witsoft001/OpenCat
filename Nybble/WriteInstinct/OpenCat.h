/*
    Skill class holds only the lookup information of joint angles.
    One frame of joint angles defines a static posture, while a series of frames defines a periodic motion, usually a gait.
    Skills are instantiated as either:
      instinct  (trained by Rongzhong Li, saved in external i2c EERPOM) or
      newbility (taught by other users, saved in PROGMEM)
    A well-tuned (finalized) newbility can also be saved in external i2c EEPROM. Remember that EEPROM has very limited (1,000,000) write cycles!

    SkillList (inherit from QList class) holds a mixture of instincts and newbilities.
    It also provides a dict(key) function to return the pointer to the skill.
    Initialization information(individual skill name, address) for SkillList is stored in on-board EEPROM

    Behavior list (inherit from QList class) holds a time dependent sequence of multiple skills, triggered by certain perceptions.
    It defines the order, speed, repetition and interval of skills。
    (Behavior list is yet to be implemented)

    Motion class uses the lookup information of a Skill to construct a Motion object that holds the actual angle array.
    It also implements the reading and writing functions in specific storage locations.
    Considering Arduino's limited SRAM, you should create only one Motion object and update it when loading new skills.

    instinct(external EEPROM) \
                                -- skill that contains only lookup information
    newbility(progmem)        /

    Skill list: skill1, skill2, skill3,...
                              |
                              v
                           motion that holds actual joint angle array in SRAM

    Behavior list: skill3(speed, repetition and interval), skill1(speed, repetition and interval), ...

    **
    Updates: One Skill object in the SkillList takes around 20 bytes in SRAM. It takes 200+ bytes for 15+ skills.
    On a tiny atmega328 chip with only 2KB SRAM, I'm implementing the Skills and SkillList in the on-board EEPROM。
    Now the skill list starts from on-board EEPROM address SKILLS.
    Format:
    1 byte skill_1 nameLength + char string name1 + 1 char skillType1 + 1 int address1,
    1 byte skill_2 nameLength + char string name2 + 1 char skillType2 + 1 int address2,
    ...
    The iterator can traverse the list with the string length of each skill name.

    The Skill and SkillList classes are obsolete in the atmega328 implementation but are still included in this header file.
    **

  Rongzhong Li
  September 2018

  Copyright (c) 2019 Petoi LLC.

  The MIT License

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

*/

#include "Instinct.h" //postures and movements trained by RongzhongLi
//#define DEVELOPER
#ifdef DEVELOPER
#include <MemoryFree.h> //http://playground.arduino.cc/Code/AvailableMemory
#include <QList.h> //https://github.com/SloCompTech/QList
#endif

// credit to Adafruit PWM servo driver library
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <EEPROM.h>
//#include <avr/eeprom.h> // doesn't work. <EEPROM.h> works

//abbreviations
#define PT(s) Serial.print(s)  //makes life easier
#define PTL(s) Serial.println(s)
#define PTF(s) Serial.print(F(s))//trade flash memory for dynamic memory with F() function
#define PTLF(s) Serial.println(F(s))

//board configuration
#define IR_RECIEVER 4 // Signal Pin of IR receiver to Arduino Digital Pin 4
#define BUZZER 5
#define GYRO
#define ULTRA_SOUND
#define BATT A0

#ifdef ULTRA_SOUND
#define VCC 8
#define TRIGGER 9
#define ECHO 10
#define LONGEST_DISTANCE 200 // 200 cm = 2 meters
float farTime =  LONGEST_DISTANCE * 2 / 0.034;
#endif

void beep(byte note, float duration = 10, int pause = 0, byte repeat = 1 ) {
  if (note == 0) {//rest note
    analogWrite(BUZZER, 0);
    delay(duration);
    return;
  }
  int freq = 220 * pow(1.059463, note - 1); // 1.059463 comes from https://en.wikipedia.org/wiki/Twelfth_root_of_two
  float period = 1000000.0 / freq;
  for (byte r = 0; r < repeat; r++) {
    for (float t = 0; t < duration * 1000; t += period) {
      analogWrite(BUZZER, 150);      // Almost any value can be used except 0 and 255
      // experiment to get the best tone
      delayMicroseconds(period / 2);        // rise for half period
      analogWrite(BUZZER, 0);       // 0 turns it off
      delayMicroseconds(period / 2);        // down for half period
    }
    delay(pause);
  }
}
void playMelody(int start) {
  byte len = (byte)EEPROM.read(start) / 2;
  for (int i = 0; i < len; i++)
    beep(EEPROM.read(start - 1 - i), 1000 / EEPROM.read(start - 1 - len - i), 100);
}

void meow(int repeat = 1, int pause = 200, int startF = 50,  int endF = 200, int increment = 5) {
  for (int r = 0; r < repeat; r++) {
    for (int amp = startF; amp <= endF; amp += increment) {
      analogWrite(BUZZER, amp);
      delay(15); // wait for 15 milliseconds to allow the buzzer to vibrate
    }
    delay(500);
    analogWrite(BUZZER, 0);
    delay(pause);
  }
}

#ifdef NyBoard_V0_1
byte pins[] = {7, 0, 8, 15,
               6, 1, 14, 9,
               5, 2, 13, 10,
               4, 3, 12, 11
              };
#else
#ifdef NyBoard_V0_2
byte pins[] = {4, 3, 11, 12,
               5, 2, 13, 10,
               6, 1, 14, 9,
               7, 0, 15, 8
              };
#endif
#endif

#ifdef NYBBLE
#define HEAD
#define TAIL
#define X_LEG
#define WALKING_DOF 8

#else
#ifdef BIT
//#define MPU_YAW180
#define LL_LEG
#define WALKING_DOF 8
#endif
#endif
//remap pins for different walking modes, pin4 ~ pin15
byte fast[] = {
  4, 4, 7, 7,
  8, 8, 11, 11,
  12, 12, 15, 15
};
byte slow[] = {
  5, 5, 8, 8,
  9, 9, 10, 10,
  13, 13, 14, 14
};
byte left[] = {
  5, 4, 7, 6,
  9, 8, 11, 10,
  13, 12, 15, 14
};
byte right[] = {
  4, 5, 6, 7,
  8, 9, 10, 11,
  12, 13, 14, 15
};

//on-board EEPROM addresses
#define MELODY 1023 //melody will be saved at the end of the 1KB EEPROM, and is read reversely. That allows some flexibility on the melody length. 
#define PIN 0                 // 16 byte array
#define CALIB 16              // 16 byte array
#define MID_SHIFT 32          // 16 byte array
#define ROTATION_DIRECTION 48 // 16 byte array
#define SERVO_ANGLE_RANGE 64  // 16 byte array
#define MPUCALIB 80           // 16 byte array
#define FAST 96               // 16 byte array
#define SLOW 112              // 16 byte array
#define LEFT 128              // 16 byte array
#define RIGHT 144             // 16 byte array

#define ADAPT_PARAM 160          // 16 x NUM_ADAPT_PARAM byte array
#define NUM_ADAPT_PARAM  2    // number of parameters for adaption
#define SKILLS 200         // 1 byte for skill name length, followed by the char array for skill name
// then followed by i(nstinct) on progmem, or n(ewbility) on progmem

//servo constants
#define DOF 16
#define PWM_FACTOR 4
#define MG92B_MIN 170*PWM_FACTOR
#define MG92B_MAX 550*PWM_FACTOR
#define MG92B_RANGE 150

#define MG90D_MIN 158*PWM_FACTOR //so mg92b and mg90 are not centered at the same signal
#define MG90D_MAX 515*PWM_FACTOR
#define MG90D_RANGE 150

#define KUDO_MIN 115*PWM_FACTOR
#define KUDO_MAX 621*PWM_FACTOR
#define KUDO_RANGE 220

// called this way, it uses the default address 0x40
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
// you can also call it with a different address you want
//Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x41);

// Depending on your servo make, the pulse width min and max may vary, you
// want these to be as small/large as possible without hitting the hard stop
// for max range. You'll have to tweak them as necessary to match the servos you
// have!
#ifndef KUDO
#define SERVOMIN  MG92B_MIN // this is the 'minimum' pulse length count (out of 4096)
#define SERVOMAX  MG92B_MAX // this is the 'maximum' pulse length count (out of 4096)
#define SERVO_ANG_RANGE MG92B_RANGE
#else
#define SERVOMIN  KUDO_MIN // this is the 'minimum' pulse length count (out of 4096)
#define SERVOMAX  KUDO_MAX // this is the 'maximum' pulse length count (out of 4096)
#define SERVO_ANG_RANGE KUDO_RANGE
#endif

#define PWM_RANGE (SERVOMAX - SERVOMIN)

float degPerRad = 180 / M_PI;
float radPerDeg = M_PI / 180;

// tone: pause,1,  2,  3,  4,  5,  6,  7,  1,  2, 3,
// code: 0,    1,  3,  5,  6,  8,  10, 12, 13, 15, 17
byte melody[] = {8, 13, 10, 13, 8,  0,  5,  8,  3,  5, 8,
                 8, 8,  32, 32, 8, 32, 32, 32, 32, 32, 8,
                };
/*byte pins[] = {16, 16, 16, 16,
               16, 16, 16, 16,
               2, 3, 13, 12,
               0, 1, 15, 14
              };*/
//byte pins[] = {11, 12, 4, 16, 16, 16, 16, 16, 9,14,1,6,8,15,0,7};//tail version

int8_t calibs[] = {0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};
int8_t middleShifts[] = {0, 15, 0, 0,
                         -45, -45, -45, -45,
                         0, 0, 0, 0,
                         0, 0, 0, 0
                        };
int8_t rotationDirections[] = {1, -1, 1, 1,
                               1, -1, 1, -1,
                               1, -1, -1, 1,
                               -1, 1, 1, -1
                              };
#ifndef KUDO
byte servoAngleRanges[] =  {MG90D_RANGE, MG90D_RANGE, MG90D_RANGE, MG90D_RANGE,
                            MG92B_RANGE, MG92B_RANGE, MG92B_RANGE, MG92B_RANGE,
                            MG92B_RANGE, MG92B_RANGE, MG92B_RANGE, MG92B_RANGE,
                            MG92B_RANGE, MG92B_RANGE, MG92B_RANGE, MG92B_RANGE
                           };

#else
byte servoAngleRanges[] =  {KUDO_RANGE, KUDO_RANGE, KUDO_RANGE, KUDO_RANGE,
                            KUDO_RANGE, KUDO_RANGE, KUDO_RANGE, KUDO_RANGE,
                            KUDO_RANGE, KUDO_RANGE, KUDO_RANGE, KUDO_RANGE,
                            KUDO_RANGE, KUDO_RANGE, KUDO_RANGE, KUDO_RANGE,
                           };
#endif
float pulsePerDegree[DOF] = {};
int8_t servoCalibs[DOF] = {};
char currentAng[DOF] = {};
int calibratedDuty0[DOF] = {};

//This function will write a 2 byte integer to the eeprom at the specified address and address + 1
void EEPROMWriteInt(int p_address, int p_value)
{
  byte lowByte = ((p_value >> 0) & 0xFF);
  byte highByte = ((p_value >> 8) & 0xFF);
  EEPROM.update(p_address, lowByte);
  EEPROM.update(p_address + 1, highByte);
}

//This function will read a 2 byte integer from the eeprom at the specified address and address + 1
int EEPROMReadInt(int p_address)
{
  byte lowByte = EEPROM.read(p_address);
  byte highByte = EEPROM.read(p_address + 1);
  return ((lowByte << 0) & 0xFF) + ((highByte << 8) & 0xFF00);
}

#define DEVICE_ADDRESS 0x50    //I2C Address of AT24C32D eeprom chip
#define WIRE_BUFFER 30 //Arduino wire allows 32 byte buffer, with 2 byte for address.
#define WIRE_LIMIT 16 //That leaves 30 bytes for data. use 16 to balance each writes
#define PAGE_LIMIT 32 //AT24C32D 32-byte Page Write Mode. Partial Page Writes Allowed
#define EEPROM_SIZE (65536/8)
#define SKILL_HEADER 3

bool EEPROMOverflow = false;
void copyDataFromPgmToI2cEeprom(unsigned int &eeAddress, unsigned int pgmAddress) {
  uint8_t period = pgm_read_byte(pgmAddress);//automatically cast to char*
  byte frameSize = period > 1 ? WALKING_DOF : 16;
  int len = period * frameSize + SKILL_HEADER;
  int writtenToEE = 0;
  while (len > 0) {
    Wire.beginTransmission(DEVICE_ADDRESS);
    Wire.write((int)((eeAddress) >> 8));   // MSB
    Wire.write((int)((eeAddress) & 0xFF)); // LSB
    /*PTF("\n* current address: ");
      PTL((unsigned int)eeAddress);
      PTLF("0\t1\t2\t3\t4\t5\t6\t7\t8\t9\ta\tb\tc\td\te\tf\t\n\t\t\t");*/
    byte writtenToWire = 0;
    do {
      if (eeAddress == EEPROM_SIZE) {
        PTL();
        PTL("I2C EEPROM overflow! You must reduce the size of your instincts file!\n");
        EEPROMOverflow = true;
#ifdef BUZZER
        meow(3);
#endif
        return;
      }
      /*PT((int8_t)pgm_read_byte(pgmAddress + writtenToEE));
        PT("\t");*/
      Wire.write((byte)pgm_read_byte(pgmAddress + writtenToEE++));
      writtenToWire++;
      eeAddress++;
    } while ((--len > 0 ) && (eeAddress  % PAGE_LIMIT ) && (writtenToWire < WIRE_LIMIT));//be careful with the chained conditions
    //self-increment may not work as expected
    Wire.endTransmission();
    delay(6);  // needs 5ms for page write
    //PTL("\nwrote " + String(writtenToWire) + " bytes.");
  }
  //PTLF("finish copying to I2C EEPROM");
}

/*
  class Skill {//the whole SkillList routine is replaced by Motion.loadBySkillName()
  public:
    char* skillName; //use char array instead of String to save memory
    int onBoardEepromAddress;
    Skill(char* name, int address): onBoardEepromAddress(address) {
      skillName = new char[strlen(name) + 1];
      strcpy(skillName, name);
    }
    void info() {
      PTL("skill name: " + String(skillName) + ",\ton-board EEPROM address: " + String(onBoardEepromAddress));
    }
  };

  class SkillList: public QList<Skill*> {//the whole SkillList routine is replaced by Motion.loadBySkillName()
  public:
    Skill* dict(char* key) {
      PTL("search for " + String(key) + "\t");
      for (byte idx = 0; idx < this->size(); idx++) {
        PT(this->at(idx)->skillName);
        PT(" ");
        if (!strcmp(this->at(idx)->skillName, key))
          return this->at(idx);
      }
      return (Skill*)NULL;
    }
  };
  SkillList skillList;
*/

class Motion {
  public:
    byte pins[DOF];
    uint8_t period;
    float expectedRollPitch[2];
    char* dutyAngles;
    Motion() {
      period = 0;
      expectedRollPitch[0] = 0;
      expectedRollPitch[1] = 0;
      dutyAngles = NULL;
    }

    int lookupAddressByName(char* skillName) {
      int skillAddressShift = 0;
      for (byte s = 0; s < NUM_SKILLS; s++) {//save skill info to on-board EEPROM, load skills to SkillList
        byte nameLen = EEPROM.read(SKILLS + skillAddressShift++);
        char* readName = new char[nameLen + 1];
        for (byte l = 0; l < nameLen; l++) {
          readName[l] = EEPROM.read(SKILLS + skillAddressShift++);
        }
        readName[nameLen] = '\0';
        if (!strcmp(readName, skillName)) {
          delete[]readName;
          return SKILLS + skillAddressShift;
        }
        delete[]readName;
        skillAddressShift += 3;//1 byte type, 1 int address
      }
      PTLF("wrong key!");
      return -1;
    }
    void loadDataFromProgmem(unsigned int pgmAddress) {
      period = pgm_read_byte(pgmAddress);//automatically cast to char*
      for (int i = 0; i < 2; i++)
        expectedRollPitch[i] = radPerDeg * (int8_t)pgm_read_byte(pgmAddress + 1 + i);
      byte frameSize = period > 1 ? WALKING_DOF : 16;
      int len = period * frameSize;
      //delete []dutyAngles; //check here
      dutyAngles = new char[len];
      for (int k = 0; k < len; k++) {
        dutyAngles[k] = pgm_read_byte(pgmAddress + SKILL_HEADER + k);
      }
    }
    void loadDataFromI2cEeprom(unsigned int &eeAddress) {
      Wire.beginTransmission(DEVICE_ADDRESS);
      Wire.write((int)((eeAddress) >> 8));   // MSB
      Wire.write((int)((eeAddress) & 0xFF)); // LSB
      Wire.endTransmission();
      Wire.requestFrom(DEVICE_ADDRESS, 3);
      period = Wire.read();
      //PTL("read " + String(period) + " frames");
      for (int i = 0; i < 2; i++)
        expectedRollPitch[i] = radPerDeg * (int8_t)Wire.read();
      byte frameSize = period > 1 ? WALKING_DOF : 16;
      int len = period * frameSize;
      //delete []dutyAngles;//check here
      dutyAngles = new char[len];

      int readFromEE = 0;
      int readToWire = 0;
      while (len > 0) {
        //PTL("request " + String(min(WIRE_BUFFER, len)));
        Wire.requestFrom(DEVICE_ADDRESS, min(WIRE_BUFFER, len));
        readToWire = 0;
        do {
          if (Wire.available()) dutyAngles[readFromEE++] = Wire.read();
          /*PT( (int8_t)dutyAngles[readFromEE - 1]);
            PT('\t')*/
        } while (--len > 0 && ++readToWire < WIRE_BUFFER);
        //PTL();
      }
      //PTLF("finish reading");
    }

    void loadDataByOnboardEepromAddress(int onBoardEepromAddress) {
      char skillType = EEPROM.read(onBoardEepromAddress);
      unsigned int dataArrayAddress = EEPROMReadInt(onBoardEepromAddress + 1);
      delete[] dutyAngles;
#ifdef DEVELOPER
      PTF("free memory: ");
      PTL(freeMemory());
#endif
#ifdef I2C_EEPROM
      if (skillType == 'I') { //copy instinct data array from external i2c eeprom
        loadDataFromI2cEeprom(dataArrayAddress);
      }
      else                    //copy newbility data array from progmem
#endif
      {
        loadDataFromProgmem(dataArrayAddress) ;
      }
#ifdef DEVELOPER
      PTF("free memory: ");
      PTL(freeMemory());
#endif
    }

    void loadBySkillName(char* skillName) {//get lookup information from on-board EEPROM and read the data array from storage
      int onBoardEepromAddress = lookupAddressByName(skillName);
      if (onBoardEepromAddress == -1)
        return;
      loadDataByOnboardEepromAddress(onBoardEepromAddress);
    }

    /*    void loadBySkillPtr(Skill* sk) {//obsolete. get lookup information from a skill pointer and read the data array from storage
          loadDataByOnboardEepromAddress(sk->onBoardEepromAddress);
        }
    */

    void info() {
      PTL("period: " + String(period) + ",\tdelayBetweenFrames: " + ",\texpected (pitch,roll): (" + expectedRollPitch[0]*degPerRad + "," + expectedRollPitch[1]*degPerRad + ")");
      for (int k = 0; k < period * (period > 1 ? WALKING_DOF : 16); k++) {
        PT(String((int8_t)dutyAngles[k]) + ", ");
      }
      PTL();
    }
};

Motion motion;

void assignSkillAddressToOnboardEeprom() {
  int skillAddressShift = 0;
  PT("\n* Assigning ");
  PT(sizeof(progmemPointer) / 2);
  PTL(" skill addresses...");
  for (byte s = 0; s < sizeof(progmemPointer) / 2; s++) { //save skill info to on-board EEPROM, load skills to SkillList
    PTL(s);
    byte nameLen = EEPROM.read(SKILLS + skillAddressShift++); //without last type character
    //PTL(nameLen);
    /*for (int n = 0; n < nameLen; n++)
      PT((char)EEPROM.read(SKILLS + skillAddressShift + n));
      PTL();*/
    skillAddressShift += nameLen;
    char skillType = EEPROM.read(SKILLS + skillAddressShift++);
#ifdef I2C_EEPROM
    if (skillType == 'N') // the address of I(nstinct) has been written in previous operation: saveSkillNameFromProgmemToOnboardEEPROM() in instinct.ino
      // if skillType == N(ewbility), save pointer address of progmem data array to onboard eeprom.
      // it has to be done for different sketches because the addresses are dynamically assigned
#endif
      EEPROMWriteInt(SKILLS + skillAddressShift, (int)progmemPointer[s]);
#if defined(I2C_EEPROM) && defined (MAIN_SKETCH)
    else
      s--;
#endif
    skillAddressShift += 2;
  }
  PTLF("Finished!");
}
/*the following function is obsolete
  void loadSkillFromOnboardEepromToSkillList() {//the whole SkillList routine is replaced by Motion.loadBySkillName()
  int skillAddressShift = 0;
  for (byte s = 0; s < NUM_SKILLS; s++) {//save skill info to on-board EEPROM, load skills to SkillList
    byte nameLen = EEPROM.read(SKILLS + skillAddressShift++);
    PTL(nameLen);
    char* skillName = new char[nameLen + 1];
    for (byte l = 0; l < nameLen; l++) {
      skillName[l] = EEPROM.read(SKILLS + skillAddressShift++);
    }
    skillName[nameLen] = '\0';
    skillList.push_back(new Skill(skillName, SKILLS + skillAddressShift));
    char skillType = EEPROM.read(SKILLS + skillAddressShift++);
    if (skillType == 'N') //progmem
      EEPROMWriteInt(SKILLS + skillAddressShift, progmemPointer[s]);
    skillAddressShift += 2;
    delete[] skillName;
  #ifdef DEVELOPER
    Serial.println(freeMemory());
  #endif
  }
  }
*/
inline byte pin(byte idx) {
  return EEPROM.read(PIN + idx);
}
inline byte remapPin(byte offset, byte idx) {
  return EEPROM.read(offset + idx);
}
inline byte servoAngleRange(byte idx) {
  return EEPROM.read(SERVO_ANGLE_RANGE + idx);
}
inline int8_t middleShift(byte idx) {
  return EEPROM.read( MID_SHIFT + idx);
}

inline int8_t rotationDirection(byte idx) {
  return EEPROM.read(ROTATION_DIRECTION + idx);
}
inline int8_t servoCalib(byte idx) {
  return EEPROM.read( CALIB + idx);
}

// balancing parameters
#define ROLL_LEVEL_TOLERANCE 2//the body is still considered as level, no angle adjustment
#define PITCH_LEVEL_TOLERANCE 1
float levelTolerance[2] = {ROLL_LEVEL_TOLERANCE * radPerDeg, PITCH_LEVEL_TOLERANCE * radPerDeg}; //the body is still considered as level, no angle adjustment
#define LARGE_PITCH 75
//the following coefficients will be divided by 10.0 in the adjust() function. so (float) 0.1 can be saved as (int8_t) 1
//this trick allows using int8_t array insead of float array, saving 96 bytes and allows storage on EEPROM
#define panF 60
#define tiltF 60
#define sRF 50   //shoulder roll factor
#define sPF 12 //shoulder pitch factor
#define uRF 30 //upper leg roll factor
#define uPF 30 //upper leg pitch factor
#define lRF (-1.5*uRF) //lower leg roll factor 
#define lPF (-1.5*uPF)//lower leg pitch factor
#define LEFT_RIGHT_FACTOR 2
#define FRONT_BACK_FACTOR 2
#define POSTURE_WALKING_FACTOR 0.5
#ifdef POSTURE_WALKING_FACTOR
float postureOrWalkingFactor;
#endif

#ifdef X_LEG
int8_t adaptiveParameterArray[16][NUM_ADAPT_PARAM] = {
  { -panF, 0}, { -panF, -tiltF}, { -1.3 * panF, 0}, {0, 0},
  {sRF, -sPF}, { -sRF, -sPF}, { -sRF, sPF}, {sRF, sPF},
  {uRF, uPF}, {uRF, uPF}, { -uRF, uPF}, { -uRF, uPF},
  {lRF, lPF}, {lRF, lPF}, { -lRF, lPF}, { -lRF, lPF}
};
#else // >> leg
int8_t adaptiveParameterArray[16][NUM_ADAPT_PARAM] = {
  { -panF, 0}, { -panF / 2, -tiltF}, { -2 * panF, 0}, {0, 0},
  {sRF, -sPF}, { -sRF, -sPF}, { -sRF, sPF}, {sRF, sPF},
  {uRF, uPF}, {uRF, uPF}, {uRF, uPF}, {uRF, uPF},
  {lRF, lPF}, {lRF, lPF}, {lRF, lPF}, {lRF, lPF}
};
#endif

float RollPitchDeviation[2];
int8_t slope = 1;
inline int8_t adaptiveCoefficient(byte idx, byte para) {
  return EEPROM.read(ADAPT_PARAM + idx * NUM_ADAPT_PARAM + para);
}

float adjust(byte i) {
  float rollAdj, pitchAdj;
  if (i == 1 || i > 3)  {//check idx = 1
    bool leftQ = (i - 1 ) % 4 > 1 ? true : false;
    //bool frontQ = i % 4 < 2 ? true : false;
    //bool upperQ = i / 4 < 3 ? true : false;
    float leftRightFactor = 1;
    if ((leftQ && RollPitchDeviation[0]*slope  > 0 )
        || ( !leftQ && RollPitchDeviation[0]*slope  < 0))
      leftRightFactor = LEFT_RIGHT_FACTOR;
    rollAdj = fabs(RollPitchDeviation[0]) * adaptiveCoefficient(i, 0) * leftRightFactor;

  }
  else
    rollAdj = RollPitchDeviation[0] * adaptiveCoefficient(i, 0) ;

  return (
#ifdef POSTURE_WALKING_FACTOR
           (i > 3 ? postureOrWalkingFactor : 1) *
#endif
           // rollAdj + adaptiveCoefficient(i, 1) * ((i % 4 < 2) ? RollPitchDeviation[1] : abs(RollPitchDeviation[1])));
           rollAdj + RollPitchDeviation[1] * adaptiveCoefficient(i, 1) );
}

void saveCalib(int8_t *var) {
  for (byte i = 0; i < DOF; i++) {
    EEPROM.update(CALIB + i, var[i]);
    calibratedDuty0[i] = SERVOMIN + PWM_RANGE / 2 + float(middleShift(i) + var[i]) * pulsePerDegree[i] * rotationDirection(i);
  }
}

void calibratedPWM(byte i, float angle) {
  /*float angle = max(-SERVO_ANG_RANGE/2, min(SERVO_ANG_RANGE/2, angle));
    if (i > 3 && i < 8)
    angle = max(-5, angle);*/
  currentAng[i] = angle;
  int duty = calibratedDuty0[i] + angle * pulsePerDegree[i] * rotationDirection(i);
  duty = max(SERVOMIN , min(SERVOMAX , duty));
  pwm.setPWM(pin(i), 0, duty);
}

void allCalibratedPWM(char * dutyAng) {
  for (int8_t i = DOF - 1; i >= 0; i--) {
    calibratedPWM(i, dutyAng[i]);
  }
}

void shutServos() {
  delay(100);
  for (int8_t i = DOF - 1; i >= 0; i--) {
    pwm.setPWM(i, 0, 4096);
  }
}

void transform( char * target,  float speedRatio = 1, byte offset = 0) {
  char *diff = new char[DOF - offset], maxDiff = 0;
  for (byte i = offset; i < DOF; i++) {
    diff[i - offset] =   currentAng[i] - target[i - offset];
    maxDiff = max(maxDiff, abs( diff[i - offset]));
  }
  byte steps = byte(round(maxDiff / 1.0/*degreeStep*/ / speedRatio));//default speed is 1 degree per step
  for (byte s = 0; s <= steps; s++)
    for (byte i = offset; i < DOF; i++) {
      float dutyAng = (target[i - offset] + (steps == 0 ? 0 : (1 + cos(M_PI * s / steps)) / 2 * diff[i - offset]));
      calibratedPWM(i,  dutyAng);
    }
  delete [] diff;
  //  printList(currentAng);
  //  PTL();
}

void behavior(int n, char** skill, float *speedRatio, int *pause) {
  for (byte i = 0; i < n; i++) {
    motion.loadBySkillName(skill[i]);
    transform( motion.dutyAngles, speedRatio[i]);
    delay(pause[i]);
  }

}

//short tools
template <typename T> int8_t sign(T val) {
  return (T(0) < val) - (val < T(0));
}

template <typename T> void printList(T * arr, byte len = DOF) {
  for (byte i = 0; i < len; i++) {
    PT((T)(arr[i]));
    PT('\t');
  }
  PTL();
}
template <typename T> void printEEPROMList(int EEaddress, byte len = DOF) {
  for (byte i = 0; i < len; i++) {
    PT((T)(EEPROM.read(EEaddress + i)));
    PT('\t');
  }
  PTL();
}
char getUserInput() {//limited to one character
  while (!Serial.available());
  return Serial.read();
}
