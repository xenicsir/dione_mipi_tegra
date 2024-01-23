/*
 * dione_ir.c - Dione IR sensor driver
 *
 * Copyright (c) 2021-2023, Xenics Infrared Solutions.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

#include <media/tegra_v4l2_camera.h>
#include <media/tegracam_core.h>

#include "../platform/tegra/camera/camera_gpio.h"

#include "tc358746_regs.h"
#include "tc358746_calculation.h"

#define DIONE_IR_REG_WIDTH_MAX		0x0002f028
#define DIONE_IR_REG_HEIGHT_MAX		0x0002f02c
#define DIONE_IR_REG_MODEL_NAME		0x00000044
#define DIONE_IR_REG_FIRMWARE_VERSION	0x2000e000
#define DIONE_IR_REG_ACQUISITION_STOP	0x00080104
#define DIONE_IR_REG_ACQUISITION_SRC	0x00080108
#define DIONE_IR_REG_ACQUISITION_STAT	0x0008010c

/* #define DIONE_IR_I2C_TMO_MS		5 */
/* #define DIONE_IR_STARTUP_TMO_MS		1500 */
/* #define DIONE_IR_HAS_SYSFS		1 */

#define CSI_HSTXVREGCNT			5

static int test_mode = 0;
static int quick_mode = 1;
module_param(test_mode, int, 0644);
module_param(quick_mode, int, 0644);

enum {
	DIONE_IR_MODE_640x480_60FPS,
	DIONE_IR_MODE_1280x1024_60FPS,
	DIONE_IR_MODE_320x240_60FPS,
	DIONE_IR_MODE_1024x768_60FPS,
};

static const int dione_ir_60fps[] = {
	60,
};

/*
 * WARNING: frmfmt ordering need to match mode definition in
 * device tree!
 */
static const struct camera_common_frmfmt dione_ir_frmfmt[] = {
	{{640, 480},	dione_ir_60fps, 1, 0, DIONE_IR_MODE_640x480_60FPS},
	{{1280, 1024},	dione_ir_60fps, 1, 0, DIONE_IR_MODE_1280x1024_60FPS},
	{{320, 240},	dione_ir_60fps, 1, 0, DIONE_IR_MODE_320x240_60FPS},
	{{1024, 768},	dione_ir_60fps, 1, 0, DIONE_IR_MODE_1024x768_60FPS},
	/* Add modes with no device tree support after below */
};

static int dione_ir_find_frmfmt(u32 width, u32 height)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(dione_ir_frmfmt); i++) {
		const struct camera_common_frmfmt *fmt = dione_ir_frmfmt + i;

		if (fmt->size.width == width && fmt->size.height == height)
			return i;
	}

	return -1;
}

static const struct regmap_range ctl_regmap_rw_ranges[] = {
	regmap_reg_range(0x0000, 0x00ff),
};

static const struct regmap_access_table ctl_regmap_access = {
	.yes_ranges = ctl_regmap_rw_ranges,
	.n_yes_ranges = ARRAY_SIZE(ctl_regmap_rw_ranges),
};

static const struct regmap_config ctl_regmap_config = {
	.reg_bits = 16,
	.reg_stride = 2,
	.val_bits = 16,
	.cache_type = REGCACHE_NONE,
	.max_register = 0x00ff,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.rd_table = &ctl_regmap_access,
	.wr_table = &ctl_regmap_access,
	.name = "tc358746-ctl",
};

static const struct regmap_range tx_regmap_rw_ranges[] = {
	regmap_reg_range(0x0100, 0x05ff),
};

static const struct regmap_access_table tx_regmap_access = {
	.yes_ranges = tx_regmap_rw_ranges,
	.n_yes_ranges = ARRAY_SIZE(tx_regmap_rw_ranges),
};

static const struct regmap_config tx_regmap_config = {
	.reg_bits = 16,
	.reg_stride = 4,
	.val_bits = 32,
	.cache_type = REGCACHE_NONE,
	.max_register = 0x05ff,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG_LITTLE,
	.rd_table = &tx_regmap_access,
	.wr_table = &tx_regmap_access,
	.name = "tc358746-tx",
};

static const struct of_device_id dione_ir_of_match[] = {
	{ .compatible = "xenics,dioneir", },
	{ },
};
MODULE_DEVICE_TABLE(of, dione_ir_of_match);

static const u32 ctrl_cid_list[] = {
	TEGRA_CAMERA_CID_GAIN,
	TEGRA_CAMERA_CID_EXPOSURE,
	TEGRA_CAMERA_CID_FRAME_RATE,
	TEGRA_CAMERA_CID_SENSOR_MODE_ID,
};

struct dione_ir {
	struct i2c_client		*tc35_client;
	struct i2c_client		*fpga_client;
	struct v4l2_subdev		*subdev;
	struct regmap			*tx_regmap;
	struct camera_common_data	*s_data;
	struct tegracam_device		*tc_dev;

	int				quick_mode;
#ifdef DIONE_IR_STARTUP_TMO_MS
	ktime_t				start_up;
#endif
	bool				tc35_found;
	bool				fpga_found;
	bool				reva;
	int				mode;

	u32				*fpga_address;
	unsigned int			fpga_address_num;

	u64				*link_frequencies;
	unsigned int			link_frequencies_num;
};

static int dione_ir_i2c_read(struct i2c_client *client, u32 addr, u8 *buf, u16 len);
static int dione_ir_i2c_write32(struct i2c_client *client, u32 addr, u32 val);

static inline int dione_ir_read_reg(struct camera_common_data *s_data,
	u16 addr, u8 *val)
{
	int err = 0;
	u32 reg_val = 0;

	err = regmap_read(s_data->regmap, addr, &reg_val);
	*val = reg_val & 0xff;

	return err;
}

