#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tc358746_calculation.h"
#include "tc358746_regs.h"
#include <uapi/linux/media-bus-format.h>

static const struct tc358746_input inputs[] = {
	{
		.mbus_fmt = MEDIA_BUS_FMT_RGB888_1X24,
		.refclk = 24000000,
		.link_frequency = 249000000,
		.num_lanes = 2,
		.discontinuous_clk = false,
		.pclk = 20000000,
		.width = 640,
		.hblank = 54,/* TODO: 44 would be better??? */
	},
	{
		.mbus_fmt = MEDIA_BUS_FMT_RGB888_1X24,
		.refclk = 24000000,
		.link_frequency = 500000000,
		.num_lanes = 2,
		.discontinuous_clk = false,
		.pclk = 83000000,
		.width = 1280,
		.hblank = 54,
	},
	{
		.mbus_fmt = MEDIA_BUS_FMT_RGB888_1X24,
		.refclk = 24000000,
		.link_frequency = 249000000,
		.num_lanes = 2,
		.discontinuous_clk = false,
		.pclk = 20000000,
		.width = 320,
		.hblank = 1084,
	},
	{
		.mbus_fmt = MEDIA_BUS_FMT_RGB888_1X24,
		.refclk = 24000000,
		.link_frequency = 500000000,
		.num_lanes = 2,
		.discontinuous_clk = false,
		.pclk = 83000000,
		.width = 1024,
		.hblank = 55,
	},
};

#define bool_str(val) ((val) ? "true" : "false")
#define add_case(code) case code: return #code

