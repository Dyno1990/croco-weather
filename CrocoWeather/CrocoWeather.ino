/* CrocoWeather - Arduino Nano ATmega328P, SSD1306 I2C 128x64.
   D2 DHT11, D3 LEFT, D4 OK, D5 RIGHT, D6 passive piezo.
   Buttons connect to GND. Hold LEFT + RIGHT for 1.5 s to exit.
   Libraries: U8g2, DHT sensor library by Adafruit.
*/
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <DHT.h>
#include <math.h>

const uint8_t LEFT_PIN=3, OK_PIN=4, RIGHT_PIN=5, SOUND_PIN=6;
// One-page (128-byte) framebuffer conserves the Nano's 2 KB SRAM.
U8G2_SSD1306_128X64_NONAME_1_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
const uint8_t DHT_PIN=2;
const uint32_t DHT_INTERVAL=2500UL;
DHT dht(DHT_PIN, DHT11);
struct Button {
  uint8_t pin;
  bool raw, down, pressed;
  uint32_t changed;
};
Button keys[3]={{LEFT_PIN,false,false,false,0},
                {OK_PIN,false,false,false,0},
                {RIGHT_PIN,false,false,false,0}};
enum Mode : uint8_t { MENU, WEATHER, PET };
Mode mode=MENU;
uint8_t menuItem=0, action=0, fullness=75, happiness=75;
bool sleeping=false, sensorOK=false, haveExtremes=false, displayOK=false;
bool chord=false, chordLatched=false;
uint32_t chordStart=0, lastFrame=0, lastRead=0, petTick=0;
uint32_t sensorStarted=0, lastGoodRead=0;
uint8_t sensorFailures=0;
bool sensorAttempted=false;
bool soundEnabled=false; // Start silent; enable explicitly from the main menu.
bool soundActive=false;
uint32_t soundStarted=0;
uint16_t soundDuration=0;
uint32_t chewStart=0;
bool chewing=false;
uint8_t eaten=0; // 0 laptop, 1 phone
float temperature=0, humidity=0, minimumT=0, maximumT=0;

