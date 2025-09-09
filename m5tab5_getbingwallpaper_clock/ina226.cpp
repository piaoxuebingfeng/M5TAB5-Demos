/*

The MIT License

Copyright (c) 2014-2023 Korneliusz Jarzębski

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

#include "ina226.h"
#include <cstdint>
#include <cmath>

bool INA226_Class::begin()
{
    bool result = _i2c->start(_addr, false, _freq) && _i2c->stop();
    return result;
}

bool INA226_Class::configure(ina226_averages_t avg, ina226_busConvTime_t busConvTime,
                             ina226_shuntConvTime_t shuntConvTime, ina226_mode_t mode)
{
    uint16_t config = 0;

    config |= (avg << 9 | busConvTime << 6 | shuntConvTime << 3 | mode);

    vBusMax   = 36;
    vShuntMax = 0.08192f;

    writeRegister16(INA226_REG_CONFIG, config);

    return true;
}

bool INA226_Class::calibrate(float rShuntValue, float iMaxExpected)
{
    uint16_t calibrationValue;
    rShunt = rShuntValue;

    float minimumLSB = iMaxExpected / 32767;

    currentLSB = (uint32_t)(minimumLSB * 100000000);
    currentLSB /= 100000000;
    currentLSB /= 0.0001;
    currentLSB = ceil(currentLSB);
    currentLSB *= 0.0001;

    powerLSB = currentLSB * 25;

    calibrationValue = (uint16_t)((0.00512) / (currentLSB * rShunt));

    writeRegister16(INA226_REG_CALIBRATION, calibrationValue);

    return true;
}

float INA226_Class::getMaxPossibleCurrent(void)
{
    return (vShuntMax / rShunt);
}

float INA226_Class::getMaxCurrent(void)
{
    float maxCurrent  = (currentLSB * 32767);
    float maxPossible = getMaxPossibleCurrent();

    if (maxCurrent > maxPossible) {
        return maxPossible;
    } else {
        return maxCurrent;
    }
}

float INA226_Class::getMaxShuntVoltage(void)
{
    float maxVoltage = getMaxCurrent() * rShunt;

    if (maxVoltage >= vShuntMax) {
        return vShuntMax;
    } else {
        return maxVoltage;
    }
}

float INA226_Class::getMaxPower(void)
{
    return (getMaxCurrent() * vBusMax);
}

float INA226_Class::readBusPower(void)
{
    return (readRegister16(INA226_REG_POWER) * powerLSB);
}

float INA226_Class::readShuntCurrent(void)
{
    return (readRegister16(INA226_REG_CURRENT) * currentLSB);
}

int16_t INA226_Class::readRawShuntCurrent(void)
{
    return readRegister16(INA226_REG_CURRENT);
}

float INA226_Class::readShuntVoltage(void)
{
    float voltage;

    voltage = readRegister16(INA226_REG_SHUNTVOLTAGE);

    return (voltage * 0.0000025);
}

float INA226_Class::readBusVoltage(void)
{
    int16_t voltage;

    voltage = readRegister16(INA226_REG_BUSVOLTAGE);

    return (voltage * 0.00125);
}

ina226_averages_t INA226_Class::getAverages(void)
{
    uint16_t value;

    value = readRegister16(INA226_REG_CONFIG);
    value &= 0b0000111000000000;
    value >>= 9;

    return (ina226_averages_t)value;
}

ina226_busConvTime_t INA226_Class::getBusConversionTime(void)
{
    uint16_t value;

    value = readRegister16(INA226_REG_CONFIG);
    value &= 0b0000000111000000;
    value >>= 6;

    return (ina226_busConvTime_t)value;
}

ina226_shuntConvTime_t INA226_Class::getShuntConversionTime(void)
{
    uint16_t value;

    value = readRegister16(INA226_REG_CONFIG);
    value &= 0b0000000000111000;
    value >>= 3;

    return (ina226_shuntConvTime_t)value;
}

ina226_mode_t INA226_Class::getMode(void)
{
    uint16_t value;

    value = readRegister16(INA226_REG_CONFIG);
    value &= 0b0000000000000111;

    return (ina226_mode_t)value;
}

void INA226_Class::setMaskEnable(uint16_t mask)
{
    writeRegister16(INA226_REG_MASKENABLE, mask);
}

uint16_t INA226_Class::getMaskEnable(void)
{
    return readRegister16(INA226_REG_MASKENABLE);
}

void INA226_Class::enableShuntOverLimitAlert(void)
{
    writeRegister16(INA226_REG_MASKENABLE, INA226_BIT_SOL);
}

void INA226_Class::enableShuntUnderLimitAlert(void)
{
    writeRegister16(INA226_REG_MASKENABLE, INA226_BIT_SUL);
}

void INA226_Class::enableBusOvertLimitAlert(void)
{
    writeRegister16(INA226_REG_MASKENABLE, INA226_BIT_BOL);
}

void INA226_Class::enableBusUnderLimitAlert(void)
{
    writeRegister16(INA226_REG_MASKENABLE, INA226_BIT_BUL);
}

void INA226_Class::enableOverPowerLimitAlert(void)
{
    writeRegister16(INA226_REG_MASKENABLE, INA226_BIT_POL);
}

void INA226_Class::enableConversionReadyAlert(void)
{
    writeRegister16(INA226_REG_MASKENABLE, INA226_BIT_CNVR);
}

void INA226_Class::disableAlerts(void)
{
    writeRegister16(INA226_REG_MASKENABLE, 0);
}

void INA226_Class::setBusVoltageLimit(float voltage)
{
    uint16_t value = voltage / 0.00125;
    writeRegister16(INA226_REG_ALERTLIMIT, value);
}

void INA226_Class::setShuntVoltageLimit(float voltage)
{
    uint16_t value = voltage / 0.0000025;
    writeRegister16(INA226_REG_ALERTLIMIT, value);
}

void INA226_Class::setPowerLimit(float watts)
{
    uint16_t value = watts / powerLSB;
    writeRegister16(INA226_REG_ALERTLIMIT, value);
}

void INA226_Class::setAlertInvertedPolarity(bool inverted)
{
    uint16_t temp = getMaskEnable();

    if (inverted) {
        temp |= INA226_BIT_APOL;
    } else {
        temp &= ~INA226_BIT_APOL;
    }

    setMaskEnable(temp);
}

void INA226_Class::setAlertLatch(bool latch)
{
    uint16_t temp = getMaskEnable();

    if (latch) {
        temp |= INA226_BIT_LEN;
    } else {
        temp &= ~INA226_BIT_LEN;
    }

    setMaskEnable(temp);
}

bool INA226_Class::isMathOverflow(void)
{
    return ((getMaskEnable() & INA226_BIT_OVF) == INA226_BIT_OVF);
}

bool INA226_Class::isAlert(void)
{
    return ((getMaskEnable() & INA226_BIT_AFF) == INA226_BIT_AFF);
}

int16_t INA226_Class::readRegister16(uint8_t reg)
{
    // Create a temporary buffer
    uint8_t buff[2];

    // Read the 16-bit register
    readRegister(reg, buff, 2);

    // Return the combined 16-bit value
    return (buff[0] << 8) | buff[1];
}

void INA226_Class::writeRegister16(uint8_t reg, uint16_t val)
{
    // Create a temporary buffer
    uint8_t buff[2];

    // Load the register address and 16-bit data
    buff[0] = val >> 8;
    buff[1] = val;

    // Write the data
    writeRegister(reg, buff, 2);
}
