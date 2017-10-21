//-----------------------------------------------------------------------------
/*

I2S Driver

*/
//-----------------------------------------------------------------------------

#include <string.h>
#include <assert.h>

#define DEBUG

#include "stm32f4_soc.h"
#include "logging.h"
#include "utils.h"

//-----------------------------------------------------------------------------

// return a pointer to the spi registers (1,2,3)
static inline SPI_TypeDef *spi_base(int n) {
	static const uint32_t base[] = { 0, SPI1_BASE, SPI2_BASE, SPI3_BASE };
	return (SPI_TypeDef *) base[n];
}

// return a point to the i2sext registers (2,3)
static inline SPI_TypeDef *i2sext_base(int n) {
	static const uint32_t base[] = { 0, 0, I2S2ext_BASE, I2S3ext_BASE };
	return (SPI_TypeDef *) base[n];
}

//-----------------------------------------------------------------------------

// enable the clock to the spi module
static void spi_enable(struct i2s_drv *i2s) {
	int idx = i2s->cfg.idx;
	if (idx == 1) {
		RCC->APB2ENR |= (1 << 12 /*SPI1 clock enable */ );
	} else if (idx == 2) {
		RCC->APB1ENR |= (1 << 14 /*SPI2 clock enable */ );
	} else if (idx == 3) {
		RCC->APB1ENR |= (1 << 15 /*SPI3 clock enable */ );
	}
}

//-----------------------------------------------------------------------------

// Initialise the i2s clock.
int set_i2sclk(uint32_t plln, uint32_t pllr) {
	RCC_PeriphCLKInitTypeDef rccclkinit;
	// Setup the i2s pll to generate i2s_clk
	HAL_RCCEx_GetPeriphCLKConfig(&rccclkinit);
	rccclkinit.PeriphClockSelection = RCC_PERIPHCLK_I2S;
	rccclkinit.PLLI2S.PLLI2SN = plln;
	rccclkinit.PLLI2S.PLLI2SR = pllr;
	return (int)HAL_RCCEx_PeriphCLKConfig(&rccclkinit);
}

// return the i2s clock rate
uint32_t get_i2sclk(void) {
	return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_I2S);
}

//-----------------------------------------------------------------------------

// return the configured sampling frequency
uint32_t i2s_get_fsclk(struct i2s_drv * i2s) {
	uint32_t chlen = i2s->base->I2SCFGR & 1;
	uint32_t spr = i2s->base->I2SPR;
	uint32_t mckoe = (spr >> 9) & 1;
	uint32_t odd = (spr >> 8) & 1;
	uint32_t div = spr & 0xff;
	uint32_t fs = get_i2sclk() / ((div << 1) + odd);
	if (mckoe) {
		fs >>= 8;
	} else {
		if (chlen) {	// 32 bits
			fs >>= 6;
		} else {	// 16 bits
			fs >>= 5;
		}
	}
	return fs;
}

//-----------------------------------------------------------------------------

#define I2SCFGR_MASK ((0x1f << 7) | 0x3f)
#define I2SPR_MASK (0x3ff)

int i2s_init(struct i2s_drv *i2s, struct i2s_cfg *cfg) {
	uint32_t val;

	memset(i2s, 0, sizeof(struct i2s_drv));
	i2s->cfg = *cfg;
	i2s->base = spi_base(i2s->cfg.idx);

	// TODO support full duplex mode
	assert(i2s->cfg.fdx == 0);

	// enable the spi module
	spi_enable(i2s);

	// clear I2SCFGR
	i2s->base->I2SCFGR &= ~I2SCFGR_MASK;

	// set I2SPR to the reset value
	reg_rmw(&i2s->base->I2SPR, I2SPR_MASK, 2);

	// setup I2SCFGR
	val = (1 << 11 /*I2SMOD */ );
	val |= (i2s->cfg.mode | i2s->cfg.standard | i2s->cfg.cpol | i2s->cfg.data_format);
	reg_rmw(&i2s->base->I2SCFGR, I2SCFGR_MASK, val);

	// setup I2SPR
	reg_rmw(&i2s->base->I2SPR, 0x3ff, (i2s->cfg.odd << 8) | (i2s->cfg.div << 0) | i2s->cfg.mckoe);
	return 0;
}

//-----------------------------------------------------------------------------

static inline int i2s_tx_empty(struct i2s_drv *i2s) {
	return i2s->base->SR & (1 << 1 /*TXE*/);
}

int i2s_wr(struct i2s_drv *i2s, uint16_t val) {
	int rc = 0;
	int delay = 10;
	while (!i2s_tx_empty(i2s) && (delay > 0)) {
		udelay(10);
		delay -= 1;
	}
	if (delay > 0) {
		i2s->base->DR = val;
	} else {
		rc = -1;
	}
	return rc;
}

//-----------------------------------------------------------------------------