static const char *mbus_fmt_to_str(u32 code)
{
	switch (code) {
		add_case(MEDIA_BUS_FMT_FIXED);
		add_case(MEDIA_BUS_FMT_RGB444_1X12);
		add_case(MEDIA_BUS_FMT_RGB444_2X8_PADHI_BE);
		add_case(MEDIA_BUS_FMT_RGB444_2X8_PADHI_LE);
		add_case(MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE);
		add_case(MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE);
		add_case(MEDIA_BUS_FMT_RGB565_1X16);
		add_case(MEDIA_BUS_FMT_BGR565_2X8_BE);
		add_case(MEDIA_BUS_FMT_BGR565_2X8_LE);
		add_case(MEDIA_BUS_FMT_RGB565_2X8_BE);
		add_case(MEDIA_BUS_FMT_RGB565_2X8_LE);
		add_case(MEDIA_BUS_FMT_RGB666_1X18);
		add_case(MEDIA_BUS_FMT_RBG888_1X24);
		add_case(MEDIA_BUS_FMT_RGB666_1X24_CPADHI);
		add_case(MEDIA_BUS_FMT_RGB666_1X7X3_SPWG);
		add_case(MEDIA_BUS_FMT_BGR888_1X24);
		add_case(MEDIA_BUS_FMT_GBR888_1X24);
		add_case(MEDIA_BUS_FMT_RGB888_1X24);
		add_case(MEDIA_BUS_FMT_RGB888_2X12_BE);
		add_case(MEDIA_BUS_FMT_RGB888_2X12_LE);
		add_case(MEDIA_BUS_FMT_RGB888_1X7X4_SPWG);
		add_case(MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA);
		add_case(MEDIA_BUS_FMT_ARGB8888_1X32);
		add_case(MEDIA_BUS_FMT_RGB888_1X32_PADHI);
		add_case(MEDIA_BUS_FMT_Y8_1X8);
		add_case(MEDIA_BUS_FMT_UV8_1X8);
		add_case(MEDIA_BUS_FMT_UYVY8_1_5X8);
		add_case(MEDIA_BUS_FMT_VYUY8_1_5X8);
		add_case(MEDIA_BUS_FMT_YUYV8_1_5X8);
		add_case(MEDIA_BUS_FMT_YVYU8_1_5X8);
		add_case(MEDIA_BUS_FMT_UYVY8_2X8);
		add_case(MEDIA_BUS_FMT_VYUY8_2X8);
		add_case(MEDIA_BUS_FMT_YUYV8_2X8);
		add_case(MEDIA_BUS_FMT_YVYU8_2X8);
		add_case(MEDIA_BUS_FMT_Y10_1X10);
		add_case(MEDIA_BUS_FMT_UYVY10_2X10);
		add_case(MEDIA_BUS_FMT_VYUY10_2X10);
		add_case(MEDIA_BUS_FMT_YUYV10_2X10);
		add_case(MEDIA_BUS_FMT_YVYU10_2X10);
		add_case(MEDIA_BUS_FMT_Y12_1X12);
		add_case(MEDIA_BUS_FMT_UYVY12_2X12);
		add_case(MEDIA_BUS_FMT_VYUY12_2X12);
		add_case(MEDIA_BUS_FMT_YUYV12_2X12);
		add_case(MEDIA_BUS_FMT_YVYU12_2X12);
		add_case(MEDIA_BUS_FMT_UYVY8_1X16);
		add_case(MEDIA_BUS_FMT_VYUY8_1X16);
		add_case(MEDIA_BUS_FMT_YUYV8_1X16);
		add_case(MEDIA_BUS_FMT_YVYU8_1X16);
		add_case(MEDIA_BUS_FMT_YDYUYDYV8_1X16);
		add_case(MEDIA_BUS_FMT_UYVY10_1X20);
		add_case(MEDIA_BUS_FMT_VYUY10_1X20);
		add_case(MEDIA_BUS_FMT_YUYV10_1X20);
		add_case(MEDIA_BUS_FMT_YVYU10_1X20);
		add_case(MEDIA_BUS_FMT_VUY8_1X24);
		add_case(MEDIA_BUS_FMT_YUV8_1X24);
		add_case(MEDIA_BUS_FMT_UYVY12_1X24);
		add_case(MEDIA_BUS_FMT_VYUY12_1X24);
		add_case(MEDIA_BUS_FMT_YUYV12_1X24);
		add_case(MEDIA_BUS_FMT_YVYU12_1X24);
		add_case(MEDIA_BUS_FMT_YUV10_1X30);
		add_case(MEDIA_BUS_FMT_AYUV8_1X32);
		add_case(MEDIA_BUS_FMT_SBGGR8_1X8);
		add_case(MEDIA_BUS_FMT_SGBRG8_1X8);
		add_case(MEDIA_BUS_FMT_SGRBG8_1X8);
		add_case(MEDIA_BUS_FMT_SRGGB8_1X8);
		add_case(MEDIA_BUS_FMT_SBGGR10_ALAW8_1X8);
		add_case(MEDIA_BUS_FMT_SGBRG10_ALAW8_1X8);
		add_case(MEDIA_BUS_FMT_SGRBG10_ALAW8_1X8);
		add_case(MEDIA_BUS_FMT_SRGGB10_ALAW8_1X8);
		add_case(MEDIA_BUS_FMT_SBGGR10_DPCM8_1X8);
		add_case(MEDIA_BUS_FMT_SGBRG10_DPCM8_1X8);
		add_case(MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8);
		add_case(MEDIA_BUS_FMT_SRGGB10_DPCM8_1X8);
		add_case(MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_BE);
		add_case(MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_LE);
		add_case(MEDIA_BUS_FMT_SBGGR10_2X8_PADLO_BE);
		add_case(MEDIA_BUS_FMT_SBGGR10_2X8_PADLO_LE);
		add_case(MEDIA_BUS_FMT_SBGGR10_1X10);
		add_case(MEDIA_BUS_FMT_SGBRG10_1X10);
		add_case(MEDIA_BUS_FMT_SGRBG10_1X10);
		add_case(MEDIA_BUS_FMT_SRGGB10_1X10);
		add_case(MEDIA_BUS_FMT_SBGGR12_1X12);
		add_case(MEDIA_BUS_FMT_SGBRG12_1X12);
		add_case(MEDIA_BUS_FMT_SGRBG12_1X12);
		add_case(MEDIA_BUS_FMT_SRGGB12_1X12);
		add_case(MEDIA_BUS_FMT_SBGGR14_1X14);
		add_case(MEDIA_BUS_FMT_SGBRG14_1X14);
		add_case(MEDIA_BUS_FMT_SGRBG14_1X14);
		add_case(MEDIA_BUS_FMT_SRGGB14_1X14);
		add_case(MEDIA_BUS_FMT_SBGGR16_1X16);
		add_case(MEDIA_BUS_FMT_SGBRG16_1X16);
		add_case(MEDIA_BUS_FMT_SGRBG16_1X16);
		add_case(MEDIA_BUS_FMT_SRGGB16_1X16);
		add_case(MEDIA_BUS_FMT_XBGGR10P_3X10);
		add_case(MEDIA_BUS_FMT_XGBRG10P_3X10);
		add_case(MEDIA_BUS_FMT_XGRBG10P_3X10);
		add_case(MEDIA_BUS_FMT_XRGGB10P_3X10);
		add_case(MEDIA_BUS_FMT_JPEG_1X8);
		add_case(MEDIA_BUS_FMT_S5C_UYVY_JPEG_1X8);
		add_case(MEDIA_BUS_FMT_AHSV8888_1X32);

		default:
			return "???";
	}
}

