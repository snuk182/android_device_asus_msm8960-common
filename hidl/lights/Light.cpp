/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2016 The CyanogenMod Project
 * Copyright (C) 2017-2020 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "light"

#include "Light.h"

#include <log/log.h>

namespace {
using android::hardware::light::V2_0::LightState;

static uint32_t rgbToBrightness(const LightState& state) {
    uint32_t color = state.color & 0x00ffffff;
    return ((77 * ((color >> 16) & 0xff)) + (150 * ((color >> 8) & 0xff)) +
            (29 * (color & 0xff))) >> 8;
}

static bool isLit(const LightState& state) {
    return (state.color & 0x00ffffff);
}
} // anonymous namespace

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

// === AS3676 LEDs ===
const static std::string LEDS_COLORS_BRIGHTNESS_BLUE_FILE = "/sys/devices/i2c-10/10-0040/leds/pwr-blue/brightness";
const static std::string LEDS_COLORS_BRIGHTNESS_GREEN_FILE = "/sys/devices/i2c-10/10-0040/leds/pwr-green/brightness";
const static std::string LEDS_COLORS_BRIGHTNESS_RED_FILE = "/sys/devices/i2c-10/10-0040/leds/pwr-red/brightness";
const static std::string LEDS_COLORS_PATTERN_BLUE_FILE = "/sys/devices/i2c-10/10-0040/leds/pwr-blue/use_pattern";
const static std::string LEDS_COLORS_PATTERN_GREEN_FILE = "/sys/devices/i2c-10/10-0040/leds/pwr-green/use_pattern";
const static std::string LEDS_COLORS_PATTERN_RED_FILE = "/sys/devices/i2c-10/10-0040/leds/pwr-red/use_pattern";
const static std::string LEDS_DIM_TIME_FILE = "/sys/bus/i2c/devices/10-0040/dim_time";
const static std::string LEDS_PATTERN_DATA_FILE = "/sys/bus/i2c/devices/10-0040/pattern_data";
const static std::string LEDS_PATTERN_DELAY_FILE = "/sys/bus/i2c/devices/10-0040/pattern_delay";
const static std::string LEDS_PATTERN_DURATION_SECS_FILE = "/sys/bus/i2c/devices/10-0040/pattern_duration_secs";
const static std::string LEDS_PATTERN_USE_SOFTDIM_FILE = "/sys/bus/i2c/devices/10-0040/pattern_use_softdim";

// === AS3676 LCD ===
const static uint32_t LCD_BRIGHTNESS_MAX = 255;
const static uint32_t LCD_BRIGHTNESS_MIN = 1;
const static uint32_t LCD_BRIGHTNESS_OFF = 0;

Light::Light(std::ofstream&& backlight1, std::ofstream&& backlight2)
    : mBacklight1(std::move(backlight1))
    , mBacklight2(std::move(backlight2))
    , mNotificationBrightnessBlue(LEDS_COLORS_BRIGHTNESS_BLUE_FILE)
    , mNotificationBrightnessGreen(LEDS_COLORS_BRIGHTNESS_GREEN_FILE)
    , mNotificationBrightnessRed(LEDS_COLORS_BRIGHTNESS_RED_FILE)
    , mNotificationPatternBlue(LEDS_COLORS_PATTERN_BLUE_FILE)
    , mNotificationPatternGreen(LEDS_COLORS_PATTERN_GREEN_FILE)
    , mNotificationPatternRed(LEDS_COLORS_PATTERN_RED_FILE)
    , mNotificationDimTime(LEDS_DIM_TIME_FILE)
    , mNotificationPatternData(LEDS_PATTERN_DATA_FILE)
    , mNotificationPatternDelay(LEDS_PATTERN_DELAY_FILE)
    , mNotificationPatternDuration(LEDS_PATTERN_DURATION_SECS_FILE)
    , mNotificationPatternSoftdim(LEDS_PATTERN_USE_SOFTDIM_FILE)
{
    auto attnFn(std::bind(&Light::setAttentionLight, this, std::placeholders::_1));
    auto backlightFn(std::bind(&Light::setBacklight, this, std::placeholders::_1));
    auto batteryFn(std::bind(&Light::setBatteryLight, this, std::placeholders::_1));
    auto notifFn(std::bind(&Light::setNotificationLight, this, std::placeholders::_1));
    mLights.emplace(std::make_pair(Type::ATTENTION, attnFn));
    mLights.emplace(std::make_pair(Type::BACKLIGHT, backlightFn));
    mLights.emplace(std::make_pair(Type::BATTERY, batteryFn));
    mLights.emplace(std::make_pair(Type::NOTIFICATIONS, notifFn));
}

