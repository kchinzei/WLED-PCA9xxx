/*
  MIT License

  Copyright (c) 2022 Kiyo Chinzei (kchinzei@gmail.com)

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

#pragma once

#include<algorithm> // for std::max()

#include "wled.h"
#include <PCA9xxxPWMFactory.h>
#include <PCA9xxxPWM.h>
#include <Wire.h>

#define WLED_DEBUG

#define MAXDEVICE 4
#define DEFAULT_PIN_OE 2

class UsermodPCA9xxxControl : public Usermod {
  private:
    PCA9xxxPWMFactory factory;
    PCA9xxxPWM *pwms[MAXDEVICE];
    uint8_t ndevice = 0;
    int8_t pinOE = -1;
    bool enable[MAXDEVICE];
    bool enablePrev[MAXDEVICE];
    bool exponential = false;
    uint8_t briPrev = 0;
    unsigned long lastTime = 0;
    byte prevCol[4] = {0,0,0,0};
    uint8_t briRGBW = 0;

  public:
    /*
     * setup() is called once at boot. WiFi is not yet connected at this point.
     * You can use it to initialize variables, sensors or similar.
     */
    void setup() {
      // Serial.begin(115200);
      ndevice = factory.scanDevice(pwms, MAXDEVICE, true);

      Wire.setClock(400000);
      for (int i = 0; i < MAXDEVICE; i++) {
        enablePrev[i] = false;
      }
    }


    /*
     * connected() is called every time the WiFi is (re)connected
     * Use it to initialize network interfaces
     */
    void connected() {
      DEBUG_PRINTLN(F("Connected to WiFi!"));
    }

    void printglobal() {
      DEBUG_PRINTF(F(" bri: "), bri);
      DEBUG_PRINTF(F(" briOld: "),briOld);
      DEBUG_PRINTF(F(" briS: "), briS);
      DEBUG_PRINTF(F(" col: [%d %d %d]"), col[0], col[1], col[2]);
      DEBUG_PRINTF(F(" briT: "), briT);
      DEBUG_PRINTF(F(" briIT: "), briIT);
      DEBUG_PRINTF(F(" briLast: "), briLast);
    }

    void printreg(PCA9xxxPWM *pwm) {
      uint8_t mode1 = pwm->read(0x00);
      uint8_t mode2 = pwm->read(0x01);
      DEBUG_PRINTF(F(" Addr: 0x%x"), pwm->get_i2cAddr());
      DEBUG_PRINTF(F(" MODE1 = 0x%x"), mode1);
      DEBUG_PRINTF(F(" MODE2 = 0x%x"), mode2);
    }
    /*
     * loop() is called continuously. Here you can check for events, read sensors, etc.
     * 
     * Tips:
     * 1. You can use "if (WLED_CONNECTED)" to check for a successful network connection.
     *    Additionally, "if (WLED_MQTT_CONNECTED)" is available to check for a connection to an MQTT broker.
     * 
     * 2. Try to avoid using the delay() function. NEVER use delays longer than 10 milliseconds.
     *    Instead, use a timer check as shown here.
     * 
     * 3. https://kno.wled.ge/advanced/custom-features/
     */

    #define PCA9XXX_MAXVAL 1.0

    void loop() {
      float val = PCA9XXX_MAXVAL * briT / 255;
      // bool colChanged = (col[0] != prevCol[0]) || (col[1] != prevCol[1]) || (col[2] != prevCol[2]) || (col[3] != prevCol[3]);
      briRGBW = std::max({col[0], col[1], col[2]}) + col[3];
      for (int i = 0; i < ndevice; i++) {
        if (briT != briPrev || enable[i] != enablePrev[i]) {
          float v = val * enable[i];
          if (v > 1.0) v = 1.0;
          if (v < 0.005) v = 0;
          PCA9xxxPWM *pwm = pwms[i];
          int n_of_ports = pwm->number_of_ports();
          for (int j = 0; j < n_of_ports; j++) {
            pwm->pwm(j, v);
          }
        }
        enablePrev[i] = enable[i];
      }
      briPrev = briT;

      unsigned long timer = millis() - lastTime;
      if (timer > 500) {
        lastTime = millis();
        ndevice = factory.scanDevice(pwms, MAXDEVICE);
        for (int i = 0; i < ndevice; i++) {
          PCA9xxxPWM *pwm = pwms[i];
          if (!pwm->hasBegun()) {
            initializePWM(pwm);
            enablePrev[i] = 0; // This ensures turning on again when connected.
          }
        }
        /*
        for (int i = 0; i < ndevice; i++) {
          printreg(pwms[i]);
        }
        Serial.println("");
        */
        printglobal();
      }
    }

