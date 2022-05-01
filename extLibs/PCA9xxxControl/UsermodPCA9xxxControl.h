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
#include "PCA9955APWM.h"
#include <Wire.h>

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
    byte prevCol[4] = {0,0,0,0};
    uint8_t briRGBW = 0;
    const String pca9955_string = "PCA9955";

  public:
    /*
     * setup() is called once at boot. WiFi is not yet connected at this point.
     * You can use it to initialize variables, sensors or similar.
     */
    void setup() {
      ndevice = factory.scanDevice(pwms, MAXDEVICE, true);

      Wire.setClock(400000);
      for (int i = 0; i < MAXDEVICE; i++) {
        enablePrev[i] = false;
      }
      for (int i = 0; i < ndevice; i++) {
        initialize_PWM(pwms[i]);
      }
    }

    /*
     * connected() is called every time the WiFi is (re)connected
     * Use it to initialize network interfaces
     */
    void connected() {
      DEBUG_PRINTLN(F("Connected to WiFi!"));
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

    void loop() {
      float val = brightness2float();
      // bool colChanged = (col[0] != prevCol[0]) || (col[1] != prevCol[1]) || (col[2] != prevCol[2]) || (col[3] != prevCol[3]);
      // briRGBW = std::max({col[0], col[1], col[2]}) + col[3];
      for (int i = 0; i < ndevice; i++) {
        if (briT != briPrev || enable[i] != enablePrev[i]) {
          set_brightness(i, val);
        }
        enablePrev[i] = enable[i];
      }
      briPrev = briT;

      loop_periodical_init();
      //loop_print_9955A_registers();
    }

    void loop_periodical_init() {
      static unsigned long lastTime = 0;
      unsigned long timer = millis() - lastTime;
      if (timer > 500) {
        lastTime = millis();
        DEBUG_PRINTF(" bri: %d", bri);
        ndevice = factory.scanDevice(pwms, MAXDEVICE);
        for (int i = 0; i < ndevice; i++) {
          PCA9xxxPWM *pwm = pwms[i];
          if (!pwm->hasBegun()) {
            initialize_PWM(pwm);
            enablePrev[i] = false; // This ensures turning on again when connected.
          } else {
            #ifdef WLED_DEBUG
            if (enable[i]) {
              print_mode_register(pwms[i]);
              print_pwm_register(pwms[i], 0);
            } else {
              DEBUG_PRINTF("| Adr: x%x (disabled) e:%d ", pwm->get_i2cAddr(), pwm->get_use_exponential());
            }
            #endif
            enable[i] &= check_errorflag(pwm);
            // check_errorflag(pwm);
          }
        }
        // print_global();
        DEBUG_PRINTLN("");
      }
    }

    bool PCA9xxx_typecheck(PCA9xxxPWM *pwm, const String matchstr) {
      // This dirty typecheck is to avoid using RTTI. RTTI causes strange errors in ESP8266.
      // Without RTTI, you cannot use dynamic_cast<T>().
      const String name = pwm->type_name();
      const char* mstr = matchstr.c_str();
      return !strncmp(name.c_str(), mstr, strlen(mstr));
    }

#define BRI_ERR_TH (32.0/255)
#define BRI_ERR_TH_EXP (144.0/255)

    bool check_errorflag(PCA9xxxPWM *pwm) {
      // PCA995xA has overtemp and open/short error detection. Sometimes error detection itself fails.
      // When IREG is small, this condition can raise output voltage level (VO). When VO > Vth = 2.85V,
      // Short-circuit detection is activated.
      if (PCA9xxx_typecheck(pwm, pca9955_string)) {
        PCA9955APWM *p55 = (PCA9955APWM *)pwm;
        uint8_t mode2 = p55->read(PCA9955APWM::MODE2);
        if (mode2 & PCA9955APWM::MODE2_OVERTEMP) {
          DEBUG_PRINTLN("ERROR DETECTED; OVERTEMP");
          return false;
        }
        if (mode2 & PCA9955APWM::MODE2_ERROR) {
          bool open_circuit = false;
          bool short_circuit = false;
          for (int i = 0; i < 4; i++) {
            uint8_t errflg = p55->read(PCA9955APWM::EFLAG0 + i);
            if (errflg & 0x55) short_circuit = true;
            if (errflg & 0xAA) open_circuit = true;
          }
          // Attempt to reset it.
          mode2 |= PCA9955APWM::MODE2_CLRERR;
          p55->write(PCA9955APWM::MODE2, mode2);
          delay(1);
          mode2 |= PCA9955APWM::MODE2_CLRERR;
          p55->write(PCA9955APWM::MODE2, mode2);
          delay(1);
          mode2 = p55->read(PCA9955APWM::MODE2);
          if (mode2 & PCA9955APWM::MODE2_ERROR) {
            // Error persists. return something
            if (open_circuit) {
              DEBUG_PRINTLN("ERROR DETECTED; OPEN CIRCUIT");
              return false;
            }
            // We ignore short_circuit flag when blightness is low.
            if (short_circuit) {
              if (brightness2float() > (exponential? BRI_ERR_TH_EXP : BRI_ERR_TH)) {
                DEBUG_PRINTLN("ERROR DETECTED; SHORT CIRCUIT");
                return false;
              }
            }
          }
        }
      }
      return true;
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
          sprintf(buf, "0x%02x", pwms[i]->get_i2cAddr());
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
      set_exponential(newExp);

      int8_t pin = -1;
      configComplete &= getJsonValue(oe["pin"], pin, DEFAULT_PIN_OE);
      set_OE_pin(pin);

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
    void initialize_PWM(PCA9xxxPWM *pwm) {
      if (pwm->isConnected()) {
        if (pwm->begin() == false) {
          pwm->reset();
          pwm->begin();
        }
        pwm->exponential_adjustment(exponential);
      }
    }

    #define PCA9XXX_MAXVAL 1.0

    float brightness2float() {
      return PCA9XXX_MAXVAL * briT / 255;
    }

    void set_brightness(int i, float v) {
      v = v * enable[i];
      if (v > 1.0) v = 1.0;
      if (v < 0.005) v = 0;
      PCA9xxxPWM *pwm = pwms[i];
      pwm->pwm(ALLPORTS, v);
      digitalWrite(pinOE, (v == 0)); // 
      /*
      int n_of_ports = pwm->number_of_ports();
      for (int j = 0; j < n_of_ports; j++) {
        pwm->pwm(j, v);
      }
      */
    }

    void set_exponential(bool newMode) {
      if (newMode != exponential) {
        float val = brightness2float();
        for (int i = 0; i < ndevice; i++) {
          pwms[i]->exponential_adjustment(newMode);
          set_brightness(i, val);
        }
        exponential = newMode;
      }
    }

    void set_OE_pin(int8_t newPinOE) {
      if (pinOE != newPinOE) {
        if (pinOE >= 0) {
          pinMode(pinOE, INPUT_PULLUP);
        }
        pinOE = newPinOE;
        if (newPinOE >= 0) {
          pinMode(pinOE, OUTPUT);
        }
      }
    }

    /*
     * debug purpose functions.
     */
#ifdef WLED_DEBUG
    void print_global() {
      DEBUG_PRINTF(" bri: %d", bri);
      DEBUG_PRINTF(" briOld: %d",briOld);
      DEBUG_PRINTF(" briS: %d", briS);
      DEBUG_PRINTF(" col: [%d %d %d]", col[0], col[1], col[2]);
      DEBUG_PRINTF(" briT: %d", briT);
      DEBUG_PRINTF(" briIT: %d", briIT);
      DEBUG_PRINTF(" briLast: %d", briLast);
    }

    void print_mode_register(PCA9xxxPWM *pwm) {
      uint8_t mode1 = pwm->read(0x00);
      uint8_t mode2 = pwm->read(0x01);
      DEBUG_PRINTF("| Adr: x%x", pwm->get_i2cAddr());
      DEBUG_PRINTF(" M1/M2 x%x/%x", mode1, mode2);
    }

    void print_pwm_register(PCA9xxxPWM *pwm, uint8_t ch = 0) {
      // PCA995xAPWM *p5x = dynamic_cast<PCA995xAPWM*>(pwm);
      // if (p5x != nullptr) {
      if (PCA9xxx_typecheck(pwm, pca9955_string)) {
        PCA9955APWM *p55 = (PCA9955APWM *)pwm;
        int p = p55->get_pwm(ch) * 255;
        int c = p55->get_current(ch) * 255;
        DEBUG_PRINTF(" p/c %d/%d e:%d ", p, c, p55->get_use_exponential());
        print_9955A_errorflags(p55);
      } else {
        int p = pwm->get_pwm(ch) * 255;
        DEBUG_PRINTF(" p %d e:%d", p,  pwm->get_use_exponential());
      }
    }
    
    void print_9955A_errorflags(PCA9955APWM *p55) {
      uint8_t val[4];
      for (int i = 0; i < 4; i++) {
        val[i] =  p55->read(PCA9955APWM::EFLAG0 + i);
      }
      DEBUG_PRINTF(" E0-3 x%x%x%x%x", val[0], val[1], val[2], val[3]);
    }

    void loop_print_9955A_registers() {
      static unsigned long lastTime = 0;
      unsigned long timer = millis() - lastTime;
      if (timer > 30000) {
        lastTime = millis();
        for (int i = 0; i < ndevice; i++) {
          PCA9xxxPWM *pwm = pwms[i];
          if (PCA9xxx_typecheck(pwm, pca9955_string)) {
            print9955A_registers((PCA9955APWM *)pwm);
          }
        }
      }
    }

    void print9955A_registers(PCA9955APWM *p55) {
      DEBUG_PRINTF("I2C Adr: 0x%x\n", p55->get_i2cAddr());
      DEBUG_PRINTF("     0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");
      for (int n = 0; n <= PCA9955APWM::EFLAG3; n++) {
        if ((n & 0x0F) == 0) {
          DEBUG_PRINTF("%02x:", n);
        }
        int val = p55->read(n);
        DEBUG_PRINTF(" %02x", val);
        if ((n & 0x0F) == 0x0F) {
          DEBUG_PRINTF("\n");
        }
      }
      DEBUG_PRINTF("\n\n");
    }
#endif // #define WLED_DEBUG
};