static inline int dione_ir_write_reg(struct camera_common_data *s_data,
	u16 addr, u8 val)
{
	int err = 0;

	err = regmap_write(s_data->regmap, addr, val);
	if (err)
		dev_err(s_data->dev, "%s: i2c write failed %#x = %#x\n",
			__func__, addr, val);

	return err;
}

static int dione_ir_set_group_hold(struct tegracam_device *tc_dev, bool val)
{
	dev_dbg(tc_dev->dev, "%s val=%d\n", __func__, val);
	return 0;
}

static int dione_ir_set_gain(struct tegracam_device *tc_dev, s64 val)
{
	dev_dbg(tc_dev->dev, "%s val=%lld\n", __func__, val);
	return 0;
}

static int dione_ir_set_frame_rate(struct tegracam_device *tc_dev, s64 val)
{
	dev_dbg(tc_dev->dev, "%s val=%lld\n", __func__, val);
	return 0;
}

static int dione_ir_set_exposure(struct tegracam_device *tc_dev, s64 val)
{
	dev_dbg(tc_dev->dev, "%s val=%lld\n", __func__, val);
	return 0;
}

static struct tegracam_ctrl_ops dione_ir_ctrl_ops = {
	.numctrls = ARRAY_SIZE(ctrl_cid_list),
	.ctrl_cid_list = ctrl_cid_list,
	.set_gain = dione_ir_set_gain,
	.set_exposure = dione_ir_set_exposure,
	.set_frame_rate = dione_ir_set_frame_rate,
	.set_group_hold = dione_ir_set_group_hold,
};

static int dione_ir_power_on(struct camera_common_data *s_data)
{
	int err = 0;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	struct dione_ir *priv = (struct dione_ir *)s_data->priv;
	bool reset = !priv->reva;

	dev_dbg(dev, "%s: power on\n", __func__);
	if (pdata && pdata->power_on) {
		err = pdata->power_on(pw);
		if (err)
			dev_err(dev, "%s failed.\n", __func__);
		else
			pw->state = SWITCH_ON;
		return err;
	}

	if (!priv->quick_mode) {
		if (pw->reset_gpio) {
			if (gpio_cansleep(pw->reset_gpio))
				gpio_set_value_cansleep(pw->reset_gpio, reset);
			else
				gpio_set_value(pw->reset_gpio, reset);
		}

		if (unlikely(!(pw->avdd || pw->iovdd || pw->dvdd)))
			goto skip_power_seqn;

		usleep_range(10, 20);

		if (pw->avdd) {
			err = regulator_enable(pw->avdd);
			if (err)
				goto dione_ir_avdd_fail;
		}

		if (pw->iovdd) {
			err = regulator_enable(pw->iovdd);
			if (err)
				goto dione_ir_iovdd_fail;
		}

		if (pw->dvdd) {
			err = regulator_enable(pw->dvdd);
			if (err)
				goto dione_ir_dvdd_fail;
		}

		usleep_range(10, 20);

skip_power_seqn:
		if (pw->reset_gpio) {
			if (gpio_cansleep(pw->reset_gpio))
				gpio_set_value_cansleep(pw->reset_gpio, !reset);
			else
				gpio_set_value(pw->reset_gpio, !reset);
		}

		usleep_range(23000, 23100);
		msleep(200);
	}

	pw->state = SWITCH_ON;

	return 0;

dione_ir_dvdd_fail:
	regulator_disable(pw->iovdd);

dione_ir_iovdd_fail:
	regulator_disable(pw->avdd);

dione_ir_avdd_fail:
	dev_err(dev, "%s failed: %d\n", __func__, err);

	return err;
}

static int dione_ir_power_off(struct camera_common_data *s_data)
{
	int err = 0;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	struct dione_ir *priv = (struct dione_ir *)s_data->priv;
	bool reset = !priv->reva;

	dev_dbg(dev, "%s: power off\n", __func__);

	if (pdata && pdata->power_off) {
		err = pdata->power_off(pw);
		if (err) {
			dev_err(dev, "%s failed\n", __func__);
			return err;
		}
	} else {
		if (!priv->quick_mode) {
			if (pw->reset_gpio) {
				if (gpio_cansleep(pw->reset_gpio))
					gpio_set_value_cansleep(pw->reset_gpio, reset);
				else
					gpio_set_value(pw->reset_gpio, reset);
			}

			usleep_range(10, 10);

			if (pw->dvdd)
				regulator_disable(pw->dvdd);
			if (pw->iovdd)
				regulator_disable(pw->iovdd);
			if (pw->avdd)
				regulator_disable(pw->avdd);
		}
	}

	pw->state = SWITCH_OFF;

	return 0;
}

static int dione_ir_power_put(struct tegracam_device *tc_dev)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;
	struct dione_ir *priv = (struct dione_ir *)tegracam_get_privdata(tc_dev);

	if (unlikely(!pw))
		return -EFAULT;

	/* really power off module when removing the driver */
	priv->quick_mode = 0;

	s_data->ops->power_off(s_data);

	if (likely(pw->dvdd))
		devm_regulator_put(pw->dvdd);

	if (likely(pw->avdd))
		devm_regulator_put(pw->avdd);

	if (likely(pw->iovdd))
		devm_regulator_put(pw->iovdd);

	pw->dvdd = NULL;
	pw->avdd = NULL;
	pw->iovdd = NULL;

	if (likely(pw->reset_gpio))
		gpio_free(pw->reset_gpio);

	if (priv->fpga_client != NULL) {
		i2c_unregister_device(priv->fpga_client);
		priv->fpga_client = NULL;
	}

	return 0;
}

