//-----------------------------------------------------------------------------
/*

Cirrus Logic CS43L22 Stereo DAC

*/
//-----------------------------------------------------------------------------

#define DEBUG

#include "cs43l22.h"
#include "stm32f4_soc.h"
//#include "delay.h"
#include "logging.h"
#include "utils.h"

//-----------------------------------------------------------------------------

#define CS43L22_REG_ID                              0x01
#define CS43L22_REG_Power_Ctl_1                     0x02
#define CS43L22_REG_Power_Ctl_2                     0x04
#define CS43L22_REG_Clocking_Ctl                    0x05
#define CS43L22_REG_Interface_Ctl_1                 0x06
#define CS43L22_REG_Interface_Ctl_2                 0x07
#define CS43L22_REG_Passthrough_A_Select            0x08
#define CS43L22_REG_Passthrough_B_Select            0x09
#define CS43L22_REG_Analog_ZC_and_SR_Settings       0x0A
#define CS43L22_REG_Passthrough_Gang_Control        0x0C
#define CS43L22_REG_Playback_Ctl_1                  0x0D
#define CS43L22_REG_Misc_Ctl                        0x0E
#define CS43L22_REG_Playback_Ctl_2                  0x0F
#define CS43L22_REG_Passthrough_A_Vol               0x14
#define CS43L22_REG_Passthrough_B_Vol               0x15
#define CS43L22_REG_PCMA_Vol                        0x1A
#define CS43L22_REG_PCMB_Vol                        0x1B
#define CS43L22_REG_BEEP_Freq_On_Time               0x1C
#define CS43L22_REG_BEEP_Vol_Off_Time               0x1D
#define CS43L22_REG_BEEP_Tone_Cfg                   0x1E
#define CS43L22_REG_Tone_Ctl                        0x1F
#define CS43L22_REG_Master_A_Vol                    0x20
#define CS43L22_REG_Master_B_Vol                    0x21
#define CS43L22_REG_Headphone_A_Volume              0x22
#define CS43L22_REG_Headphone_B_Volume              0x23
#define CS43L22_REG_Speaker_A_Volume                0x24
#define CS43L22_REG_Speaker_B_Volume                0x25
#define CS43L22_REG_Channel_Mixer_Swap              0x26
#define CS43L22_REG_Limit_Ctl_1_Thresholds          0x27
#define CS43L22_REG_Limit_Ctl_2_Release Rate        0x28
#define CS43L22_REG_Limiter_Attack_Rate             0x29
#define CS43L22_REG_Overflow_Clock_Status           0x2E
#define CS43L22_REG_Battery_Compensation            0x2F
#define CS43L22_REG_VP_Battery_Level                0x30
#define CS43L22_REG_Speaker_Status                  0x31
#define CS43L22_REG_Charge_Pump_Frequency           0x34

//-----------------------------------------------------------------------------

// read a dac register
static int cs4x_rd(struct cs4x_dac *dac, uint8_t reg, uint8_t * val) {
	uint8_t buf[1] = { reg };
	int rc;
	rc = i2c_wr_buf(dac->i2c, dac->adr, buf, 1);
	if (rc != 0) {
		return rc;
	}
	rc = i2c_rd_buf(dac->i2c, dac->adr, buf, 1);
	if (rc != 0) {
		return rc;
	}
	*val = buf[0];
	return 0;
}

// write a dac register
static int cs4x_wr(struct cs4x_dac *dac, uint8_t reg, uint8_t val) {
	uint8_t buf[2] = { reg, val };
	return i2c_wr_buf(dac->i2c, dac->adr, buf, 2);
}

// set bits in a register
static int cs4x_set(struct cs4x_dac *dac, uint8_t reg, uint8_t bits) {
	uint8_t val;
	int rc;
	rc = cs4x_rd(dac, reg, &val);
	if (rc != 0) {
		return rc;
	}
	val |= bits;
	return cs4x_wr(dac, reg, val);
}

// clear bits in a register
static int cs4x_clr(struct cs4x_dac *dac, uint8_t reg, uint8_t bits) {
	uint8_t val;
	int rc;
	rc = cs4x_rd(dac, reg, &val);
	if (rc != 0) {
		return rc;
	}
	val &= ~bits;
	return cs4x_wr(dac, reg, val);
}

