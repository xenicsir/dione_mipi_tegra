/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tc358746 - Parallel to CSI-2 bridge
 *
 * Copyright 2018 Marco Felsch <kernel@pengutronix.de>
 *
 * References:
 * REF_01:
 * - TC358746AXBG/TC358748XBG/TC358748IXBG Functional Specification Rev 1.2
 * REF_02:
 * - TC358746(A)748XBG_Parallel-CSI2_Tv23p.xlsx, Rev Tv23
 *
 * Calculation extracted by Peter Rozsahegyi <peter.rozsahegyi@pcbdesign.hu>
 */

#include <linux/module.h>
#include <uapi/linux/media-bus-format.h>

#include "tc358746_calculation.h"
#include "tc358746_regs.h"

#define DATAFMT_PDFMT_RGB888		3
#define CONFCTL_PDATAF_MODE0		0
#define TC358746_MAX_FIFO_SIZE		512

#define TC358746_LINEINIT_MIN_US	110
#define TC358746_TWAKEUP_MIN_US		1200
#define TC358746_LPTXTIME_MIN_NS	55
#define TC358746_TCLKZERO_MIN_NS	305
#define TC358746_TCLKTRAIL_MIN_NS	65
#define TC358746_TCLKPOST_MIN_NS	65
#define TC358746_THSZERO_MIN_NS		150
#define TC358746_THSTRAIL_MIN_NS	65
#define TC358746_THSPREPARE_MIN_NS	45

#ifdef TC358746_DEFINE_LOGS
#include "tc358746_logs.h"
#endif

#ifndef log_error
#define log_error(fmt, args...)
#endif
#ifndef log_info
#define log_info(fmt, args...)
#endif

/* TODO: Add other formats as required */
static const struct tc358746_mbus_fmt tc358746_formats[] = {
	{
		.code = MEDIA_BUS_FMT_UYVY8_2X8,
		.bus_width = 8,
		.bpp = 16,
		.pdformat = DATAFMT_PDFMT_YCBCRFMT_422_8_BIT,
		.pdataf = CONFCTL_PDATAF_MODE0,
		.ppp = 2,
	}, {
		.code = MEDIA_BUS_FMT_UYVY8_1X16,
		.bus_width = 16,
		.bpp = 16,
		.pdformat = DATAFMT_PDFMT_YCBCRFMT_422_8_BIT,
		.pdataf = CONFCTL_PDATAF_MODE1,
		.ppp = 1,
	}, {
		.code = MEDIA_BUS_FMT_YUYV8_1X16,
		.bus_width = 16,
		.bpp = 16,
		.pdformat = DATAFMT_PDFMT_YCBCRFMT_422_8_BIT,
		.pdataf = CONFCTL_PDATAF_MODE2,
		.ppp = 1,
	}, {
		.code = MEDIA_BUS_FMT_UYVY10_2X10,
		.bus_width = 10,
		.bpp = 20,
		.pdformat = DATAFMT_PDFMT_YCBCRFMT_422_10_BIT,
		.pdataf = CONFCTL_PDATAF_MODE0, /* don't care */
		.ppp = 2,
	}, {
		/* in datasheet listed as YUV444 */
		.code = MEDIA_BUS_FMT_GBR888_1X24,
		.bus_width = 24,
		.bpp = 24,
		.pdformat = DATAFMT_PDFMT_YCBCRFMT_444,
		.pdataf = CONFCTL_PDATAF_MODE0, /* don't care */
		.ppp = 2,
		.csitx_only = true,
	}, {
		.code = MEDIA_BUS_FMT_RGB888_1X24,
		.bus_width = 24,
		.bpp = 24,
		.pdformat = DATAFMT_PDFMT_RGB888,
		.pdataf = CONFCTL_PDATAF_MODE0,
		.ppp = 1,
	},
};

static const struct tc358746_mbus_fmt *tc358746_get_format(u32 code)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(tc358746_formats); i++)
		if (tc358746_formats[i].code == code)
			return &tc358746_formats[i];

	return NULL;
}