static int dione_ir_power_get(struct tegracam_device *tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct clk *parent;
	int err = 0;

	if (!pdata) {
		dev_err(dev, "pdata missing\n");
		return -EFAULT;
	}

	/* Sensor MCLK (aka. INCK) */
	if (pdata->mclk_name) {
		pw->mclk = devm_clk_get(dev, pdata->mclk_name);
		if (IS_ERR(pw->mclk)) {
			dev_err(dev, "unable to get clock %s\n",
				pdata->mclk_name);
			return PTR_ERR(pw->mclk);
		}

		if (pdata->parentclk_name) {
			parent = devm_clk_get(dev, pdata->parentclk_name);
			if (IS_ERR(parent)) {
				dev_err(dev, "unable to get parent clock %s\n",
					pdata->parentclk_name);
			} else
				clk_set_parent(pw->mclk, parent);
		}
	}

	/* analog 2.8v */
	if (pdata->regulators.avdd)
		err |= camera_common_regulator_get(dev,
				&pw->avdd, pdata->regulators.avdd);
	/* IO 1.8v */
	if (pdata->regulators.iovdd)
		err |= camera_common_regulator_get(dev,
				&pw->iovdd, pdata->regulators.iovdd);
	/* dig 1.2v */
	if (pdata->regulators.dvdd)
		err |= camera_common_regulator_get(dev,
				&pw->dvdd, pdata->regulators.dvdd);
	if (err) {
		dev_err(dev, "%s: unable to get regulator(s)\n", __func__);
		goto done;
	}

	/* Reset or ENABLE GPIO */
	pw->reset_gpio = pdata->reset_gpio;
	err = gpio_request(pw->reset_gpio, "cam_reset_gpio");
	if (err < 0) {
		dev_err(dev, "%s: unable to request reset_gpio (%d)\n",
			__func__, err);
		goto done;
	}

done:
	pw->state = SWITCH_OFF;

	return err;
}

static struct camera_common_pdata *dione_ir_parse_dt(
		struct tegracam_device *tc_dev)
{
	struct device *dev = tc_dev->dev;
	struct device_node *np = dev->of_node;
	struct camera_common_pdata *board_priv_pdata;
	const struct of_device_id *match;
	struct camera_common_pdata *ret = NULL;
	int err = 0;
	int gpio;

	if (!np)
		return NULL;

	match = of_match_device(dione_ir_of_match, dev);
	if (!match) {
		dev_err(dev, "Failed to find matching dt id\n");
		return NULL;
	}

	board_priv_pdata = devm_kzalloc(dev, sizeof(*board_priv_pdata),
					GFP_KERNEL);
	if (!board_priv_pdata)
		return NULL;

	gpio = of_get_named_gpio(np, "reset-gpios", 0);
	if (gpio < 0) {
		if (gpio == -EPROBE_DEFER)
			ret = ERR_PTR(-EPROBE_DEFER);
		dev_err(dev, "reset-gpios not found\n");
		goto error;
	}
	board_priv_pdata->reset_gpio = (unsigned int)gpio;

	err = of_property_read_string(np, "mclk", &board_priv_pdata->mclk_name);
	if (err)
		dev_dbg(dev, "mclk name not present, "
			"assume sensor driven externally\n");

	err = of_property_read_string(np, "avdd-reg",
		&board_priv_pdata->regulators.avdd);
	err |= of_property_read_string(np, "iovdd-reg",
		&board_priv_pdata->regulators.iovdd);
	err |= of_property_read_string(np, "dvdd-reg",
		&board_priv_pdata->regulators.dvdd);
	if (err)
		dev_dbg(dev, "avdd, iovdd and/or dvdd reglrs. not present, "
			"assume sensor powered independently\n");

	board_priv_pdata->has_eeprom =
		of_property_read_bool(np, "has-eeprom");

	return board_priv_pdata;

error:
	devm_kfree(dev, board_priv_pdata);

	return ret;
}

static inline int tc358746_sleep_mode(struct regmap *regmap, int enable)
{
	return regmap_update_bits(regmap, SYSCTL, SYSCTL_SLEEP_MASK,
				  enable ? SYSCTL_SLEEP_MASK : 0);
}

static inline int tc358746_sreset(struct regmap *regmap)
{
	int err;

	err = regmap_write(regmap, SYSCTL, SYSCTL_SRESET_MASK);

	udelay(10);

	if (!err)
		err = regmap_write(regmap, SYSCTL, 0);

	return err;
}

static int tc358746_set_pll(struct regmap *regmap,
			    const struct tc358746_pll *pll,
			    const struct tc358746_csi *csi)
{
	u32 pllctl0, pllctl1, pllctl0_new;
	int err;

	err = regmap_read(regmap, PLLCTL0, &pllctl0);
	if (!err)
		err = regmap_read(regmap, PLLCTL1, &pllctl1);

	if (err)
		return err;

	pllctl0_new = PLLCTL0_PLL_PRD_SET(pll->pll_prd) |
	  PLLCTL0_PLL_FBD_SET(pll->pll_fbd);

	/*
	 * Only rewrite when needed (new value or disabled), since rewriting
	 * triggers another format change event.
	 */
	if (pllctl0 != pllctl0_new || (pllctl1 & PLLCTL1_PLL_EN_MASK) == 0) {
		u16 pllctl1_mask = PLLCTL1_PLL_FRS_MASK | PLLCTL1_RESETB_MASK |
				   PLLCTL1_PLL_EN_MASK;
		u16 pllctl1_val = PLLCTL1_PLL_FRS_SET(csi->speed_range) |
				  PLLCTL1_RESETB_MASK | PLLCTL1_PLL_EN_MASK;

		err = regmap_write(regmap, PLLCTL0, pllctl0_new);
		if (!err)
			err = regmap_update_bits(regmap, PLLCTL1,
						 pllctl1_mask, pllctl1_val);

		udelay(1000);

		if (!err)
			err = regmap_update_bits(regmap, PLLCTL1,
						 PLLCTL1_CKEN_MASK,
						 PLLCTL1_CKEN_MASK);
	}

	return err;
}

