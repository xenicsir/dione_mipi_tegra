/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __TC358746_CALCULATION_H
#define __TC358746_CALCULATION_H

#include <stdbool.h>
#include <linux/types.h>

struct tc358746_mbus_fmt {
	u32 code;
	u8 bus_width;
	u8 bpp;			/* total bpp */
	u8 pdformat;		/* peripheral data format */
	u8 pdataf;		/* parallel data format option */
	u8 ppp;			/* pclk per pixel */
	bool csitx_only;	/* format only in csi-tx mode supported */
};

struct tc358746_pll {
	/* internal pll */
	unsigned int pllinclk_hz;
	u16 pll_prd;
	u16 pll_fbd;
};

struct tc358746_csi {
	unsigned char speed_range;
	unsigned int unit_clk_hz;
	unsigned char unit_clk_mul;
	unsigned int speed_per_lane;	/* bps / lane */
	unsigned short lane_num;
	bool is_continuous_clk;

	/* CSI2-TX Parameters */
	u32 lineinitcnt;
	u32 lptxtimecnt;
	u32 twakeupcnt;
	u32 tclk_preparecnt;
	u32 tclk_zerocnt;
	u32 tclk_trailcnt;
	u32 tclk_postcnt;
	u32 ths_preparecnt;
	u32 ths_zerocnt;
	u32 ths_trailcnt;

	unsigned int csi_hs_lp_hs_ps;
};

struct tc358746 {
	const struct tc358746_mbus_fmt *format;
	struct tc358746_pll pll;
	struct tc358746_csi csi;
	u16 vb_fifo;
};

struct tc358746_input {
	/* get_format */
	int mbus_fmt;
	/* setup_pll */
	u32 refclk;
	/* set_lane_settings */
	u64 link_frequency;
	int num_lanes;
	bool discontinuous_clk;
	/* adjust_fifo_size */
	unsigned int pclk;
	unsigned int width;
	unsigned int hblank;
};

int tc358746_calculate(struct tc358746 *self,
		       const struct tc358746_input *input);

#endif