void readButtons(uint32_t now) {
  for(uint8_t i=0;i<3;i++) {
    Button &b=keys[i]; b.pressed=false;
    bool v=(digitalRead(b.pin)==LOW);
    if(v!=b.raw) {b.raw=v; b.changed=now;}
    if(v!=b.down && uint32_t(now-b.changed)>=30) {
      b.down=v; b.pressed=v;
    }
  }
}
void stopSound() {
  noTone(SOUND_PIN);
  digitalWrite(SOUND_PIN,LOW);
  pinMode(SOUND_PIN,OUTPUT);
  soundActive=false;
}
void serviceSound(uint32_t now) {
  if(!soundEnabled || mode!=PET || !soundActive || uint32_t(now-soundStarted)>=soundDuration) {
    if(soundActive) stopSound();
    else digitalWrite(SOUND_PIN,LOW);
  }
}
void enterMode(Mode next, uint32_t now) {
  stopSound();
  mode=next; chewing=false; petTick=now;
  // Do not force early reads when switching modes repeatedly.
}
void sound(uint16_t hz, uint16_t duration) {
  if(!soundEnabled || mode!=PET || duration==0) return;
  stopSound();
  soundStarted=millis(); soundDuration=duration; soundActive=true;
  tone(SOUND_PIN,hz,duration);
}
void readWeather(uint32_t now) {
  if(soundActive || uint32_t(now-sensorStarted)<DHT_INTERVAL ||
     uint32_t(now-lastRead)<DHT_INTERVAL) return;
  lastRead=now; sensorAttempted=true;
  const int idle=digitalRead(DHT_PIN);
  // One physical transaction; temperature reuses the same cached sample.
  const float h=dht.readHumidity();
  const float t=dht.readTemperature();
  sensorOK=!isnan(h) && !isnan(t);
  if(!sensorOK) {
    ++sensorFailures;
    Serial.print(F("DHT11 failed; DATA idle=")); Serial.print(idle);
    Serial.print(F("; consecutive failures=")); Serial.println(sensorFailures);
    if(sensorFailures>=3) {
      dht.begin(); sensorStarted=now; sensorFailures=0;
      Serial.println(F("DHT11 reinitialized. Check +5V, GND, S/OUT -> D2 and pull-up."));
    }
    return;
  }
  sensorFailures=0; humidity=h; temperature=t; lastGoodRead=now;
  Serial.print(F("DHT11 T=")); Serial.print(t);
  Serial.print(F(" C RH=")); Serial.println(h);
  if(!haveExtremes) {minimumT=t; maximumT=t; haveExtremes=true;}
  if(t<minimumT) minimumT=t;
  if(t>maximumT) maximumT=t;
}
uint8_t addCapped(uint8_t value, uint8_t amount) {
  return value>100-amount ? 100 : value+amount;
}
void petAction(uint32_t now) {
  if(action<2) {
    sleeping=false; eaten=action; chewing=true; chewStart=now;
    fullness=addCapped(fullness,action==0?25:15);
    happiness=addCapped(happiness,8);
    sound(action==0?660:880,130);
  } else {
    sleeping=!sleeping;
    sound(sleeping?330:990,100);
  }
}
void update(uint32_t now) {
  readButtons(now);
  // Reserve this combination; no actions while held or while releasing it.
  if(keys[0].down && keys[2].down) {
    if(!chord) {chord=true; chordStart=now;}
    if(!chordLatched && uint32_t(now-chordStart)>=1500) {
      enterMode(MENU,now); chordLatched=true;
    }
    return;
  }
  if(chord || chordLatched) {
    if(!keys[0].down && !keys[2].down) {
      chord=false; chordLatched=false;
    }
    return;
  }
  if(mode==MENU) {
    if(keys[0].pressed) menuItem=(menuItem+2)%3;
    if(keys[2].pressed) menuItem=(menuItem+1)%3;
    if(keys[1].pressed) {
      if(menuItem==2) {soundEnabled=!soundEnabled; stopSound();}
      else enterMode(menuItem==0?WEATHER:PET,now);
    }
  } else if(mode==PET) {
    if(keys[0].pressed) action=(action+2)%3;
    if(keys[2].pressed) action=(action+1)%3;
    if(keys[1].pressed) petAction(now);
  }
  if(chewing && uint32_t(now-chewStart)>=2000) chewing=false;
  // Needs pause outside PET. A power cycle starts a fresh pet.
  if(mode==PET && uint32_t(now-petTick)>=15000) {
    petTick=now;
    if(fullness) fullness--;
    if(sleeping) happiness=addCapped(happiness,2);
    else if(happiness) happiness--;
  }
}
void bar(uint8_t x,uint8_t y,uint8_t value) {
  oled.drawFrame(x,y,34,6);
  oled.drawBox(x+1,y+1,(uint16_t(value)*32)/100,4);
}
void laptop(int16_t x,int16_t y) {
  oled.drawFrame(x,y,16,11);
  oled.drawLine(x+2,y+12,x+14,y+12);
  oled.drawLine(x-2,y+13,x+18,y+13);
}
void phone(int16_t x,int16_t y) {
  oled.drawRFrame(x,y,9,16,2);
  oled.drawLine(x+2,y+3,x+6,y+3);
  oled.drawPixel(x+4,y+13);
}
void crocodile(uint32_t now) {
  const uint32_t elapsed=chewing ? uint32_t(now-chewStart) : 0;
  const bool chewingPhase=chewing && elapsed>=600 && elapsed<1600;
  const bool mouthOpen=chewingPhase && ((elapsed/140)%2==0);
  const bool blink=sleeping || (now%4200>3980);
  const int8_t step=(now/180)%4;
  const int16_t x=sleeping?0:(chewing?2:(step<2?0:2));
  const int16_t y=25+(sleeping?((now/900)%2):((now/260)%2));
  // Wagging tail and alternating feet give a little walk cycle.
  oled.drawTriangle(x+13,y+9+(step<2?0:3),x+35,y+6,x+35,y+20);
  oled.drawRBox(x+30,y+3,42,20,6);
  oled.drawBox(x+36+(sleeping?0:step%2*2),y+21,10,4);
  oled.drawBox(x+59+(sleeping?0:(1-step%2)*2),y+21,10,4);
  for(uint8_t i=0;i<4;i++)
    oled.drawTriangle(x+33+i*7,y+4,x+37+i*7,y-1,x+41+i*7,y+4);
  // Two raised eyes and a long crocodile snout.
  oled.drawRBox(x+59,y-3,11,11,3);
  oled.drawRBox(x+69,y-2,10,10,3);
  oled.drawRBox(x+67,y+5,34,12,3);
  oled.setDrawColor(0);
  if(blink) {
    oled.drawHLine(x+62,y+1,5); oled.drawHLine(x+72,y+2,4);
  } else {
    oled.drawBox(x+62,y-1,5,5); oled.drawBox(x+72,y,4,4);
    oled.setDrawColor(1);
    oled.drawBox(x+65,y,2,3); oled.drawBox(x+74,y+1,2,2);
    oled.setDrawColor(0);
  }
  if(mouthOpen) oled.drawTriangle(x+73,y+11,x+102,y+6,x+102,y+18);
  else oled.drawLine(x+74,y+12,x+99,y+(happiness<25?10:13));
  oled.drawPixel(x+95,y+8);
  oled.setDrawColor(1);
  if(mouthOpen) {
    for(uint8_t i=0;i<3;i++) {
      oled.drawTriangle(x+82+i*7,y+9-i,x+85+i*7,y+9-i,x+84+i*7,y+12-i);
    }
  }
  if(sleeping) {
    const uint8_t lift=(now/300)%8;
    oled.drawStr(105,30-lift,"z"); oled.drawStr(116,22-lift,"Z");
  } else if(chewing) {
    if(elapsed<1100) {
      const int16_t foodX=110-int32_t(elapsed)*22/1100;
      if(eaten==0) laptop(foodX,32); else phone(foodX,29);
    }
    if(chewingPhase) {
      for(uint8_t i=0;i<3;i++)
        oled.drawBox(104+i*6,37+((elapsed/100+i*3)%9),2,2);
    }
    if(elapsed>=1600) oled.drawStr(103,27,"YUM!");
  } else if(fullness<25) oled.drawStr(104,27,"FOOD");
  else if(happiness>70 && (now/1500)%2) oled.drawStr(107,25,"<3");
}
void drawWeather(uint32_t now) {
  oled.drawStr(0,8,"WEATHER / DHT11");
  oled.drawHLine(0,11,128);
  if(haveExtremes) {
    oled.setFont(u8g2_font_6x12_tf);
    oled.setCursor(2,26); oled.print(temperature,1); oled.print(F(" C"));
    oled.setCursor(74,26); oled.print(humidity,0); oled.print(F(" %"));
    oled.setFont(u8g2_font_5x7_tf);
    oled.setCursor(2,39); oled.print(F("MIN ")); oled.print(minimumT,1);
    oled.setCursor(67,39); oled.print(F("MAX ")); oled.print(maximumT,1);
    oled.setCursor(2,50);
    if(sensorOK) oled.print(F("LIVE / "));
    else oled.print(F("STALE / "));
    oled.print(uint32_t(now-lastGoodRead)/1000); oled.print(F("s ago"));
  } else if(!sensorAttempted) {
    oled.drawStr(2,28,"DHT11: starting...");
    oled.drawStr(2,42,"Please wait 2.5 sec");
  } else {
    oled.drawStr(2,25,"DHT11: NO RESPONSE");
    oled.drawStr(2,38,"S/OUT -> D2, + -> 5V");
    oled.drawStr(2,50,"Retrying / Serial 9600");
  }
  oled.drawStr(0,63,"Hold L+R 1.5s: menu");
}
void drawScreen(uint32_t now) {
  oled.setDrawColor(1);
  oled.setFont(u8g2_font_5x7_tf);
  if(mode==MENU) {
    oled.drawStr(26,10,"CROCO STATION");
    oled.drawHLine(0,14,128);
    oled.drawStr(15,27,"WEATHER STATION");
    oled.drawStr(15,39,"CROCO TAMAGOTCHI");
    oled.setCursor(15,51); oled.print(F("SOUND: "));
    oled.print(soundEnabled?F("ON"):F("OFF"));
    oled.drawStr(3,27+menuItem*12,">");
    oled.drawStr(3,63,"L/R: choose  OK: select");
  } else if(mode==WEATHER) {
    drawWeather(now);
  } else {
    oled.drawStr(0,7,"FOOD"); bar(23,1,fullness);
    oled.drawStr(65,7,"JOY"); bar(84,1,happiness);
    crocodile(now);
    oled.drawHLine(0,52,128);
    oled.setCursor(0,63); oled.print(action==0?'>':' '); oled.print(F("LAPTOP"));
    oled.setCursor(43,63); oled.print(action==1?'>':' '); oled.print(F("PHONE"));
    oled.setCursor(82,63); oled.print(action==2?'>':' '); oled.print(sleeping?F("WAKE"):F("SLEEP"));
  }
  if(chord && !chordLatched) {
    oled.setDrawColor(0); oled.drawBox(0,53,128,11);
    oled.setDrawColor(1); oled.drawStr(0,62,"Keep holding L+R...");
  }
}
void setup() {
  stopSound();
  for(uint8_t i=0;i<3;i++) pinMode(keys[i].pin,INPUT_PULLUP);
  Serial.begin(9600); dht.begin(); sensorStarted=millis(); Wire.begin();
  // Detect the two common SSD1306 I2C addresses automatically.
  uint8_t address=0;
  const uint8_t addresses[2]={0x3C,0x3D};
  for(uint8_t i=0;i<2;i++) {
    Wire.beginTransmission(addresses[i]);
    if(Wire.endTransmission()==0) {address=addresses[i]; break;}
  }
  if(!address) {
    Serial.println(F("OLED not found at 0x3C/0x3D. Check SDA A4, SCL A5."));
    return;
  }
  oled.setI2CAddress(address<<1); oled.begin(); displayOK=true;
  Serial.println(F("CrocoWeather ready. Hold LEFT+RIGHT 1.5s for menu."));
}
void loop() {
  const uint32_t now=millis();
  serviceSound(now); // Runs even while holding the menu combination.
  if(!displayOK) return;
  readWeather(now); // Refresh real measurements in all modes, even during menu holds.
  update(now);
  if(uint32_t(now-lastFrame)>=80) {
    lastFrame=now;
    oled.firstPage();
    do { drawScreen(now); } while(oled.nextPage());
  }
}