static int tc358746_set_csi_color_space(struct regmap *regmap,
					const struct tc358746_mbus_fmt *format)
{
	int err;

	err = regmap_update_bits(regmap, DATAFMT,
				 (DATAFMT_PDFMT_MASK | DATAFMT_UDT_EN_MASK),
				 DATAFMT_PDFMT_SET(format->pdformat));

	if (!err)
		err = regmap_update_bits(regmap, CONFCTL, CONFCTL_PDATAF_MASK,
					 CONFCTL_PDATAF_SET(format->pdataf));

	return err;
}

static int tc358746_set_buffers(struct regmap *regmap,
				u32 width, u8 bpp, u16 vb_fifo)
{
	unsigned int byte_per_line = (width * bpp) / 8;
	int err;

	err = regmap_write(regmap, FIFOCTL, vb_fifo);

	if (!err)
		err = regmap_write(regmap, WORDCNT, byte_per_line);

	return err;
}

static int tc358746_enable_csi_lanes(struct regmap *regmap,
				     int lane_num, int enable)
{
	u32 val = 0;
	int err = 0;

	if (lane_num < 1 || !enable) {
		if (!err)
			err = regmap_write(regmap, CLW_CNTRL,
					   CLW_CNTRL_CLW_LANEDISABLE_MASK);
		if (!err)
			err = regmap_write(regmap, D0W_CNTRL,
					   D0W_CNTRL_D0W_LANEDISABLE_MASK);
	}

	if (lane_num < 2 || !enable) {
		if (!err)
			err = regmap_write(regmap, D1W_CNTRL,
					   D1W_CNTRL_D1W_LANEDISABLE_MASK);
	}

	if (lane_num < 3 || !enable) {
		if (!err)
			err = regmap_write(regmap, D2W_CNTRL,
					   D2W_CNTRL_D2W_LANEDISABLE_MASK);
	}

	if (lane_num < 4 || !enable) {
		if (!err)
			err = regmap_write(regmap, D3W_CNTRL,
					   D2W_CNTRL_D3W_LANEDISABLE_MASK);
	}

	if (lane_num > 0 && enable) {
		val |= HSTXVREGEN_CLM_HSTXVREGEN_MASK |
			HSTXVREGEN_D0M_HSTXVREGEN_MASK;
	}

	if (lane_num > 1 && enable)
		val |= HSTXVREGEN_D1M_HSTXVREGEN_MASK;

	if (lane_num > 2 && enable)
		val |= HSTXVREGEN_D2M_HSTXVREGEN_MASK;

	if (lane_num > 3 && enable)
		val |= HSTXVREGEN_D3M_HSTXVREGEN_MASK;

	if (!err)
		err = regmap_write(regmap, HSTXVREGEN, val);

	return err;
}

static int tc358746_set_csi(struct regmap *regmap,
			    const struct tc358746_csi *csi)
{
	u32 val;
	int err;

	val = TCLK_HEADERCNT_TCLK_ZEROCNT_SET(csi->tclk_zerocnt) |
	      TCLK_HEADERCNT_TCLK_PREPARECNT_SET(csi->tclk_preparecnt);
	err = regmap_write(regmap, TCLK_HEADERCNT, val);

	val = THS_HEADERCNT_THS_ZEROCNT_SET(csi->ths_zerocnt) |
	      THS_HEADERCNT_THS_PREPARECNT_SET(csi->ths_preparecnt);
	if (!err)
		err = regmap_write(regmap, THS_HEADERCNT, val);

	if (!err)
		err = regmap_write(regmap, TWAKEUP, csi->twakeupcnt);

	if (!err)
		err = regmap_write(regmap, TCLK_POSTCNT, csi->tclk_postcnt);

	if (!err)
		err = regmap_write(regmap, THS_TRAILCNT, csi->ths_trailcnt);

	if (!err)
		err = regmap_write(regmap, LINEINITCNT, csi->lineinitcnt);

	if (!err)
		err = regmap_write(regmap, LPTXTIMECNT, csi->lptxtimecnt);

	if (!err)
		err = regmap_write(regmap, TCLK_TRAILCNT, csi->tclk_trailcnt);

	if (!err)
		err = regmap_write(regmap, HSTXVREGCNT, CSI_HSTXVREGCNT);

	val = csi->is_continuous_clk ? TXOPTIONCNTRL_CONTCLKMODE_MASK : 0;
	if (!err)
		err = regmap_write(regmap, TXOPTIONCNTRL, val);

	return err;
}

static int tc358746_wr_csi_control(struct regmap *regmap, u32 val)
{
	val &= CSI_CONFW_DATA_MASK;
	val |= CSI_CONFW_MODE_SET_MASK | CSI_CONFW_ADDRESS_CSI_CONTROL_MASK;

	return regmap_write(regmap, CSI_CONFW, val);
}

static int tc358746_enable_csi_module(struct regmap *regmap, int lane_num)
{
	u32 val;
	int err;

	err = regmap_write(regmap, STARTCNTRL, STARTCNTRL_START_MASK);

	if (!err)
		err = regmap_write(regmap, CSI_START, CSI_START_STRT_MASK);

	val = CSI_CONTROL_NOL_1_MASK;
	if (lane_num == 2)
		val = CSI_CONTROL_NOL_2_MASK;
	else if (lane_num == 3)
		val = CSI_CONTROL_NOL_3_MASK;
	else if (lane_num == 4)
		val = CSI_CONTROL_NOL_4_MASK;

	val |= CSI_CONTROL_CSI_MODE_MASK | CSI_CONTROL_TXHSMD_MASK |
		CSI_CONTROL_EOTDIS_MASK; /* add, according to Excel */

	if (!err)
		err = tc358746_wr_csi_control(regmap, val);

	return err;
}