#define PCA9xxx_ROOT_KEY "PCA9xxx Status"
#define PCA9xxx_OE_KEY "Output Enable"
#define PCA9xxx_EXPONENTIAL "Exponential brightness"
#define PCA9xxx_DEVICE_KEY "Device %d"
#define PCA9xxx_ENABLE "Enable"
#define PCA9xxx_ADDR "Addr"
#define PCA9xxx_TYPE "Type"

    void addToConfig(JsonObject& root)
    {
      JsonObject top = root.createNestedObject(PCA9xxx_ROOT_KEY);
      JsonObject oe = top.createNestedObject(PCA9xxx_OE_KEY);
      oe["pin"] = pinOE;
      top[PCA9xxx_EXPONENTIAL] = exponential;

      // For display purpose only
      for (int i = 0; i < MAXDEVICE; i++) {
        char buf[32];
        sprintf(buf, PCA9xxx_DEVICE_KEY, i);
        JsonObject device = top.createNestedObject(buf);
        device[PCA9xxx_ENABLE] = enable[i];
        if (i < ndevice) {
          sprintf(buf, "0x%x", pwms[i]->get_i2cAddr());
          device[PCA9xxx_ADDR] = buf;
          device[PCA9xxx_TYPE] = pwms[i]->type_name();
        } else {
          device[PCA9xxx_ADDR] = "(n/c)";
          device[PCA9xxx_TYPE] = "(n/c)";
        }
      }
    }

    bool readFromConfig(JsonObject& root)
    {
      // default settings values could be set here (or below using the 3-argument getJsonValue()) instead of in the class definition or constructor
      // setting them inside readFromConfig() is slightly more robust, handling the rare but plausible use case of single value being missing after boot (e.g. if the cfg.json was manually edited and a value was removed)

      JsonObject top = root[PCA9xxx_ROOT_KEY];

      bool configComplete = !top.isNull();

      JsonObject oe = top[PCA9xxx_OE_KEY];
      configComplete &= !oe.isNull();
      bool newExp = false;
      configComplete &= getJsonValue(top[PCA9xxx_EXPONENTIAL], newExp, false);
      setExponential(newExp);

      int8_t pin = -1;
      configComplete &= getJsonValue(oe["pin"], pin, DEFAULT_PIN_OE);
      setOE(pin);

      for (int i = 0; i < MAXDEVICE; i++) {
        char buf[32];
        sprintf(buf, PCA9xxx_DEVICE_KEY, i);
        JsonObject device = top[buf];
        configComplete &= !device.isNull();
        configComplete &= getJsonValue(device[PCA9xxx_ENABLE], enable[i], 1);
      }
      return configComplete;
    }

  private:
    void initializePWM(PCA9xxxPWM *pwm) {
      if (pwm->isConnected()) {
        if (pwm->begin() == false) {
          // Serial.println("Fail. Attempt to reset");
          pwm->reset();
          pwm->begin();
        }
        pwm->exponential_adjustment(exponential);
      }
    }

    void setExponential(bool newMode) {
      if (newMode != exponential) {
        for (int i = 0; i < ndevice; i++) {
          pwms[i]->exponential_adjustment(newMode);
        }
        exponential = newMode;
      }
    }

    void setOE(int8_t newPinOE) {
      if (pinOE != newPinOE) {
        if (pinOE >= 0) {
          pinMode(pinOE, INPUT_PULLUP);
        }
        pinOE = newPinOE;
        if (newPinOE >= 0) {
          pinMode(pinOE, OUTPUT);
          digitalWrite(pinOE, 0); // Low-active
        }
      }
    }
};