static int tc358746_adjust_fifo_size(const struct tc358746_input *input,
				     const struct tc358746_mbus_fmt *format,
				     struct tc358746_csi *csi_settings,
				     u16 *fifo_size)
{
	int c_hactive_ps_diff, c_lp_active_ps_diff, c_fifo_delay_ps_diff;
	unsigned int p_hactive_ps, p_hblank_ps, p_htotal_ps;
	unsigned int c_hactive_ps, c_lp_active_ps, c_fifo_delay_ps;
	unsigned int csi_bps, csi_bps_period_ps;
	unsigned int csi_hsclk, csi_hsclk_period_ps;
	unsigned int pclk_period_ps;
	unsigned int _fifo_size;

	pclk_period_ps = 1000000000 / (input->pclk / 1000);
	csi_bps = csi_settings->speed_per_lane * csi_settings->lane_num;
	csi_bps_period_ps = 1000000000 / (csi_bps / 1000);
	csi_hsclk = csi_settings->speed_per_lane >> 3;
	csi_hsclk_period_ps = 1000000000 / (csi_hsclk / 1000);

	/*
	 * Calculation:
	 * p_hactive_ps = pclk_period_ps * pclk_per_pixel * h_active_pixel
	 */
	p_hactive_ps = pclk_period_ps * format->ppp * input->width;

	/*
	 * Calculation:
	 * p_hblank_ps = pclk_period_ps * h_blank_pixel
	 */
	p_hblank_ps = pclk_period_ps * input->hblank;
	p_htotal_ps = p_hblank_ps + p_hactive_ps;

	/*
	 * Adjust the fifo size to adjust the csi timing. Hopefully we can find
	 * a fifo size where the parallel input timings and the csi tx timings
	 * fit together.
	 */
	for (_fifo_size = 1; _fifo_size < TC358746_MAX_FIFO_SIZE; _fifo_size++) {
		/*
		 * Calculation:
		 * c_fifo_delay_ps = (fifo_size * 32) / parallel_bus_width *
		 *                   pclk_period_ps + 4 * csi_hsclk_period_ps
		 */
		c_fifo_delay_ps = _fifo_size * 32 * pclk_period_ps;
		c_fifo_delay_ps /= format->bus_width;
		c_fifo_delay_ps += 4 * csi_hsclk_period_ps;

		/*
		 * Calculation:
		 * c_hactive_ps = csi_bps_period_ps * image_bpp * h_active_pixel
		 *                + c_fifo_delay
		 */
		c_hactive_ps = csi_bps_period_ps * format->bpp * input->width;
		c_hactive_ps += c_fifo_delay_ps;

		/*
		 * Calculation:
		 * c_lp_active_ps = p_htotal_ps - c_hactive_ps
		 */
		c_lp_active_ps = p_htotal_ps - c_hactive_ps;

		c_hactive_ps_diff = c_hactive_ps - p_hactive_ps;
		c_fifo_delay_ps_diff = p_htotal_ps - c_hactive_ps;
		c_lp_active_ps_diff =
		    c_lp_active_ps - csi_settings->csi_hs_lp_hs_ps;

		if (c_hactive_ps_diff > 0 &&
		    c_fifo_delay_ps_diff > 0 && c_lp_active_ps_diff > 0)
			break;
	}

	/*
	 * If we can't transfer the image using this csi link frequency try to
	 * use another link freq.
	 */
	log_info("found fifo-size %d\n",
		 _fifo_size == TC358746_MAX_FIFO_SIZE ? -1 : _fifo_size);
	*fifo_size = _fifo_size;
	return _fifo_size == TC358746_MAX_FIFO_SIZE ? -EINVAL : 0;
}