static int dione_ir_set_mode(struct tegracam_device *tc_dev)
{
	struct dione_ir *priv = (struct dione_ir *)tegracam_get_privdata(tc_dev);
	struct camera_common_data *s_data = priv->s_data;
	struct regmap *ctl_regmap = s_data->regmap;
	struct regmap *tx_regmap = priv->tx_regmap;
	const struct sensor_mode_properties *sensor_mode;
	const struct camera_common_colorfmt *colorfmt;
	struct tc358746_input input;
	struct tc358746 params;
	int i, err;

	if (s_data->mode != priv->mode)
		return -EINVAL;

	sensor_mode = s_data->sensor_props.sensor_modes + s_data->mode_prop_idx;
	colorfmt = camera_common_find_pixelfmt(sensor_mode->image_properties.pixel_format);

	if (!colorfmt) {
		dev_err(tc_dev->dev, "unsupported pixelformat\n");
		return -EINVAL;
	}

	if (s_data->def_clk_freq != sensor_mode->signal_properties.mclk_freq * 1000) {
		dev_err(tc_dev->dev, "mclk_freq must be the same in every mode\n");
		return -EINVAL;
	}

	input.mbus_fmt = colorfmt->code;
	input.refclk = s_data->def_clk_freq;
	input.num_lanes = sensor_mode->signal_properties.num_lanes;
	input.discontinuous_clk = sensor_mode->signal_properties.discontinuous_clk;
	input.pclk = sensor_mode->signal_properties.pixel_clock.val;
	input.width = sensor_mode->image_properties.width;
	input.hblank = sensor_mode->image_properties.line_length - input.width;

	for (i = 0; i < priv->link_frequencies_num; i++) {
		input.link_frequency = priv->link_frequencies[i];
		if (tc358746_calculate(&params, &input) == 0)
			break;
	}

	if (i >= priv->link_frequencies_num) {
		dev_err(tc_dev->dev, "could not calculate parameters for tc358746\n");
		return -EINVAL;
	}

	err = 0;
	if (test_mode) {
#ifdef DIONE_IR_STARTUP_TMO_MS
		/* wait until FPGA in sensor finishes booting up */
		while (ktime_ms_delta(ktime_get(), priv->start_up)
				< DIONE_IR_STARTUP_TMO_MS)
			msleep(100);
#endif
		/* enable test pattern in the sensor module */
		err = dione_ir_i2c_write32(priv->fpga_client,
					   DIONE_IR_REG_ACQUISITION_STOP, 2);
		if (!err) {
			msleep(300);
			err = dione_ir_i2c_write32(priv->fpga_client,
						   DIONE_IR_REG_ACQUISITION_SRC, 0);
		}

		if (!err) {
			msleep(300);
			err = dione_ir_i2c_write32(priv->fpga_client,
						   DIONE_IR_REG_ACQUISITION_STOP, 1);
		}
	}

	regmap_write(ctl_regmap, DBG_ACT_LINE_CNT, 0);

	if (!err)
		err = tc358746_sreset(ctl_regmap);
	if (err) {
		dev_err(tc_dev->dev, "Failed to reset chip\n");
		return err;
	}

	err = tc358746_set_pll(ctl_regmap, &params.pll, &params.csi);
	if (err) {
		dev_err(tc_dev->dev, "Failed to setup PLL\n");
		return err;
	}

	err = tc358746_set_csi_color_space(ctl_regmap, params.format);

	if (!err)
		err = tc358746_set_buffers(ctl_regmap, input.width,
				           params.format->bpp, params.vb_fifo);

	if (!err)
		err = tc358746_enable_csi_lanes(tx_regmap,
						params.csi.lane_num, true);

	if (!err)
		err = tc358746_set_csi(tx_regmap, &params.csi);

	if (!err)
		err = tc358746_enable_csi_module(tx_regmap,
						 params.csi.lane_num);

	if (err)
		dev_err(tc_dev->dev, "%s return code (%d)\n", __func__, err);

	return err;
}

static int dione_ir_start_streaming(struct tegracam_device *tc_dev)
{
	struct dione_ir *priv = (struct dione_ir *)tegracam_get_privdata(tc_dev);
	struct camera_common_data *s_data = priv->s_data;
	struct regmap *ctl_regmap = s_data->regmap;
	int err;

	err = regmap_write(ctl_regmap, PP_MISC, 0);
	if (!err)
		err = regmap_update_bits(ctl_regmap, CONFCTL,
					 CONFCTL_PPEN_MASK, CONFCTL_PPEN_MASK);

	if (err)
		dev_err(tc_dev->dev, "%s return code (%d)\n", __func__, err);

	return err;
}

static int dione_ir_stop_streaming(struct tegracam_device *tc_dev)
{
	struct dione_ir *priv = (struct dione_ir *)tegracam_get_privdata(tc_dev);
	struct camera_common_data *s_data = priv->s_data;
	struct regmap *ctl_regmap = s_data->regmap;
	struct regmap *tx_regmap = priv->tx_regmap;
	int err;

	err = regmap_update_bits(ctl_regmap, PP_MISC, PP_MISC_FRMSTOP_MASK,
				 PP_MISC_FRMSTOP_MASK);
	if (!err)
		err = regmap_update_bits(ctl_regmap, CONFCTL,
					 CONFCTL_PPEN_MASK, 0);

	if (!err)
		err = regmap_update_bits(ctl_regmap, PP_MISC,
					 PP_MISC_RSTPTR_MASK,
					 PP_MISC_RSTPTR_MASK);

	if (!err)
		err = regmap_write(tx_regmap, CSIRESET,
				   (CSIRESET_RESET_CNF_MASK |
				    CSIRESET_RESET_MODULE_MASK));
	if (!err)
		err = regmap_write(ctl_regmap, DBG_ACT_LINE_CNT, 0);

	if (err)
		dev_err(tc_dev->dev, "%s return code (%d)\n", __func__, err);

	return err;
}