static const char *pdformat_to_str(u8 code)
{
	switch (code) {
		add_case(DATAFMT_PDFMT_RAW8);
		add_case(DATAFMT_PDFMT_RAW10);
		add_case(DATAFMT_PDFMT_RAW12);
		add_case(DATAFMT_PDFMT_RGB888);
		add_case(DATAFMT_PDFMT_RGB666);
		add_case(DATAFMT_PDFMT_RGB565);
		add_case(DATAFMT_PDFMT_YCBCRFMT_422_8_BIT);
		add_case(DATAFMT_PDFMT_RAW14);
		add_case(DATAFMT_PDFMT_YCBCRFMT_422_10_BIT);
		add_case(DATAFMT_PDFMT_YCBCRFMT_444);

		default:
			return "???";
	}
}

static const char *pdataf_to_str(u8 code)
{
	switch (code) {
		add_case(CONFCTL_PDATAF_MODE0);
		add_case(CONFCTL_PDATAF_MODE1);
		add_case(CONFCTL_PDATAF_MODE2);

		default:
			return "???";
	}
}

static const char *speed_range_to_str(u8 speed_range)
{
	switch (speed_range) {
		case 0:
			return "500MHz - 1GHz HSCK frequency";
		case 1:
			return "250MHz – 500MHz HSCK frequency";
		case 2:
			return "125 MHz – 250MHz HSCK frequency";
		case 3:
			return "62.5MHz – 125MHz HSCK frequency";
		default:
			return "???";
	}
}

void tc358746_input_dump(const struct tc358746_input *self)
{
	fprintf(stdout,
		"\t\t/* config %u */\n"
		"\t\t.input = {\n"
		"\t\t\t.mbus_fmt = %s,\n"
		"\t\t\t.refclk = %u,\t\t\t/* Hz */\n"
		"\t\t\t.link_frequency = %lu,\t\t/* Hz */\n"
		"\t\t\t.num_lanes = %u,\n"
		"\t\t\t.discontinuous_clk = %s,\n"
		"\t\t\t.pclk = %u,\t\t\t/* Hz */\n"
		"\t\t\t.width = %u,\n"
		"\t\t\t.hblank = %u,\n"
		"\t\t},\n",
		self->width,
		mbus_fmt_to_str(self->mbus_fmt),
		self->refclk,
		self->link_frequency,
		self->num_lanes,
		bool_str(self->discontinuous_clk),
		self->pclk,
		self->width,
		self->hblank);
}

void tc358746_mbus_fmt_dump(const struct tc358746_mbus_fmt *self)
{
	fprintf(stdout,
		"\t\t.format = {\n"
		"\t\t\t.code = %s,\n"
		"\t\t\t.bus_width = %u,\n"
		"\t\t\t.bpp = %u,\t\t\t\t/* total bpp */\n"
		"\t\t\t.pdformat = %s,\t/* peripheral data format */\n"
		"\t\t\t.pdataf = %s,\t\t/* parallel data format option */\n"
		"\t\t\t.ppp = %u,\t\t\t\t/* pclk per pixel */\n"
		"\t\t\t.csitx_only = %s,\t\t\t/* format only in csi-tx mode supported */\n"
		"\t\t},\n",
		mbus_fmt_to_str(self->code),
		self->bus_width,
		self->bpp,
		pdformat_to_str(self->pdformat),
		pdataf_to_str(self->pdataf),
		self->ppp,
		bool_str(self->csitx_only));
}

void tc358746_pll_dump(const struct tc358746_pll *self)
{
	fprintf(stdout,
		"\t\t.pll = {\n"
		"\t\t\t.pllinclk_hz = %u,\n"
		"\t\t\t.pll_prd = %u,\n"
		"\t\t\t.pll_fbd = %u,\n"
		"\t\t},\n",
		self->pllinclk_hz,
		self->pll_prd,
		self->pll_fbd);
}