// Methods from ::android::hardware::light::V2_0::ILight follow.
Return<Status> Light::setLight(Type type, const LightState& state) {
    if (mLights.find(type) != mLights.end()) {
        mLights.at(type)(state);
        return Status::SUCCESS;
    }
    return Status::LIGHT_NOT_SUPPORTED;
}

Return<void> Light::getSupportedTypes(getSupportedTypes_cb _hidl_cb) {
    Type *types = new Type[mLights.size()];
    int idx = 0;

    for (auto const &kv : mLights) {
        Type t = kv.first;
        types[idx++] = t;
    }

    {
        hidl_vec<Type> hidl_types{};
        hidl_types.setToExternal(types, mLights.size());

        _hidl_cb(hidl_types);
    }

    delete[] types;

    return Void();
}

void Light::setAttentionLight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);
    mAttentionState = state;
    setSpeakerBatteryLightLocked();
}

// /* === Module get_light_lcd_max_backlight === */
// static int
// get_light_lcd_max_backlight() {

//     char value[6];
//     int fd, len, max_brightness;

//     /* Open the max brightness file */
//     if ((fd = open(MAX_BRIGHTNESS_FILE, O_RDONLY)) < 0) {
//         ALOGE("[%s]: Could not open max brightness file %s: %s", __FUNCTION__,
//                 MAX_BRIGHTNESS_FILE, strerror(errno));
//         ALOGE("[%s]: Assume max brightness 255", __FUNCTION__);
//         return 255;
//     }

//     /* Read the max brightness file */
//     if ((len = read(fd, value, sizeof(value))) <= 1) {
//         ALOGE("[%s]: Could not read max brightness file %s: %s", __FUNCTION__,
//                 MAX_BRIGHTNESS_FILE, strerror(errno));
//         ALOGE("[%s]: Assume max brightness 255", __FUNCTION__);
//         close(fd);
//         return 255;
//     }

//     /* Extract the max brightness value */
//     max_brightness = strtol(value, NULL, 10);
//     close(fd);

//     return (unsigned int) max_brightness;
// }

// /* === Module brightness_apply_gamma === */
// #ifdef ENABLE_GAMMA_CORRECTION
// static int
// brightness_apply_gamma(int brightness) {

//     double floatbrt = (double) brightness;

//     /* Regular gamma correction curve */
// #ifndef ENABLE_GAMMA_CORRECTION_SINE
//     floatbrt /= 255.0;
// #ifdef LOG_BRIGHTNESS
//     ALOGV("%s: brightness = %d, floatbrt = %f", __FUNCTION__, brightness,
//             floatbrt);
// #endif
//     floatbrt = pow(floatbrt, 2.2);
// #ifdef LOG_BRIGHTNESS
//     ALOGV("%s: gamma corrected floatbrt = %f", __FUNCTION__, floatbrt);
// #endif
//     floatbrt *= 255.0;

//     /* Sine gamma correction curve */
// #else
//     floatbrt = 128 + 127 * sin(M_PI * (-0.5 + floatbrt / 255.0));
// #ifdef LOG_BRIGHTNESS
//     ALOGV("%s: brightness = %d, floatbrt = %f", __FUNCTION__, brightness,
//             floatbrt);
// #endif
// #endif

//     brightness = (int) floatbrt;
// #ifdef LOG_BRIGHTNESS
//     ALOGV("%s: gamma corrected brightness = %d", __FUNCTION__, brightness);
// #endif
//     return brightness;
// }
// #endif

void Light::setBacklight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);

    uint32_t brightness = rgbToBrightness(state);

    // LCD brightness limitations
    if (brightness <= LCD_BRIGHTNESS_OFF) {
        brightness = LCD_BRIGHTNESS_OFF;
    }
    else if (brightness < LCD_BRIGHTNESS_MIN) {
        brightness = LCD_BRIGHTNESS_MIN;
    }
    else if (brightness > LCD_BRIGHTNESS_MAX) {
        brightness = LCD_BRIGHTNESS_MAX;
    }

//     int err = 0;
//     int brightness = rgb_to_brightness(state);
//     int max_brightness = get_light_lcd_max_backlight();

//     // Process the brightness value
//     if (brightness > 0) {
// #ifdef ENABLE_GAMMA_CORRECTION
//         brightness = brightness_apply_gamma(brightness);
// #endif
//         brightness = (max_brightness * brightness) / 255;
//         if (brightness < LCD_BRIGHTNESS_MIN) {
//             brightness = LCD_BRIGHTNESS_MIN;
//         }
//     }

    // LCD brightness update
    mBacklight1 << brightness << std::endl;
    mBacklight2 << brightness << std::endl;