static struct camera_common_sensor_ops dione_ir_ops = {
	.numfrmfmts = ARRAY_SIZE(dione_ir_frmfmt),
	.frmfmt_table = dione_ir_frmfmt,
	.power_on = dione_ir_power_on,
	.power_off = dione_ir_power_off,
	.write_reg = dione_ir_write_reg,
	.read_reg = dione_ir_read_reg,
	.parse_dt = dione_ir_parse_dt,
	.power_get = dione_ir_power_get,
	.power_put = dione_ir_power_put,
	.set_mode = dione_ir_set_mode,
	.start_streaming = dione_ir_start_streaming,
	.stop_streaming = dione_ir_stop_streaming,
};

#ifdef DIONE_IR_I2C_TMO_MS
static inline int i2c_transfer_one(struct i2c_client *client,
				   void *buf, size_t len, u16 flags)
{
	struct i2c_msg msgs;

	msgs.addr = client->addr;
	msgs.flags = flags;
	msgs.len = len;
	msgs.buf = buf;

	return i2c_transfer(client->adapter, &msgs, 1);
}

static int dione_ir_i2c_read(struct i2c_client *client, u32 reg, u8 *dst, u16 len)
{
	u8 tx_data[6];
	u8 rx_data[72];
	int ret = 0, tmo, retry;

	if (len > sizeof(rx_data) - 2)
		ret = -EINVAL;

	if (!ret) {
		*(u32 *)tx_data = cpu_to_le32(reg);
		*(u16 *)(tx_data + 4) = cpu_to_le16(len);

		retry = 4;
		tmo = DIONE_IR_I2C_TMO_MS;
		ret = -EIO;

		while (retry-- > 0) {
			if (i2c_transfer_one(client, tx_data, 6, 0) == 1) {
				ret = 0;
				break;
			}
			msleep(tmo);
			tmo <<= 2;
		}
	}

	if (!ret) {
		retry = 4;
		tmo = DIONE_IR_I2C_TMO_MS;
		ret = -EIO;

		msleep(2);
		while (retry-- > 0) {
			if (i2c_transfer_one(client,
					     rx_data, len + 2, I2C_M_RD) == 1) {
				ret = 0;
				break;
			}
			msleep(tmo);
			tmo <<= 2;
		}
	}

	if (!ret) {
		if (rx_data[0] != 0 || rx_data[1] != 0) {
			ret = -EINVAL;
		} else {
			switch (len) {
			case 1:
				dst[0] = rx_data[2];
				break;
			case 2:
				*(u16 *)dst = le16_to_cpu(*(u16 *)(rx_data + 2));
				break;
			case 4:
				*(u32 *)dst = le32_to_cpu(*(u32 *)(rx_data + 2));
				break;
			default:
				memcpy(dst, rx_data + 2, len);
			}
		}
	}

	return ret;
}

static int dione_ir_i2c_write32(struct i2c_client *client, u32 reg, u32 val)
{
	int ret = -EIO, retry = 4, tmo = DIONE_IR_I2C_TMO_MS;
	u8 tx_data[10];

	*(u32 *)tx_data = cpu_to_le32(reg);
	*(u16 *)(tx_data + 4) = cpu_to_le16(4);
	*(u32 *)(tx_data + 6) = cpu_to_le32(val);

	while (retry-- > 0) {
		if (i2c_transfer_one(client, tx_data, sizeof(tx_data), 0) == 1) {
			ret = 0;
			break;
		}
		msleep(tmo);
		tmo <<= 2;
	}

	return ret;
}
#else
static int dione_ir_i2c_read(struct i2c_client *client, u32 reg, u8 *dst, u16 len)
{
	struct i2c_msg msgs[2];
	u8 tx_data[6];
	u8 rx_data[72];
	int ret = 0;

	if (len > sizeof(rx_data) - 2)
		ret = -EINVAL;

	if (!ret) {
		*(u32 *)tx_data = cpu_to_le32(reg);
		*(u16 *)(tx_data + 4) = cpu_to_le16(len);

		msgs[0].addr = client->addr;
		msgs[0].flags = 0;
		msgs[0].len = sizeof(tx_data);
		msgs[0].buf = tx_data;

		msgs[1].addr = client->addr;
		msgs[1].flags = I2C_M_RD;
		msgs[1].len = len + 2;
		msgs[1].buf = rx_data;

		if (i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs)) != 2)
			ret = -EIO;
	}

	if (!ret) {
		if (rx_data[0] != 0 || rx_data[1] != 0) {
			ret = -EINVAL;
		} else {
			switch (len) {
			case 1:
				dst[0] = rx_data[2];
				break;
			case 2:
				*(u16 *)dst = le16_to_cpu(*(u16 *)(rx_data + 2));
				break;
			case 4:
				*(u32 *)dst = le32_to_cpu(*(u32 *)(rx_data + 2));
				break;
			default:
				memcpy(dst, rx_data + 2, len);
			}
		}
	}

	return ret;
}

static int dione_ir_i2c_write32(struct i2c_client *client, u32 reg, u32 val)
{
	struct i2c_msg msgs;
	u8 tx_data[10];

	*(u32 *)tx_data = cpu_to_le32(reg);
	*(u16 *)(tx_data + 4) = cpu_to_le16(4);
	*(u32 *)(tx_data + 6) = cpu_to_le32(val);

	msgs.addr = client->addr;
	msgs.flags = 0;
	msgs.len = sizeof(tx_data);
	msgs.buf = tx_data;

	if (i2c_transfer(client->adapter, &msgs, 1) != 1)
		return -EIO;

	return 0;
}
#endif