static int tc358746_calculate_csi_txtimings(struct tc358746_csi *csi)
{
	unsigned int spl;
	unsigned int spl_p_ps, hsclk_p_ps, hfclk_p_ns;
	unsigned int hfclk, hsclk;	/* SYSCLK */
	unsigned int tmp;
	unsigned int lptxtime_ps, tclk_post_ps, tclk_trail_ps, tclk_zero_ps,
	    ths_trail_ps, ths_zero_ps;

	spl = csi->speed_per_lane;
	hsclk = spl >> 3;  /* spl in bit-per-second, hsclk in byte-per-sercond */
	hfclk = hsclk >> 1;/* HFCLK = SYSCLK / 2 */

	if (hsclk > 125000000U) {
		log_error("unsupported HS byte clock %d, must <= 125 MHz\n", hsclk);
		return -EINVAL;
	}

	hfclk_p_ns = DIV_ROUND_CLOSEST(1000000000, hfclk);
	hsclk_p_ps = 1000000000 / (hsclk / 1000);
	spl_p_ps = 1000000000 / (spl / 1000);

	/*
	 * Calculation:
	 * hfclk_p_ns * lineinitcnt > 100us
	 * lineinitcnt > 100 * 10^-6s / hfclk_p_ns * 10^-9
	 *
	 */
	csi->lineinitcnt = DIV_ROUND_UP(TC358746_LINEINIT_MIN_US * 1000,
						hfclk_p_ns);

	/*
	 * Calculation:
	 * (lptxtimecnt + 1) * hsclk_p_ps > 50ns
	 * 38ns < (tclk_preparecnt + 1) * hsclk_p_ps < 95ns
	 */
	csi->lptxtimecnt = csi->tclk_preparecnt =
	    DIV_ROUND_UP(TC358746_LPTXTIME_MIN_NS * 1000, hsclk_p_ps) - 1;

	/*
	 * Limit:
	 * (tclk_zero + tclk_prepar) period > 300ns.
	 * Since we have no upper limit and for simplicity:
	 * tclk_zero > 300ns.
	 *
	 * Calculation:
	 * tclk_zero = ([2,3] + tclk_zerocnt) * hsclk_p_ps + ([2,3] * spl_p_ps)
	 *
	 * Note: REF_02 uses
	 * tclk_zero = (2.5 + tclk_zerocnt) * hsclk_p_ps + (3.5 * spl_p_ps)
	 */
	tmp = TC358746_TCLKZERO_MIN_NS * 1000 - 3 * spl_p_ps;
	tmp = DIV_ROUND_UP(tmp, hsclk_p_ps);
	csi->tclk_zerocnt = tmp - 2;

	/*
	 * Limit:
	 * 40ns + 4 * spl_p_ps < (ths_preparecnt + 1) * hsclk_p_ps
	 *                     < 85ns + 6 * spl_p_ps
	 */
	tmp = TC358746_THSPREPARE_MIN_NS * 1000 + 4 * spl_p_ps;
	tmp = DIV_ROUND_UP(tmp, hsclk_p_ps);
	csi->ths_preparecnt = tmp - 1;

	/*
	 * Limit:
	 * (ths_zero + ths_prepare) period > 145ns + 10 * spl_p_ps.
	 * Since we have no upper limit and for simplicity:
	 * ths_zero period > 145ns + 10 * spl_p_ps.
	 *
	 * Calculation:
	 * ths_zero = ([6,8] + ths_zerocnt) * hsclk_p_ps + [3,4] * hsclk_p_ps +
	 *            [13,14] * spl_p_ps
	 *
	 * Note: REF_02 uses
	 * ths_zero = (7 + ths_zerocnt) * hsclk_p_ps + 4 * hsclk_p_ps +
	 *            11 * spl_p_ps
	 */
	tmp = TC358746_THSZERO_MIN_NS * 1000 - spl_p_ps;
	tmp = DIV_ROUND_UP(tmp, hsclk_p_ps);
	csi->ths_zerocnt = tmp < 11 ? 0 : tmp - 11;

	/*
	 * Limit:
	 * hsclk_p_ps * (lptxtimecnt + 1) * (twakeupcnt + 1) > 1ms
	 *
	 * Since we have no upper limit use 1.2ms as lower limit to
	 * surley meet the spec limit.
	 */
	tmp = hsclk_p_ps / 1000;	/* tmp = hsclk_p_ns */
	csi->twakeupcnt =
	    DIV_ROUND_UP(TC358746_TWAKEUP_MIN_US * 1000,
			 tmp * (csi->lptxtimecnt + 1)) - 1;

	/*
	 * Limit:
	 * 60ns + 4 * spl_p_ps < thstrail < 105ns + 12 * spl_p_ps
	 *
	 * Calculation:
	 * thstrail = (1 + ths_trailcnt) * hsclk_p_ps + [3,4] * hsclk_p_ps -
	 *            [13,14] * spl_p_ps
	 *
	 * [2] set formula to:
	 * thstrail = (1 + ths_trailcnt) * hsclk_p_ps + 4 * hsclk_p_ps -
	 *            11 * spl_p_ps
	 */
	tmp = TC358746_THSTRAIL_MIN_NS * 1000 + 15 * spl_p_ps;
	tmp = DIV_ROUND_UP(tmp, hsclk_p_ps);
	csi->ths_trailcnt = tmp - 5;

	/*
	 * Limit:
	 * 60ns < tclk_trail < 105ns + 12 * spl_p_ps
	 *
	 * Limit used by REF_02:
	 * 60ns < tclk_trail < 105ns + 12 * spl_p_ps - 30
	 *
	 * Calculation:
	 * tclk_trail = ([1,2] + tclk_trailcnt) * hsclk_p_ps +
	 *              (2 + [1,2]) * hsclk_p_ps - [2,3] * spl_p_ps
	 *
	 * Calculation used by REF_02:
	 * tclk_trail = (1 + tclk_trailcnt) * hsclk_p_ps +
	 *              4 * hsclk_p_ps - 3 * spl_p_ps
	 */
	tmp = TC358746_TCLKTRAIL_MIN_NS * 1000 + 3 * spl_p_ps;
	tmp = DIV_ROUND_UP(tmp, hsclk_p_ps);
	csi->tclk_trailcnt = tmp < 5 ? 0 : tmp - 5;

	/*
	 * Limit:
	 * tclk_post > 60ns + 52 * spl_p_ps
	 *
	 * Limit used by REF_02:
	 * tclk_post > 60ns + 52 * spl_p_ps
	 *
	 * Calculation:
	 * tclk_post = ([1,2] + (tclk_postcnt + 1)) * hsclk_p_ps + hsclk_p_ps
	 *
	 * Note REF_02 uses:
	 * tclk_post = (2.5 + tclk_postcnt) * hsclk_p_ps + hsclk_p_ps +
	 *              2.5 * spl_p_ps
	 * To meet the REF_02 validation limits following equation is used:
	 * tclk_post = (2 + tclk_postcnt) * hsclk_p_ps + hsclk_p_ps +
	 *              3 * spl_p_ps
	 */
	tmp = TC358746_TCLKPOST_MIN_NS * 1000 + 49 * spl_p_ps;
	tmp = DIV_ROUND_UP(tmp, hsclk_p_ps);
	csi->tclk_postcnt = tmp - 3;

	/*
	 * Last calculate the csi hs->lp->hs transistion time in ns. Note REF_02
	 * mixed units in the equation for the continuous case. I don't know if
	 * this was the intention. The driver drops the last 'multiply all by
	 * two' to get nearly the same results.
	 */
	lptxtime_ps = (csi->lptxtimecnt + 1) * hsclk_p_ps;
	tclk_post_ps =
	    (4 + csi->tclk_postcnt) * hsclk_p_ps + 3 * spl_p_ps;
	tclk_trail_ps =
	    (5 + csi->tclk_trailcnt) * hsclk_p_ps - 3 * spl_p_ps;
	tclk_zero_ps =
	    (2 + csi->tclk_zerocnt) * hsclk_p_ps + 3 * spl_p_ps;
	ths_trail_ps =
	    (5 + csi->ths_trailcnt) * hsclk_p_ps - 11 * spl_p_ps;
	ths_zero_ps =
	    (7 + csi->ths_zerocnt) * hsclk_p_ps + 4 * hsclk_p_ps +
	    11 * spl_p_ps;

	if (csi->is_continuous_clk) {
		tmp = 2 * lptxtime_ps;
		tmp += 25 * hsclk_p_ps;
		tmp += ths_trail_ps;
		tmp += ths_zero_ps;
	} else {
		tmp = 4 * lptxtime_ps;
		tmp += ths_trail_ps + tclk_post_ps + tclk_trail_ps +
		    tclk_zero_ps + ths_zero_ps;
		tmp += (13 + csi->lptxtimecnt * 8) * hsclk_p_ps;
		tmp += 22 * hsclk_p_ps;
		tmp *= 3;
		tmp = DIV_ROUND_CLOSEST(tmp, 2);
	}
	csi->csi_hs_lp_hs_ps = tmp;

	return 0;
}