// #ifdef DEVICE_HAYABUSA
//     if (brightness == 0 && is_lit(&g_notification)) {
//         // Apply logo brightness on display off
//         write_int(LOGO_BACKLIGHT_FILE, max_brightness);
//         write_int(LOGO_BACKLIGHT2_FILE, max_brightness);
//     } else {
//         write_int(LOGO_BACKLIGHT_FILE, brightness);
//         write_int(LOGO_BACKLIGHT2_FILE, brightness);
//     }

//     last_screen_brightness = brightness;
// #endif
}

void Light::setBatteryLight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);
    mBatteryState = state;
    setSpeakerBatteryLightLocked();
}

void Light::setNotificationLight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);
    mNotificationState = state;
    setSpeakerBatteryLightLocked();
}

void Light::setSpeakerBatteryLightLocked() {
    if (isLit(mNotificationState)) {
        setSpeakerLightLocked(mNotificationState);
    } else if (isLit(mAttentionState)) {
        setSpeakerLightLocked(mAttentionState);
    } else {
        setSpeakerLightLocked(mBatteryState);
    }
}

// /* === Module clear_lights_locked === */
// static void
// clear_lights_locked() {

//     /* Clear all LEDs */
//     write_string(PATTERN_DATA_FILE, "0");
//     write_int(PATTERN_USE_SOFTDIM_FILE, 0);
//     write_int(PATTERN_DURATION_SECS_FILE, 1);
//     write_int(PATTERN_DELAY_FILE, 1);
//     write_int(DIM_TIME_FILE, 500);
//     write_int(PWR_RED_BRIGHTNESS_FILE, 0);
//     write_int(PWR_RED_USE_PATTERN_FILE, 0);
//     write_int(PWR_GREEN_BRIGHTNESS_FILE, 0);
//     write_int(PWR_GREEN_USE_PATTERN_FILE, 0);
//     write_int(PWR_BLUE_BRIGHTNESS_FILE, 0);
//     write_int(PWR_BLUE_USE_PATTERN_FILE, 0);
// #ifdef DEVICE_HAYABUSA
//     write_int(LOGO_BACKLIGHT_FILE, last_screen_brightness);
//     write_int(LOGO_BACKLIGHT2_FILE, last_screen_brightness);
//     write_int(LOGO_BACKLIGHT_PATTERN_FILE, 0);
//     write_int(LOGO_BACKLIGHT2_PATTERN_FILE, 0);
// #endif
// }

/* === Module get_light_leds_dim_time === */
static int
get_light_leds_dim_time(int offMS) {

    /* Get diming time from offMS */
    if (offMS > 500) return 500;
    else if (offMS > 190) return 190;
    else if (offMS > 95) return 95;
    else if (offMS > 50) return 50;
    else return 50;
}

/* === Module pattern_data_on_bit === */
static void
pattern_data_on_bit(double duration, int onMS, int offMS, int *data) {

    int bit, cycle, now_cycle, s;

    bit = 0;
    cycle = (int)(duration / (onMS + offMS) + 0.5);
    if (cycle == 0) cycle = 1;
    now_cycle = 0;
    s = 0;

    /* Transform settings to pattern data */
    for (bit = 0; bit < 32; ++bit) {
        for (now_cycle = 0; now_cycle < cycle; ++now_cycle) {
            s = duration / 32 * bit - now_cycle * (onMS + offMS);
            if (s >= 0 && s < onMS) {
                *data |= 1 << bit;
            }
        }
    }
}