static int detect_dione_ir(struct dione_ir *priv, u32 fpga_addr)
{
	struct device *dev = priv->s_data->dev;
	u32 width, height;
	u8 buf[64];
	int i, mode, ret;

	msleep(200);

	dev_dbg(dev, "probing fpga at address %#02x%s\n",
		fpga_addr, priv->reva ? " reva" : "");

	priv->fpga_client = i2c_new_dummy(priv->tc35_client->adapter, fpga_addr);
	if (!priv->fpga_client)
		return -ENOMEM;

	ret = dione_ir_i2c_read(priv->fpga_client, DIONE_IR_REG_WIDTH_MAX,
				(u8 *)&width, sizeof(width));
	if (ret < 0)
		goto error;

	ret = dione_ir_i2c_read(priv->fpga_client, DIONE_IR_REG_HEIGHT_MAX,
				(u8 *)&height, sizeof(height));
	if (ret < 0)
		goto error;

	mode = dione_ir_find_frmfmt(width, height);
	if (mode < 0) {
		ret = -ENODEV;
		goto error;
	}

	priv->fpga_found = true;

	ret = dione_ir_i2c_read(priv->fpga_client, DIONE_IR_REG_FIRMWARE_VERSION,
				buf, sizeof(buf));
	if (ret < 0)
		goto error;

	for (i = sizeof(buf) - 1; i >= 0 && buf[i] == 0xff; i--)
		buf[i] = '\0';

	dev_info(dev, "dione-ir %ux%u at address %#02x, firmware: %s\n",
		 width, height, fpga_addr, buf);

	return mode;

error:
	if (priv->fpga_client != NULL) {
		i2c_unregister_device(priv->fpga_client);
		priv->fpga_client = NULL;
	}

	return ret;
}

static int dione_ir_board_setup(struct dione_ir *priv)
{
	struct camera_common_data *s_data = priv->s_data;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	struct regmap *ctl_regmap = s_data->regmap;
	u32 reg_val;
	int i, _quick_mode, err = 0;

	if (pdata->mclk_name) {
		err = camera_common_mclk_enable(s_data);
		if (err) {
			dev_err(dev, "error turning on mclk (%d)\n", err);
			goto done;
		}
	}

	_quick_mode = priv->quick_mode;
	priv->quick_mode = 0;
	err = s_data->ops->power_on(s_data);
	priv->quick_mode = _quick_mode;

#ifdef DIONE_IR_STARTUP_TMO_MS
	priv->start_up = ktime_get();
#endif
	/* Probe sensor model id registers */
	err = regmap_read(ctl_regmap, CHIPID, &reg_val);
	if (err)
		goto err_reg_probe;

	if ((reg_val & CHIPID_CHIPID_MASK) != 0x4400) {
		dev_err(dev, "%s: invalid tc35 chip-id: %#x\n",
			__func__, reg_val);
		err = -ENODEV;
		goto err_reg_probe;
	}

	priv->tc35_found = true;

	if (err) {
		dev_err(dev, "error during power on sensor (%d)\n", err);
		goto err_power_on;
	}

	for (i = 0; i < priv->fpga_address_num; i++) {
		u32 fpga_addr = priv->fpga_address[i];
		err = detect_dione_ir(priv, fpga_addr);
		if (err >= 0) {
			priv->mode = err;
			err = 0;
			break;
		}
	}

	if (err < 0)
		goto err_reg_probe;

err_reg_probe:
	s_data->ops->power_off(s_data);

err_power_on:
	if (pdata->mclk_name)
		camera_common_mclk_disable(s_data);

done:
	return err;
}

static int dione_ir_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	dev_dbg(&client->dev, "%s:\n", __func__);
	return 0;
}

static const struct v4l2_subdev_internal_ops dione_ir_subdev_internal_ops = {
	.open = dione_ir_open,
};

static struct tegracam_device *dione_ir_probe_sensor(struct dione_ir *priv)
{
	struct tegracam_device *tc_dev;
	struct device *dev = &priv->tc35_client->dev;
	int err;

	tc_dev = devm_kzalloc(dev, sizeof(struct tegracam_device), GFP_KERNEL);
	if (!tc_dev)
		return NULL;

	priv->quick_mode = quick_mode;
	tc_dev->client = priv->tc35_client;
	tc_dev->dev = dev;
	strncpy(tc_dev->name, "dioneir", sizeof(tc_dev->name));
	tc_dev->dev_regmap_config = &ctl_regmap_config;
	tc_dev->sensor_ops = &dione_ir_ops;
	tc_dev->v4l2sd_internal_ops = &dione_ir_subdev_internal_ops;
	tc_dev->tcctrl_ops = &dione_ir_ctrl_ops;

	err = tegracam_device_register(tc_dev);
	if (err) {
		devm_kfree(tc_dev->dev, tc_dev);
		tc_dev = NULL;
		dev_err(dev, "tegra camera driver registration failed\n");
	}

	if (!err) {
		priv->tc_dev = tc_dev;
		priv->s_data = tc_dev->s_data;
		priv->subdev = &tc_dev->s_data->subdev;
		tegracam_set_privdata(tc_dev, (void *)priv);

		priv->tx_regmap = devm_regmap_init_i2c(priv->tc35_client,
						       &tx_regmap_config);
		if (IS_ERR(priv->tx_regmap)) {
			dev_err(dev, "tx_regmap init failed: %ld\n",
					PTR_ERR(priv->tx_regmap));
			err = -ENODEV;
		}
	}

	if (!err) {
		err = dione_ir_board_setup(priv);
		if (err && !priv->tc35_found && !priv->fpga_found) {
			priv->reva = true;
			err = dione_ir_board_setup(priv);
		}

		if (err && priv->tc35_found && priv->fpga_found)
			dev_err(dev, "dione_ir_board_setup error: %d\n", err);
	}

	if (err) {
		if (tc_dev)
			tegracam_device_unregister(tc_dev);
		tc_dev = NULL;
	}

	if (!test_mode && priv->fpga_client != NULL) {
		i2c_unregister_device(priv->fpga_client);
		priv->fpga_client = NULL;
	}

	return tc_dev;
}

#ifdef DIONE_IR_HAS_SYSFS
/**
 * sysfs interface function handling ".../restart_mipi"
 */

static struct dione_ir *dione_ir_priv = NULL;