static int tc358746_set_lane_settings(const struct tc358746_input *input,
				      const struct tc358746_pll *pll,
				      struct tc358746_csi *csi)
{
	struct tc358746_csi *s = csi;
	u32 bps_pr_lane;

	/*
	 * The CSI bps per lane must be between 62.5 Mbps and 1 Gbps.
	 * bps_pr_lane = 2 * link_freq, because MIPI data lane is double
	 * data rate.
	 */
	bps_pr_lane = 2 * input->link_frequency;
	if (bps_pr_lane < 62500000U || bps_pr_lane > 1000000000U) {
		log_error("unsupported bps per lane: %u bps\n", bps_pr_lane);
		return -EINVAL;
	}

	if (bps_pr_lane > 500000000)
		s->speed_range = 0;
	else if (bps_pr_lane > 250000000)
		s->speed_range = 1;
	else if (bps_pr_lane > 125000000)
		s->speed_range = 2;
	else
		s->speed_range = 3;

	s->unit_clk_hz = pll->pllinclk_hz >> s->speed_range;
	s->unit_clk_mul = bps_pr_lane / s->unit_clk_hz;
	s->speed_per_lane = bps_pr_lane;
	s->lane_num = input->num_lanes;
	s->is_continuous_clk = !input->discontinuous_clk;