void tc358746_csi_dump(const struct tc358746_csi *self)
{
	fprintf(stdout,
		"\t\t.csi = {\n"
		"\t\t\t.speed_range = %u,\t\t\t/* %s */\n"
		"\t\t\t.unit_clk_hz = %u,\n"
		"\t\t\t.unit_clk_mul = %u,\n"
		"\t\t\t.speed_per_lane = %u,\t\t/* bps/lane */\n"
		"\t\t\t.lane_num = %u,\n"
		"\t\t\t.is_continuous_clk = %s,\t\t/* CSI clock during LP %sabled */\n"
		"\n"
		"\t\t\t/* CSI2-TX Parameters */\n"
		"\t\t\t.lineinitcnt = %u,\n"
		"\t\t\t.lptxtimecnt = %u,\n"
		"\t\t\t.twakeupcnt = %u,\n"
		"\t\t\t.tclk_preparecnt = %u,\n"
		"\t\t\t.tclk_zerocnt = %u,\n"
		"\t\t\t.tclk_trailcnt = %u,\n"
		"\t\t\t.tclk_postcnt = %u,\n"
		"\t\t\t.ths_preparecnt = %u,\n"
		"\t\t\t.ths_zerocnt = %u,\n"
		"\t\t\t.ths_trailcnt = %u,\n"
		"\n"
		"\t\t\t.csi_hs_lp_hs_ps = %u,\t\t/* %u us */\n"
		"\t\t},\n",
		self->speed_range, speed_range_to_str(self->speed_range),
		self->unit_clk_hz,
		self->unit_clk_mul,
		self->speed_per_lane,
		self->lane_num,
		bool_str(self->is_continuous_clk),
		self->is_continuous_clk ? "en" : "dis",
		self->lineinitcnt,
		self->lptxtimecnt,
		self->twakeupcnt,
		self->tclk_preparecnt,
		self->tclk_zerocnt,
		self->tclk_trailcnt,
		self->tclk_postcnt,
		self->ths_preparecnt,
		self->ths_zerocnt,
		self->ths_trailcnt,
		self->csi_hs_lp_hs_ps, self->csi_hs_lp_hs_ps / 1000);
}

void tc358746_dump(const struct tc358746 *self,
		   const struct tc358746_input *input)
{
	fprintf(stdout,
		"\t{\n");

	if (input)
		tc358746_input_dump(input);
	tc358746_mbus_fmt_dump(self->format);
	tc358746_pll_dump(&self->pll);
	tc358746_csi_dump(&self->csi);

	fprintf(stdout,
		"\t\t.vb_fifo = %u,\n"
		"\t},\n",
		self->vb_fifo);
}

void put_header(void)
{
	fprintf(stdout, "{\n");
}

void put_footer(void)
{
	fprintf(stdout, "};\n");
}

int try_inputs(void)
{
	bool all_ok = true;

	put_header();

	for (u32 i = 0; i < ARRAY_SIZE(inputs); i++) {
		const struct tc358746_input *input = inputs + i;
		struct tc358746 param;

		if (tc358746_calculate(&param, input) < 0) {
			all_ok = false;
			continue;
		}

		tc358746_dump(&param, input);
	}

	put_footer();

	return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

int compare_freqs(const void *a, const void *b)
{
    return (*(uint32_t *)a - *(uint32_t *)b);
}

int find_lowest_link_freq(void)
{
	bool inputs_ok[ARRAY_SIZE(inputs)];
	struct tc358746_input _inputs[ARRAY_SIZE(inputs)];
	u32 link_frequencies[ARRAY_SIZE(inputs)];
	int link_frequencies_fill = 0;
	struct tc358746 param[ARRAY_SIZE(inputs)];
	bool all_ok;
	int i;

	for (i = 0; i < ARRAY_SIZE(inputs); i++) {
		inputs_ok[i] = false;
		_inputs[i] = inputs[i];
	}

	for (u32 link_freq = 1; link_freq <= 500; link_freq++) {
		for (i = 0; i < ARRAY_SIZE(_inputs); i++) {
			if (inputs_ok[i])
				continue;

			_inputs[i].link_frequency = link_freq * 1000000;

			if (tc358746_calculate(param + i, _inputs + i) < 0)
				continue;

			link_frequencies[link_frequencies_fill++] =
						_inputs[i].link_frequency;
			inputs_ok[i] = true;
		}
	}

	put_header();
	all_ok = true;

	for (i = 0; i < ARRAY_SIZE(inputs); i++) {
		if (!inputs_ok[i]) {
			all_ok = false;
			continue;
		}

		tc358746_dump(param + i, _inputs + i);
	}

	put_footer();

	qsort(link_frequencies, link_frequencies_fill,
	      sizeof(link_frequencies[0]), compare_freqs);

	for (i = link_frequencies_fill - 1; i > 0; i--) {
		if (link_frequencies[i] == link_frequencies[i-1]) {
			int items = link_frequencies_fill - i - 1;
			memmove(link_frequencies + i, link_frequencies + i + 1,
				items * sizeof(link_frequencies[0]));
			link_frequencies_fill--;
		}
	}

	fprintf(stdout, "link-frequencies = /bits/ 64 <");
	for (i = 0; i < link_frequencies_fill; i++) {
		bool add_sep = i + 1 != link_frequencies_fill;
		fprintf(stdout, "%u%s",
			link_frequencies[i], add_sep ? " " : "");
	}
	fprintf(stdout, ">;\n");

	return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(void)
{
	/* return try_inputs(); */
	return find_lowest_link_freq();
}