static ssize_t dione_ir_sysfs_restart_mipi(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   const char *buf, size_t count)
{
	struct camera_common_data *s_data;
	int cmd;

	if ((dione_ir_priv != NULL) && (sscanf(buf, "%x", &cmd) == 1) && (cmd == 1)) {
		s_data = dione_ir_priv->s_data;
		dione_ir_stop_streaming(dione_ir_priv->tc_dev);
		msleep(1000);
		dione_ir_set_mode(dione_ir_priv->tc_dev);
		dione_ir_start_streaming(dione_ir_priv->tc_dev);
	}

	return count;
}

static struct kobj_attribute dione_ir_sysfs_attr_restart_mipi = {
	.attr = { .name = "restart_mipi", .mode = VERIFY_OCTAL_PERMISSIONS(0220) },
	.show = NULL,
	.store = dione_ir_sysfs_restart_mipi,
};

static void dione_ir_sysfs_create(struct i2c_client *client,
				  struct dione_ir *priv)
{
	struct device *dev = &client->dev;
	const char *path = kobject_get_path(&dev->kobj, GFP_KERNEL);
	int err = -EINVAL;

	dione_ir_priv = priv;

	if (path != NULL)
		err = sysfs_create_file(&dev->kobj,
					&dione_ir_sysfs_attr_restart_mipi.attr);

	if (!err)
		dev_info(dev, "sysfs path: /sys%s/%s\n", path,
			 dione_ir_sysfs_attr_restart_mipi.attr.name);
}

static void dione_ir_sysfs_remove(struct i2c_client *client)
{
	sysfs_remove_file(&client->dev.kobj,
			  &dione_ir_sysfs_attr_restart_mipi.attr);
}
#else
static inline void dione_ir_sysfs_create(struct i2c_client *client,
					 struct dione_ir *priv)
{
}

static inline void dione_ir_sysfs_remove(struct i2c_client *client)
{
}
#endif

static int dione_ir_parse_fpga_address(struct i2c_client *client,
				       struct dione_ir *priv)
{
	struct device_node *node = client->dev.of_node;
	int len;

	if (!of_get_property(node, "fpga-address", &len)) {
		dev_err(&client->dev,
			"fpga-address property not found or too many\n");
		return -EINVAL;
	}

	priv->fpga_address = devm_kzalloc(&client->dev, len, GFP_KERNEL);
	if (!priv->fpga_address)
		return -ENOMEM;

	priv->fpga_address_num = len / sizeof(*priv->fpga_address);

	return of_property_read_u32_array(node, "fpga-address",
					  priv->fpga_address,
					  priv->fpga_address_num);
}

static int dione_ir_parse_link_frequencies(struct i2c_client *client,
					   struct dione_ir *priv)
{
	struct device_node *node;
	int len;

	node = of_graph_get_next_endpoint(client->dev.of_node, NULL);
	if (!node)
		return -EINVAL;

	if (!of_get_property(node, "link-frequencies", &len)) {
		dev_err(&client->dev,
			"link-frequencies property not found or too many\n");
		of_node_put(node);
		return -ENODATA;
	}

	priv->link_frequencies = devm_kzalloc(&client->dev, len, GFP_KERNEL);
	if (!priv->link_frequencies) {
		of_node_put(node);
		return -ENOMEM;
	}

	priv->link_frequencies_num = len / sizeof(*priv->link_frequencies);
	return of_property_read_u64_array(node, "link-frequencies",
					  priv->link_frequencies,
					  priv->link_frequencies_num);
}

static int dione_ir_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct tegracam_device *tc_dev;
	struct dione_ir *priv;
	int err;

	dev_dbg(dev, "probing v4l2 sensor at addr %#02x\n", client->addr);

	if (!IS_ENABLED(CONFIG_OF) || !dev->of_node)
		return -EINVAL;

	priv = devm_kzalloc(dev, sizeof(struct dione_ir), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	err = dione_ir_parse_fpga_address(client, priv);
	if (err < 0)
		return err;

	err = dione_ir_parse_link_frequencies(client, priv);
	if (err < 0)
		return err;

	priv->tc35_client = client;
	if (test_mode)
		quick_mode = 1;

	tc_dev = dione_ir_probe_sensor(priv);
	if (!tc_dev) {
		if (!priv->tc35_found && !priv->fpga_found)
			dev_err(dev, "no dione-ir sensor found\n");
		else if (priv->tc35_found && !priv->fpga_found)
			dev_err(dev, "no fpga found, please install it\n");
		else
			dev_err(dev, "dione-ir probe error\n");
		return -ENODEV;
	}

	err = tegracam_v4l2subdev_register(tc_dev, true);
	if (err) {
		dev_err(dev, "tegra camera subdev registration failed\n");
		tegracam_device_unregister(tc_dev);
		return err;
	}

	dev_info(dev, "detected dione-ir sensor%s%s%s%s\n",
		 priv->reva ? " (reva)" : "",
		 test_mode || quick_mode ? ", mode:" : "",
		 test_mode ? " test" : "",
		 quick_mode ? " quick" : "");

	dione_ir_sysfs_create(client, priv);

	return 0;
}

static int dione_ir_remove(struct i2c_client *client)
{
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	struct dione_ir *priv = (struct dione_ir *)s_data->priv;

	tegracam_v4l2subdev_unregister(priv->tc_dev);
	tegracam_device_unregister(priv->tc_dev);

	dione_ir_sysfs_remove(client);

	return 0;
}

static const struct i2c_device_id dione_ir_id[] = {
	{ "dioneir", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, dione_ir_id);

static struct i2c_driver dione_ir_i2c_driver = {
	.driver = {
		.name = "dioneir",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(dione_ir_of_match),
	},
	.probe = dione_ir_probe,
	.remove = dione_ir_remove,
	.id_table = dione_ir_id,
};
module_i2c_driver(dione_ir_i2c_driver);

MODULE_DESCRIPTION("Media Controller driver for Xenics Dione IR sensors");
MODULE_AUTHOR("Xenics Infrared Solutions / Peter Rozsahegyi, Botond Kardos");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