void Light::setSpeakerLightLocked(const LightState& state) {
    // Variables
    char pattern_data[11];
    Flash flashMode;
    int32_t delayOn, delayOff, delayTotal;
    int32_t pattern_duration = 1;
    int32_t pattern_dim_time = 50;
    int32_t pattern_data_dec = 0;
    int32_t pattern_delay = 0;
    uint32_t colorARGB;
    uint32_t colorBlue;
    uint32_t colorGreen;
    uint32_t colorRed;

    // LEDs variables processing
    colorARGB = state.color;
    delayOn = state.flashOnMs;
    delayOff = state.flashOffMs;
    flashMode = state.flashMode;
    colorBlue = colorARGB & 0xFF;
    colorGreen = (colorARGB >> 8) & 0xFF;
    colorRed = (colorARGB >> 16) & 0xFF;

    // Avoid flashing programs with an empty delay
    if (delayOff == 0 || delayOn == 0 || flashMode == Flash::NONE) {
        delayOff = 0;
        delayOn = 0;
        flashMode = Flash::NONE;
    }

    // Using LED soft dimming
    if (flashMode == Flash::TIMED) {

        /* Render with timings */
        if (delayOn > 0 && delayOff > 0) {
            if (delayOn + delayOff > 15000) {
                if (delayOn > 8000) {
                    delayOn = 8000;
                }
                if (delayOff > 15000 - delayOn) {
                    delayOff = 15000 - delayOn;
                }
            }

            delayTotal = delayOn + delayOff;

            if (delayTotal < 8000 && delayOn < 1000) {
                pattern_duration = 1;
                pattern_dim_time = get_light_leds_dim_time(
                        delayOff > delayOn ? delayOn : delayOff);
                pattern_data_on_bit(1000.0, delayOn, delayOff, &pattern_data_dec);
                pattern_delay = delayTotal - 1000 < 1000 ?
                    (delayOff > 3 * delayOn ? 1 : 0) :
                    (delayTotal - 1000) / 1000;
                /* When delayOff is 3 times longer than delayOn (eg on 300ms, off 1000ms)
                   if we only use pattern_data to describe off time (700ms)
                   it will appear too short.
                   (although 700ms is more close to desired 1000ms than +1s delay)
                   So we force 1s delay in such cases. */
            } else {
                pattern_duration = 8;
                pattern_dim_time = get_light_leds_dim_time(
                        delayOff > delayOn ? delayOn : delayOff);
                pattern_data_on_bit(8000.0, delayOn, delayOff, &pattern_data_dec);
                pattern_delay = (delayTotal - 8000 < 8000 ?
                        0 : (delayTotal - 8000) / 1000);
                /* The above trick is not needed here
                   since it won't make much visible difference
                   when delayOff > 3 * delayOn here. */
            }
        }
        /* Render without timings */
        else {
            pattern_data_dec = 0;
        }

        /* Write the pattern data and clear lights */
        snprintf(pattern_data, 11, "0x%X", pattern_data_dec);
        // clear_lights_locked();

/*        write_string(PATTERN_DATA_FILE, pattern_data);
        write_int(PATTERN_USE_SOFTDIM_FILE, pattern_use_softdim);
        write_int(PATTERN_DURATION_SECS_FILE, pattern_duration);
        write_int(PATTERN_DELAY_FILE, pattern_delay);
        write_int(DIM_TIME_FILE, pattern_dim_time);
        write_int(PWR_RED_BRIGHTNESS_FILE, red);
        write_int(PWR_RED_USE_PATTERN_FILE, 1);
        write_int(PWR_GREEN_BRIGHTNESS_FILE, green);
        write_int(PWR_GREEN_USE_PATTERN_FILE, 1);
        write_int(PWR_BLUE_BRIGHTNESS_FILE, blue);
        write_int(PWR_BLUE_USE_PATTERN_FILE, 1);
        */

        mNotificationPatternData << pattern_data << std::endl;
        mNotificationPatternSoftdim << "1" << std::endl;
        mNotificationPatternDuration << pattern_duration << std::endl;
        mNotificationPatternDelay << pattern_delay << std::endl;
        mNotificationDimTime << pattern_dim_time << std::endl;
        mNotificationBrightnessBlue << colorBlue << std::endl;
        mNotificationBrightnessGreen << colorGreen << std::endl;
        mNotificationBrightnessRed << colorRed << std::endl;
        mNotificationPatternBlue << "1" << std::endl;
        mNotificationPatternGreen << "1" << std::endl;
        mNotificationPatternRed << "1" << std::endl;
// #ifdef DEVICE_HAYABUSA
//         /* This 255 may be something like last_screen_brightness==0?255:l_s_b
//            But since everything is working as expected
//            (LED won't be triggered when screen is on,
//            including new notif arriving in lockscreen)
//            It's likely that I'll never visit this again... */
//         write_int(LOGO_BACKLIGHT_FILE, 255);
//         write_int(LOGO_BACKLIGHT2_FILE, 255);
//         write_int(LOGO_BACKLIGHT_PATTERN_FILE, 1);
//         write_int(LOGO_BACKLIGHT2_PATTERN_FILE, 1);
// #endif
    }
    // Using LED direct control
    else {
        mNotificationBrightnessBlue << colorBlue << std::endl;
        mNotificationBrightnessGreen << colorGreen << std::endl;
        mNotificationBrightnessRed << colorRed << std::endl;
    }

    // LEDs debug text
    ALOGE("LEDs : %d %d %d - delayOn : %d, delayOff : %d - Flash : %d",
            colorRed, colorGreen, colorBlue, delayOn, delayOff, flashMode);
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android