	log_info("unit_clk %uHz: unit_clk_mul %u: speed_range %u: speed_per_lane(bps/lane) %u: csi_lane_numbers %u\n",
		s->unit_clk_hz, s->unit_clk_mul, s->speed_range,
		s->speed_per_lane, s->lane_num);

	return 0;
}

static int tc358746_setup_pll(const struct tc358746_input *input,
			      struct tc358746_pll *pll)
{
	unsigned int pllinclk;
	unsigned char pll_prediv;

	if (input->refclk < 6000000 || input->refclk > 40000000) {
		log_error("refclk must between 6MHz and 40MHz\n");
		return -EINVAL;
	}

	/*
	 * The PLL input clock is obtained by dividing refclk by pll_prd.
	 * It must be between 4 MHz and 40 MHz, lower frequency is better.
	 */
	pll_prediv = DIV_ROUND_CLOSEST(input->refclk, 4000000);
	if (pll_prediv < 1 || pll_prediv > 16) {
		log_error("invalid pll pre-divider value: %d\n", pll_prediv);
		return -EINVAL;
	}
	pll->pll_prd = pll_prediv;

	pllinclk = DIV_ROUND_CLOSEST(input->refclk, pll_prediv);
	if (pllinclk < 4000000 || pllinclk > 40000000) {
		log_error("invalid pll input clock: %d Hz\n", pllinclk);
		return -EINVAL;
	}

	pll->pllinclk_hz = pllinclk;

	return 0;
}

static void tc358746_setup_pll_post(struct tc358746_pll *pll,
				    struct tc358746_csi *csi)
{
	u16 pll_frs = csi->speed_range;

	/*
	 * Calculation:
	 * speed_per_lane = (pllinclk_hz * (fbd + 1)) / 2^frs
	 *
	 * Calculation used by REF_02:
	 * speed_per_lane = (pllinclk_hz * fbd) / 2^frs
	 */
#if 1
	pll->pll_fbd =
	    DIV_ROUND_CLOSEST(csi->speed_per_lane, pll->pllinclk_hz);
	pll->pll_fbd <<= pll_frs;
	pll->pll_fbd -= csi->speed_range;
#else
	pll->pll_fbd = csi->speed_per_lane / pll->pllinclk_hz;
	pll->pll_fbd <<= pll_frs;
#endif
}

int tc358746_calculate(struct tc358746 *self,
		       const struct tc358746_input *input)
{
	const struct tc358746_mbus_fmt *format;
	struct tc358746_pll pll;
	struct tc358746_csi csi;
	u16 vb_fifo;

	format = tc358746_get_format(input->mbus_fmt);
	if (!format)
		return -EINVAL;

	if (tc358746_setup_pll(input, &pll) < 0)
		return -EINVAL;

	if (tc358746_set_lane_settings(input, &pll, &csi) < 0)
		return -EINVAL;

	tc358746_setup_pll_post(&pll, &csi);

	if (tc358746_calculate_csi_txtimings(&csi) < 0)
		return -EINVAL;

	if (tc358746_adjust_fifo_size(input, format, &csi, &vb_fifo) < 0)
		return -EINVAL;

	self->format = format;
	self->pll = pll;
	self->csi = csi;
	self->vb_fifo = vb_fifo;

	return 0;
}
