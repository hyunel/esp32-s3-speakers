/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2021 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "string.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include "es8156.h"
#include "driver/gpio.h"
#include "esp_check.h"

#define ES8156_ADDR             0x08

static i2c_bus_device_handle_t i2c_handle;

static esp_err_t es8156_write_reg(uint8_t reg_addr, uint8_t data)
{
    return i2c_bus_write_byte(i2c_handle, reg_addr, data);
}

static esp_err_t es8156_read_reg(uint8_t reg_addr, uint8_t *data)
{
    return i2c_bus_read_byte(i2c_handle, reg_addr, data);
}

esp_err_t es8156_standby(void)
{
    esp_err_t ret = 0;
    ret = es8156_write_reg(0x14, 0x00);
    ret |= es8156_write_reg(0x19, 0x02);
    ret |= es8156_write_reg(0x21, 0x1F);
    ret |= es8156_write_reg(0x22, 0x02);
    ret |= es8156_write_reg(0x25, 0x21);
    ret |= es8156_write_reg(0x25, 0xA1);
    ret |= es8156_write_reg(0x18, 0x01);
    ret |= es8156_write_reg(0x09, 0x02);
    ret |= es8156_write_reg(0x09, 0x01);
    ret |= es8156_write_reg(0x08, 0x00);
    return ret;
}

esp_err_t es8156_resume(void)
{
    esp_err_t ret = 0;
    ret |= es8156_write_reg(0x08, 0x3F);
    ret |= es8156_write_reg(0x09, 0x00);
    ret |= es8156_write_reg(0x18, 0x00);

    ret |= es8156_write_reg(0x25, 0x20);
    ret |= es8156_write_reg(0x22, 0x00);
    ret |= es8156_write_reg(0x21, 0x3C);
    ret |= es8156_write_reg(0x19, 0x20);
    ret |= es8156_write_reg(0x14, 179);
    return ret;
}

esp_err_t es8156_codec_init(i2c_bus_handle_t bus)
{
    esp_err_t ret = 0;
    i2c_handle = i2c_bus_device_create(bus, ES8156_ADDR, i2c_bus_get_current_clk_speed(bus));

    ret |= es8156_write_reg(ES8156_SCLK_MODE_REG02, 0x04);
    ret |= es8156_write_reg(ES8156_ANALOG_SYS1_REG20, 0x2A);
    ret |= es8156_write_reg(ES8156_ANALOG_SYS2_REG21, 0x3C);
    ret |= es8156_write_reg(ES8156_ANALOG_SYS3_REG22, 0x00);
    ret |= es8156_write_reg(ES8156_ANALOG_SYS4_REG23, 0x00);
    ret |= es8156_write_reg(ES8156_ANALOG_LP_REG24, 0x07);

    ret |= es8156_write_reg(ES8156_TIME_CONTROL1_REG0A, 0x01);
    ret |= es8156_write_reg(ES8156_TIME_CONTROL2_REG0B, 0x01);
    ret |= es8156_write_reg(ES8156_DAC_SDP_REG11, 0x00);
    
    ret |= es8156_write_reg(ES8156_P2S_CONTROL_REG0D, 0x14);
    ret |= es8156_write_reg(ES8156_MISC_CONTROL3_REG18, 0x00);
    ret |= es8156_write_reg(ES8156_CLOCK_ON_OFF_REG08, 0x3F);
    ret |= es8156_write_reg(ES8156_RESET_REG00, 0x02);
    ret |= es8156_write_reg(ES8156_RESET_REG00, 0x03);
    ret |= es8156_write_reg(ES8156_ANALOG_SYS5_REG25, 0x20);

    return ret;
}

esp_err_t es8156_codec_set_voice_mute(int channel, bool enable)
{
    // master channel
    if(channel == 0) return i2c_bus_write_bits(i2c_handle, ES8156_DAC_MUTE_REG13, 1, 2, enable ? 0b11: 0);
    return i2c_bus_write_bit(i2c_handle, ES8156_DAC_MUTE_REG13, channel, enable);
}

esp_err_t es8156_codec_get_voice_mute(int channel, bool *enable)
{
    uint8_t regv;
    esp_err_t err = es8156_read_reg(ES8156_DAC_MUTE_REG13, &regv);
    if(err != ESP_OK) return err;
    
    *enable = regv & (1 << (channel + 1));

    return ESP_OK;
}

/**
 * @brief Set voice volume
 *
 * @note Register values. 0x00: -95.5 dB, 0x5B: -50 dB, 0xBF: 0 dB, 0xFF: 32 dB
 * @note Accuracy of gain is 0.5 dB
 *
 * @param volume: voice volume (0~100)
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t es8156_codec_set_voice_volume(uint8_t volume)
{
    return es8156_write_reg(ES8156_VOLUME_CONTROL_REG14, volume);
}

esp_err_t es8156_codec_get_voice_volume(uint8_t *volume)
{
    return es8156_read_reg(ES8156_VOLUME_CONTROL_REG14, volume);
}