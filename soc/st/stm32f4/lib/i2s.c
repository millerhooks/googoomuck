//-----------------------------------------------------------------------------
/*

I2S Driver

*/
//-----------------------------------------------------------------------------

#include <string.h>
#include <assert.h>

#include "stm32f4_soc.h"
#include "utils.h"

#define DEBUG
#include "logging.h"

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
// i2s clock configuration

struct i2s_clk_cfg {
	uint32_t fs;		// nominal sample rate
	uint16_t plln;
	uint8_t pllr;
	uint8_t div;
	uint8_t odd;
};

static const struct i2s_clk_cfg *i2s_clk_lookup(uint32_t fs) {
	unsigned int i;
	// from ./scripts/i2sclk.py (mckoe = 1, chlen = 16/32)
	static const struct i2s_clk_cfg clk_cfg[] = {
		{12000, 384, 5, 12, 1},	// 12000 Hz
		{22050, 429, 4, 9, 1},	// 22049.753289 Hz
		{24000, 424, 3, 11, 1},	// 24003.623188 Hz
		{35156, 270, 5, 3, 0},	// 35156.25 Hz (goom rate)
		{44100, 429, 2, 9, 1},	// 44099.506579 Hz
		{48000, 430, 7, 2, 1},	// 47991.071429 Hz
		{96000, 344, 2, 3, 1},	// 95982.142857 Hz
		{192000, 393, 2, 2, 0},	// 191894.531250 Hz
	};
	for (i = 0; i < (sizeof(clk_cfg) / sizeof(struct i2s_clk_cfg)); i++) {
		if (clk_cfg[i].fs == fs) {
			return &clk_cfg[i];
		}
	}
	return NULL;
}

// Initialise the i2s clock.
int set_i2sclk(uint32_t fs) {
	RCC_PeriphCLKInitTypeDef rccclkinit;
	// lookup the clock configuration
	const struct i2s_clk_cfg *cfg = i2s_clk_lookup(fs);
	if (cfg == NULL) {
		return -1;
	}
	// Setup the i2s pll to generate i2s_clk
	HAL_RCCEx_GetPeriphCLKConfig(&rccclkinit);
	rccclkinit.PeriphClockSelection = RCC_PERIPHCLK_I2S;
	rccclkinit.PLLI2S.PLLI2SN = cfg->plln;
	rccclkinit.PLLI2S.PLLI2SR = cfg->pllr;
	return (int)HAL_RCCEx_PeriphCLKConfig(&rccclkinit);
}

// return the i2s clock rate
uint32_t get_i2sclk(void) {
	return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_I2S);
}

//-----------------------------------------------------------------------------

// return the configured sampling frequency
uint32_t i2s_get_fsclk(struct i2s_drv * i2s) {
	uint32_t chlen = i2s->regs->I2SCFGR & 1;
	uint32_t spr = i2s->regs->I2SPR;
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

// define the non-reserved register bits
#define I2SCFGR_MASK (0xfbfU)
#define I2SPR_MASK (0x3ffU)
#define CR2_MASK (0xf7U)

int i2s_init(struct i2s_drv *i2s, struct i2s_cfg *cfg) {
	uint32_t val;
	const struct i2s_clk_cfg *clk_cfg = i2s_clk_lookup(cfg->fs);
	if (clk_cfg == NULL) {
		return -1;
	}
	memset(i2s, 0, sizeof(struct i2s_drv));
	i2s->cfg = *cfg;
	i2s->regs = spi_base(i2s->cfg.idx);

	// enable the spi module
	spi_enable(i2s);

	// clear I2SCFGR (allow configuration)
	i2s->regs->I2SCFGR &= ~I2SCFGR_MASK;

	// setup I2SCFGR
	val = (1 << 11 /*I2SMOD */ );	// i2s (not spi)
	val |= i2s->cfg.mode;	// operating mode
	val |= i2s->cfg.standard;	// standard selection
	val |= i2s->cfg.cpol;	// steady state clock polarity
	val |= i2s->cfg.data_format;	// data and channel length
	reg_rmw(&i2s->regs->I2SCFGR, I2SCFGR_MASK, val);

	// setup I2SPR
	val = (clk_cfg->odd << 8);	// odd factor for the prescaler
	val |= (clk_cfg->div << 0);	// linear prescaler
	val |= i2s->cfg.mckoe;	// master clock output enable
	reg_rmw(&i2s->regs->I2SPR, I2SPR_MASK, val);

	// setup dma
	reg_rmw(&i2s->regs->CR2, CR2_MASK, i2s->cfg.dma);

	return 0;
}

//-----------------------------------------------------------------------------

static inline int i2s_tx_empty(struct i2s_drv *i2s) {
	return i2s->regs->SR & (1 << 1 /*TXE*/);
}

int i2s_wr(struct i2s_drv *i2s, int16_t val) {
	int rc = 0;
	int delay = 10;
	while (!i2s_tx_empty(i2s) && (delay > 0)) {
		udelay(10);
		delay -= 1;
	}
	if (delay > 0) {
		i2s->regs->DR = (uint16_t) val;
	} else {
		rc = -1;
	}
	return rc;
}

//-----------------------------------------------------------------------------