//-----------------------------------------------------------------------------

// read and verify the device id
static int cs4x_id(struct cs4x_dac *dac) {
	uint8_t id;
	int rc;
	rc = cs4x_rd(dac, CS43L22_REG_ID, &id);
	if (rc != 0) {
		return rc;
	}
	if ((id & 0xf8) != 0xe0) {
		return -1;
	}
	return 0;
}

//-----------------------------------------------------------------------------

// set the output device
int cs4x_output(struct cs4x_dac *dac, unsigned int out) {
	const uint8_t ctrl[DAC_OUTPUT_MAX] = { 0xff, 0xfa, 0xaf, 0xaa, 0x05 };
	if (out >= DAC_OUTPUT_MAX) {
		out = DAC_OUTPUT_OFF;
	}
	return cs4x_wr(dac, CS43L22_REG_Power_Ctl_2, ctrl[out]);
}

//-----------------------------------------------------------------------------

#define VOL_0DB (float)(102.0/114.0)

// set the master volume
int cs4x_volume(struct cs4x_dac *dac, float vol) {
	uint8_t x;
	int rc;
	vol = clamp(vol, 0.0, 1.0);
	// piecewise linear mapping from float vol to byte
	if (vol < VOL_0DB) {
		x = 52 + (uint8_t) (vol * ((256.0 - 52.0) / VOL_0DB));
	} else {
		x = (uint8_t) ((vol - VOL_0DB) * (24.0) / (1.0 - VOL_0DB));
	}
	rc = cs4x_wr(dac, CS43L22_REG_Master_A_Vol, x);
	rc |= cs4x_wr(dac, CS43L22_REG_Master_B_Vol, x);
	return rc;
}

//-----------------------------------------------------------------------------

int cs4x_init(struct cs4x_dac *dac, struct i2c_bus *i2c, uint8_t adr, int rst) {
	int rc;

	dac->i2c = i2c;
	dac->adr = adr;
	dac->rst = rst;

	// 4.9 Recommended Power-Up Sequence (1,2)
	// reset the dac
	gpio_clr(dac->rst);
	gpio_set(dac->rst);

	rc = cs4x_id(dac);
	if (rc != 0) {
		DBG("cs4x bad device id %d\r\n", rc);
		goto exit;
	}
	// 4.9 Recommended Power-Up Sequence (4)
	// 4.11 Required Initialization Settings
	rc |= cs4x_wr(dac, 0, 0x99);
	rc |= cs4x_wr(dac, 0x47, 0x80);
	rc |= cs4x_set(dac, 0x32, 1 << 7);
	rc |= cs4x_clr(dac, 0x32, 1 << 7);
	rc |= cs4x_wr(dac, 0, 0);

	// TODO clock setup

	// set the output to AUTO
	rc |= cs4x_output(dac, DAC_OUTPUT_AUTO);
	// Clock configuration: Auto detection
	rc |= cs4x_wr(dac, CS43L22_REG_Clocking_Ctl, 0x81);
	// Set the Slave Mode and the audio Standard
	rc |= cs4x_wr(dac, CS43L22_REG_Interface_Ctl_1, 0x04);

	// Set the Master volume
	rc |= cs4x_volume(dac, 0.5);

#if 0

	/* If the Speaker is enabled, set the Mono mode and volume attenuation level */
	if (OutputDevice != OUTPUT_DEVICE_HEADPHONE) {
		/* Set the Speaker Mono mode */
		counter += CODEC_IO_Write(DeviceAddr, 0x0F, 0x06);

		/* Set the Speaker attenuation level */
		counter += CODEC_IO_Write(DeviceAddr, 0x24, 0x00);
		counter += CODEC_IO_Write(DeviceAddr, 0x25, 0x00);
	}
#endif

	// 4.9 Recommended Power-Up Sequence (6)
	rc |= cs4x_wr(dac, CS43L22_REG_Power_Ctl_1, 0x9e);

	if (rc != 0) {
		goto exit;
	}

 exit:
	return rc;
}

//-----------------------------------------------------------------------------