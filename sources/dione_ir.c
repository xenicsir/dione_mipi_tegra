/*
 * dione_ir.c - Dione IR sensor driver
 *
 * Copyright (c) 2021-2022, Xenics Infrared Solutions.  All rights reserved.
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
#include <linux/of_device.h>
#include <linux/of_gpio.h>

#include <media/tegra_v4l2_camera.h>
#include <media/tegracam_core.h>

#include "../platform/tegra/camera/camera_gpio.h"

#include "tc358746_regs.h"


#define DIONE640_I2C_ADDR              0x5a
#define DIONE640_REG_WIDTH             0x00080188
#define DIONE640_REG_WIDTH_MAX         0x0002f028
#define DIONE640_REG_FIRMWARE_VERSION  0x2000e000

#define DIONE640_STARTUP_TMO_MS        12000

#define DIONE1280_I2C_ADDR             0x5b
#define DIONE1280_REG_WIDTH_MAX        0x0002f028
#define DIONE1280_REG_HEIGHT_MAX       0x0002f02c
#define DIONE1280_REG_MODEL_NAME       0x00000044
#define DIONE1280_REG_FIRMWARE_VERSION 0x2000e000
#define DIONE1280_REG_ACQUISITION_STOP 0x00080104
#define DIONE1280_REG_ACQUISITION_SRC  0x00080108
#define DIONE1280_REG_ACQUISITION_STAT 0x0008010c

#define DIONE1280_I2C_TMO_MS           5
#define DIONE1280_STARTUP_TMO_MS       15000


static int test_mode = 0;
static int quick_mode = 1;
module_param(test_mode, int, 0644);
module_param(quick_mode, int, 0644);

enum {
	DIONE_IR_MODE_640x480_60FPS,
	DIONE_IR_MODE_1280x1024_60FPS,
};

static const int dioneir_60fps[] = {
	60,
};

/*
 * WARNING: frmfmt ordering need to match mode definition in
 * device tree!
 */
static const struct camera_common_frmfmt dioneir_frmfmt_common[] = {
	{{640, 480},	dioneir_60fps, 1, 0, DIONE_IR_MODE_640x480_60FPS},
	{{1280, 1024},	dioneir_60fps, 1, 0, DIONE_IR_MODE_1280x1024_60FPS},
	/* Add modes with no device tree support after below */
};

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

static const struct of_device_id dioneir_of_match[] = {
	{ .compatible = "xenics,dione_ir", },
	{ },
};
MODULE_DEVICE_TABLE(of, dioneir_of_match);

static const u32 ctrl_cid_list[] = {
	TEGRA_CAMERA_CID_GAIN,
	TEGRA_CAMERA_CID_EXPOSURE,
	TEGRA_CAMERA_CID_FRAME_RATE,
	TEGRA_CAMERA_CID_SENSOR_MODE_ID,
};

struct dione_struct {
	struct i2c_client		*tc35_client;
	struct i2c_client		*fpga_client;
	struct v4l2_subdev		*subdev;
	struct regmap			*tx_regmap;
	struct camera_common_data	*s_data;
	struct tegracam_device		*tc_dev;
	ktime_t					start_up;
	int						quick_mode;
};


struct tc358746_param {
	/* clock */
	bool is_continuous_clk;
	u16 pll_prd;
	u16 pll_fbd;
	u16 pll_frs;

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
	u32 hstxvregcnt;

	/* other */
	int lane_num;
	u32 fmt_width;
	u16 vb_fifo;
	u8 pdformat;
	u8 pdataf;
	u8 bpp;
};

const struct tc358746_param dione640_params = {
	.is_continuous_clk = false,
	.pll_prd = 3,
	.pll_fbd = 82,
	.pll_frs = 1,

	.lineinitcnt = 4000,
	.lptxtimecnt = 3,
	.tclk_preparecnt = 2,
	.tclk_zerocnt = 18,
	.tclk_trailcnt = 1,
	.ths_preparecnt = 3,
	.ths_zerocnt = 0,
	.twakeupcnt = 17000,
	.tclk_postcnt = 0,
	.ths_trailcnt = 1,
	.hstxvregcnt = 5,

	.lane_num = 2,
	.fmt_width = 640,
	.vb_fifo = 247,
	.pdformat = 0x3,
	.pdataf = 0,
	.bpp = 24
};

const struct tc358746_param dione1280_params = {
	.is_continuous_clk = true,
	.pll_prd = 3,
	.pll_fbd = 125,
	.pll_frs = 0,

	.lineinitcnt = 6500,
	.lptxtimecnt = 6,
	.tclk_preparecnt = 6,
	.tclk_zerocnt = 35,
	.tclk_trailcnt = 4,
	.ths_preparecnt = 6,
	.ths_zerocnt = 8,
	.twakeupcnt = 25000,
	.tclk_postcnt = 12,
	.ths_trailcnt = 5,
	.hstxvregcnt = 5,

	.lane_num = 2,
	.fmt_width = 1280,
	.vb_fifo = 2,
	.pdformat = 0x3,
	.pdataf = 0,
	.bpp = 24
};

/* CSI and other tc358746 settings for supported modes */
const struct tc358746_param dioneir_mode_params[] = {
	dione640_params,
	dione1280_params
};

static struct dione_struct *my_dione_struct = NULL;

static int dione1280_i2c_read(struct i2c_client *client, u32 addr, u8 *buf, u16 len);
static int dione1280_i2c_write32(struct i2c_client *client, u32 addr, u32 val);


static inline int dioneir_read_reg(struct camera_common_data *s_data,
	u16 addr, u8 *val)
{
	int err = 0;
	u32 reg_val = 0;

	err = regmap_read(s_data->regmap, addr, &reg_val);
	*val = reg_val & 0xff;

	return err;
}

static inline int dioneir_write_reg(struct camera_common_data *s_data,
	u16 addr, u8 val)
{
	int err = 0;

	err = regmap_write(s_data->regmap, addr, val);
	if (err)
		dev_err(s_data->dev, "%s: i2c write failed, 0x%x = %x",
			__func__, addr, val);

	return err;
}

static int dioneir_set_group_hold(struct tegracam_device *tc_dev, bool val)
{
	dev_dbg(tc_dev->dev, "%s val=%d\n", __func__, val);
	return 0;
}

static int dioneir_set_gain(struct tegracam_device *tc_dev, s64 val)
{
	dev_dbg(tc_dev->dev, "%s val=%lld\n", __func__, val);
	return 0;
}

static int dioneir_set_frame_rate(struct tegracam_device *tc_dev, s64 val)
{
	dev_dbg(tc_dev->dev, "%s val=%lld\n", __func__, val);
	return 0;
}

static int dioneir_set_exposure(struct tegracam_device *tc_dev, s64 val)
{
	dev_dbg(tc_dev->dev, "%s val=%lld\n", __func__, val);
	return 0;
}

static struct tegracam_ctrl_ops dioneir_ctrl_ops = {
	.numctrls = ARRAY_SIZE(ctrl_cid_list),
	.ctrl_cid_list = ctrl_cid_list,
	.set_gain = dioneir_set_gain,
	.set_exposure = dioneir_set_exposure,
	.set_frame_rate = dioneir_set_frame_rate,
	.set_group_hold = dioneir_set_group_hold,
};

static int dioneir_power_on(struct camera_common_data *s_data)
{
	int err = 0;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	struct dione_struct *priv = (struct dione_struct *)s_data->priv;

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
				gpio_set_value_cansleep(pw->reset_gpio, 1);
			else
				gpio_set_value(pw->reset_gpio, 1);
		}

		if (unlikely(!(pw->avdd || pw->iovdd || pw->dvdd)))
			goto skip_power_seqn;

		usleep_range(10, 20);

		if (pw->avdd) {
			err = regulator_enable(pw->avdd);
			if (err)
				goto dioneir_avdd_fail;
		}

		if (pw->iovdd) {
			err = regulator_enable(pw->iovdd);
			if (err)
				goto dioneir_iovdd_fail;
		}

		if (pw->dvdd) {
			err = regulator_enable(pw->dvdd);
			if (err)
				goto dioneir_dvdd_fail;
		}

		usleep_range(10, 20);

skip_power_seqn:
		if (pw->reset_gpio) {
			if (gpio_cansleep(pw->reset_gpio))
				gpio_set_value_cansleep(pw->reset_gpio, 0);
			else
				gpio_set_value(pw->reset_gpio, 0);
		}

		usleep_range(23000, 23100);
		msleep(200);
	} /* if (!quick_mode) */

	pw->state = SWITCH_ON;

	return 0;

dioneir_dvdd_fail:
	regulator_disable(pw->iovdd);

dioneir_iovdd_fail:
	regulator_disable(pw->avdd);

dioneir_avdd_fail:
	dev_err(dev, "%s failed.\n", __func__);

	return -ENODEV;
}

static int dioneir_power_off(struct camera_common_data *s_data)
{
	int err = 0;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	struct dione_struct *priv = (struct dione_struct *)s_data->priv;

	dev_dbg(dev, "%s: power off\n", __func__);

	if (pdata && pdata->power_off) {
		err = pdata->power_off(pw);
		if (err) {
			dev_err(dev, "%s failed.\n", __func__);
			return err;
		}
	} else {
		if (!priv->quick_mode) {
			if (pw->reset_gpio) {
				if (gpio_cansleep(pw->reset_gpio))
					gpio_set_value_cansleep(pw->reset_gpio, 1);
				else
					gpio_set_value(pw->reset_gpio, 1);
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

static int dioneir_power_on_reva(struct camera_common_data *s_data)
{
	int err = 0;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	struct dione_struct *priv = (struct dione_struct *)s_data->priv;

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
				gpio_set_value_cansleep(pw->reset_gpio, 0);
			else
				gpio_set_value(pw->reset_gpio, 0);
		}

		if (unlikely(!(pw->avdd || pw->iovdd || pw->dvdd)))
			goto skip_power_seqn;

		usleep_range(10, 20);

		if (pw->avdd) {
			err = regulator_enable(pw->avdd);
			if (err)
				goto dioneir_avdd_fail;
		}

		if (pw->iovdd) {
			err = regulator_enable(pw->iovdd);
			if (err)
				goto dioneir_iovdd_fail;
		}

		if (pw->dvdd) {
			err = regulator_enable(pw->dvdd);
			if (err)
				goto dioneir_dvdd_fail;
		}

		usleep_range(10, 20);

	skip_power_seqn:
		if (pw->reset_gpio) {
			if (gpio_cansleep(pw->reset_gpio))
				gpio_set_value_cansleep(pw->reset_gpio, 1);
			else
				gpio_set_value(pw->reset_gpio, 1);
		}

		usleep_range(23000, 23100);
		msleep(200);
	} /* if (!quick_mode) */

	pw->state = SWITCH_ON;

	return 0;

dioneir_dvdd_fail:
	regulator_disable(pw->iovdd);

dioneir_iovdd_fail:
	regulator_disable(pw->avdd);

dioneir_avdd_fail:
	dev_err(dev, "%s failed.\n", __func__);

	return -ENODEV;
}

static int dioneir_power_off_reva(struct camera_common_data *s_data)
{
	int err = 0;
	struct camera_common_power_rail *pw = s_data->power;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	struct dione_struct *priv = (struct dione_struct *)s_data->priv;

	dev_dbg(dev, "%s: power off\n", __func__);

	if (pdata && pdata->power_off) {
		err = pdata->power_off(pw);
		if (err) {
			dev_err(dev, "%s failed.\n", __func__);
			return err;
		}
	} else {
		if (!priv->quick_mode) {
			if (pw->reset_gpio) {
				if (gpio_cansleep(pw->reset_gpio))
					gpio_set_value_cansleep(pw->reset_gpio, 0);
				else
					gpio_set_value(pw->reset_gpio, 0);
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

static int dioneir_power_put(struct tegracam_device *tc_dev)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct camera_common_power_rail *pw = s_data->power;
	struct dione_struct *priv = (struct dione_struct *)tegracam_get_privdata(tc_dev);

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

static int dioneir_power_get(struct tegracam_device *tc_dev)
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
				dev_err(dev, "unable to get parent clock %s",
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

static struct camera_common_pdata *dioneir_parse_dt(
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

	match = of_match_device(dioneir_of_match, dev);
	if (!match) {
		dev_err(dev, "Failed to find matching dt id\n");
		return NULL;
	}

	board_priv_pdata = devm_kzalloc(dev,
		sizeof(*board_priv_pdata), GFP_KERNEL);
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
							const struct tc358746_param *csi_param)
{
	u32 pllctl0, pllctl1, pllctl0_new;
	int err;

	err = regmap_read(regmap, PLLCTL0, &pllctl0);
	if (!err)
		err = regmap_read(regmap, PLLCTL1, &pllctl1);

	if (err)
		return err;

	pllctl0_new = PLLCTL0_PLL_PRD_SET(csi_param->pll_prd) |
	  PLLCTL0_PLL_FBD_SET(csi_param->pll_fbd);

	/*
	 * Only rewrite when needed (new value or disabled), since rewriting
	 * triggers another format change event.
	 */

	if (pllctl0 != pllctl0_new || (pllctl1 & PLLCTL1_PLL_EN_MASK) == 0) {
		u16 pllctl1_mask = PLLCTL1_PLL_FRS_MASK | PLLCTL1_RESETB_MASK |
				   PLLCTL1_PLL_EN_MASK;
		u16 pllctl1_val = PLLCTL1_PLL_FRS_SET(csi_param->pll_frs) |
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
										const struct tc358746_param *csi_param)
{
	int err;

	err = regmap_update_bits(regmap, DATAFMT,
				 (DATAFMT_PDFMT_MASK | DATAFMT_UDT_EN_MASK),
				 DATAFMT_PDFMT_SET(csi_param->pdformat));

	if (!err)
		err = regmap_update_bits(regmap, CONFCTL, CONFCTL_PDATAF_MASK,
					 CONFCTL_PDATAF_SET(csi_param->pdataf));

	return err;
}

static int tc358746_set_buffers(struct regmap *regmap,
								const struct tc358746_param *csi_param)
{
	unsigned int byte_per_line = (csi_param->fmt_width * csi_param->bpp) / 8;
	int err;

	err = regmap_write(regmap, FIFOCTL, csi_param->vb_fifo);

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
							const struct tc358746_param *param)
{
	u32 val;
	int err;

	val = TCLK_HEADERCNT_TCLK_ZEROCNT_SET(param->tclk_zerocnt) |
	      TCLK_HEADERCNT_TCLK_PREPARECNT_SET(param->tclk_preparecnt);
	err = regmap_write(regmap, TCLK_HEADERCNT, val);

	val = THS_HEADERCNT_THS_ZEROCNT_SET(param->ths_zerocnt) |
	      THS_HEADERCNT_THS_PREPARECNT_SET(param->ths_preparecnt);
	if (!err)
		err = regmap_write(regmap, THS_HEADERCNT, val);

	if (!err)
		err = regmap_write(regmap, TWAKEUP, param->twakeupcnt);

	if (!err)
		err = regmap_write(regmap, TCLK_POSTCNT, param->tclk_postcnt);

	if (!err)
		err = regmap_write(regmap, THS_TRAILCNT, param->ths_trailcnt);

	if (!err)
		err = regmap_write(regmap, LINEINITCNT, param->lineinitcnt);

	if (!err)
		err = regmap_write(regmap, LPTXTIMECNT, param->lptxtimecnt);

	if (!err)
		err = regmap_write(regmap, TCLK_TRAILCNT, param->tclk_trailcnt);

	if (!err)
		err = regmap_write(regmap, HSTXVREGCNT, param->hstxvregcnt);

	val = param->is_continuous_clk ? TXOPTIONCNTRL_CONTCLKMODE_MASK : 0;
	if (!err)
		err = regmap_write(regmap, TXOPTIONCNTRL, val);

	return err;
}

static int tc358746_wr_csi_control(struct regmap *regmap, u32 val)
{
	u32 _val;

	val &= CSI_CONFW_DATA_MASK;
	_val = CSI_CONFW_MODE_SET_MASK | CSI_CONFW_ADDRESS_CSI_CONTROL_MASK |
		val;

	return regmap_write(regmap, CSI_CONFW, _val);
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
		CSI_CONTROL_EOTDIS_MASK; /* add according to excell */

	if (!err)
		err = tc358746_wr_csi_control(regmap, val);

	return err;
}

static int dioneir_set_mode_common(struct tegracam_device *tc_dev, s64 tmo_startup)
{
	struct dione_struct *priv = (struct dione_struct *)tegracam_get_privdata(tc_dev);
	struct camera_common_data *s_data = priv->s_data;
	struct regmap *ctl_regmap = s_data->regmap;
	struct regmap *tx_regmap = priv->tx_regmap;
	const struct tc358746_param *csi_setting;
	int err;

	err = 0;
	if (test_mode) {
		/* wait until FPGA in sensor finishes booting up */
		while(ktime_ms_delta(ktime_get(), priv->start_up) < tmo_startup)
			msleep(100);

		/* enable test pattern in the sensor module */
		err = dione1280_i2c_write32(priv->fpga_client, DIONE1280_REG_ACQUISITION_STOP, 2);
		if (!err) {
			msleep(300);
			err = dione1280_i2c_write32(priv->fpga_client, DIONE1280_REG_ACQUISITION_SRC, 0);
		}

		if (!err) {
			msleep(300);
			err = dione1280_i2c_write32(priv->fpga_client, DIONE1280_REG_ACQUISITION_STOP, 1);
		}
	}

	csi_setting = &dioneir_mode_params[s_data->mode];

	regmap_write(ctl_regmap, DBG_ACT_LINE_CNT, 0);

	if (!err)
		err = tc358746_sreset(ctl_regmap);
	if (err) {
		dev_err(tc_dev->dev, "Failed to reset chip\n");
		return err;
	}

	err = tc358746_set_pll(ctl_regmap, csi_setting);
	if (err) {
		dev_err(tc_dev->dev, "Failed to setup PLL\n");
		return err;
	}

	err = tc358746_set_csi_color_space(ctl_regmap, csi_setting);

	if (!err)
		err = tc358746_set_buffers(ctl_regmap, csi_setting);

	if (!err)
		err = tc358746_enable_csi_lanes(tx_regmap, csi_setting->lane_num, true);

	if (!err)
		err = tc358746_set_csi(tx_regmap, csi_setting);

	if (!err)
		err = tc358746_enable_csi_module(tx_regmap, csi_setting->lane_num);

	if (err)
		dev_err(tc_dev->dev, "%s return code (%d)\n", __func__, err);
	return err;
}

static int dione640_set_mode(struct tegracam_device *tc_dev)
{
	struct dione_struct *priv = (struct dione_struct *)tegracam_get_privdata(tc_dev);
	struct camera_common_data *s_data = priv->s_data;

	if (s_data->mode != DIONE_IR_MODE_640x480_60FPS)
		return -EINVAL;

	return dioneir_set_mode_common(tc_dev, DIONE640_STARTUP_TMO_MS);
}

static int dione1280_set_mode(struct tegracam_device *tc_dev)
{
	struct dione_struct *priv = (struct dione_struct *)tegracam_get_privdata(tc_dev);
	struct camera_common_data *s_data = priv->s_data;

	if (s_data->mode != DIONE_IR_MODE_1280x1024_60FPS)
		return -EINVAL;

	return dioneir_set_mode_common(tc_dev, DIONE1280_STARTUP_TMO_MS);
}


static int dioneir_start_streaming(struct tegracam_device *tc_dev)
{
	struct dione_struct *priv = (struct dione_struct *)tegracam_get_privdata(tc_dev);
	struct camera_common_data *s_data = priv->s_data;
	struct regmap *ctl_regmap = s_data->regmap;
	int err;

	err = regmap_write(ctl_regmap, PP_MISC, 0);
	if (!err)
		err = regmap_update_bits(ctl_regmap, CONFCTL,
								 CONFCTL_PPEN_MASK,
								 CONFCTL_PPEN_MASK);

	if (err)
		dev_err(tc_dev->dev, "%s return code (%d)\n", __func__, err);
	return err;
}

static int dioneir_stop_streaming(struct tegracam_device *tc_dev)
{
	struct dione_struct *priv = (struct dione_struct *)tegracam_get_privdata(tc_dev);
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

static struct camera_common_sensor_ops dione640_common_ops = {
	.numfrmfmts = ARRAY_SIZE(dioneir_frmfmt_common),
	.frmfmt_table = dioneir_frmfmt_common,
	.power_on = dioneir_power_on,
	.power_off = dioneir_power_off,
	.write_reg = dioneir_write_reg,
	.read_reg = dioneir_read_reg,
	.parse_dt = dioneir_parse_dt,
	.power_get = dioneir_power_get,
	.power_put = dioneir_power_put,
	.set_mode = dione640_set_mode,
	.start_streaming = dioneir_start_streaming,
	.stop_streaming = dioneir_stop_streaming,
};

static struct camera_common_sensor_ops dione640_reva_common_ops = {
	.numfrmfmts = ARRAY_SIZE(dioneir_frmfmt_common),
	.frmfmt_table = dioneir_frmfmt_common,
	.power_on = dioneir_power_on_reva,
	.power_off = dioneir_power_off_reva,
	.write_reg = dioneir_write_reg,
	.read_reg = dioneir_read_reg,
	.parse_dt = dioneir_parse_dt,
	.power_get = dioneir_power_get,
	.power_put = dioneir_power_put,
	.set_mode = dione640_set_mode,
	.start_streaming = dioneir_start_streaming,
	.stop_streaming = dioneir_stop_streaming,
};

static struct camera_common_sensor_ops dione1280_common_ops = {
	.numfrmfmts = ARRAY_SIZE(dioneir_frmfmt_common),
	.frmfmt_table = dioneir_frmfmt_common,
	.power_on = dioneir_power_on,
	.power_off = dioneir_power_off,
	.write_reg = dioneir_write_reg,
	.read_reg = dioneir_read_reg,
	.parse_dt = dioneir_parse_dt,
	.power_get = dioneir_power_get,
	.power_put = dioneir_power_put,
	.set_mode = dione1280_set_mode,
	.start_streaming = dioneir_start_streaming,
	.stop_streaming = dioneir_stop_streaming,
};

static int dione640_i2c_read(struct i2c_client *client, u32 dev_addr, void *dst,
							size_t len)
{
	struct i2c_msg msgs[2];
	u8 tmp_buf[72];
	int ret;

	ret = 0;
	if (len > sizeof(tmp_buf) - 2)
		ret = -1;

	if (!ret) {
		*( (u32 *)tmp_buf ) = dev_addr;
		*( (u16 *)tmp_buf + 2 ) = len;

		msgs[0].addr = client->addr;
		msgs[0].flags = 0;
		msgs[0].len = 6;
		msgs[0].buf = tmp_buf;

		msgs[1].addr = client->addr;
		msgs[1].flags = I2C_M_RD;
		msgs[1].len = len + 2;
		msgs[1].buf = tmp_buf;

		if (i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs)) != 2)
			ret = -1;
	}

	if (!ret) {
		if ((tmp_buf[0] != 0) || (tmp_buf[1] != 0))
			ret = -1;
		else
			memcpy(dst, tmp_buf+2, len);
	}

	return ret;
} /* dione640_i2c_read */


static int generic_i2c_read(struct i2c_client *client, void *buf, size_t len)
{
	struct i2c_msg msgs[1];
	int ret;

	msgs[0].addr = client->addr;
	msgs[0].flags = I2C_M_RD;
	msgs[0].len = len;
	msgs[0].buf = buf;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	return ret;
} /* generic_i2c_read */


static int generic_i2c_write(struct i2c_client *client, void *buf, size_t len)
{
	struct i2c_msg msgs[1];
	int ret;

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = len;
	msgs[0].buf = buf;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	return ret;
} /* generic_i2c_write */


static int dione1280_i2c_read( struct i2c_client *client, u32 addr, u8 *buf, u16 len )
{
	u8 tmp_buf[72];
	int ret, tmo, retry;

	ret = 0;
	if (len > sizeof(tmp_buf) - 2)
		ret = -1;

	if (!ret) {
		*( (u32 *)tmp_buf ) = addr;
		*( (u16 *)tmp_buf + 2 ) = len;
		retry = 4;
		tmo = DIONE1280_I2C_TMO_MS ;
		ret = -1;
		while (retry-- > 0) {
			if (generic_i2c_write(client, tmp_buf, 6) == 1) {
				ret = 0;
				break;
			}
			msleep(tmo);
			tmo <<= 2;
		}
	}

	if (!ret) {
		retry = 4;
		tmo = DIONE1280_I2C_TMO_MS ;
		ret = -1;
		msleep(2);
		while (retry-- > 0) {
			if (generic_i2c_read(client, tmp_buf, len+2) == 1) {
				ret = 0;
				break;
			}
			msleep(tmo);
			tmo <<= 2;
		}
	}

	if (!ret) {
		if ((tmp_buf[0] != 0) || (tmp_buf[1] != 0))
			ret = -1;
		else
			memcpy(buf, tmp_buf+2, len);
	}

	return ret;
}

static int dione1280_i2c_write32(struct i2c_client *client, u32 addr, u32 val)
{
	u8 tmp_buf[10];
	int ret, tmo, retry;

	ret = 0;
	*( (u32 *)tmp_buf ) = addr;
	*( (u16 *)tmp_buf + 2 ) = 4;
	memcpy(tmp_buf + 6, &val, 4);
	retry = 4;
	tmo = DIONE1280_I2C_TMO_MS;
	ret = -1;
	while (retry-- > 0) {
		if (generic_i2c_write(client, tmp_buf, 10) == 1) {
			ret = 0;
			break;
		}
		msleep(tmo);
		tmo <<= 2;
	}

	return ret;
}

static int detect_dione640(struct dione_struct *priv)
{
	struct device *dev = priv->s_data->dev;
	u8 buf[64];
	u32 reg_val;
	int cnt;
	int ret = 0;

	msleep(200);

	priv->fpga_client = i2c_new_dummy(priv->tc35_client->adapter, DIONE640_I2C_ADDR);

	if (!priv->fpga_client)
		ret = -ENODEV;

	if (!ret) {
		ret = dione640_i2c_read(priv->fpga_client, DIONE640_REG_WIDTH_MAX, (u8 *)&reg_val, 4);
		if (ret || (reg_val != 640))
			ret = -ENODEV;
	}

	if (!ret ) {
		ret = dione640_i2c_read(priv->fpga_client, DIONE640_REG_FIRMWARE_VERSION, buf, 64);
		if (!ret) {
			cnt = 63;
			while ((cnt > 0) && (buf[cnt] == 0xff))
				buf[cnt--] = 0;
			dev_info(dev, "FirmwareVersion: %s\n", buf);
		}
	}

	if (ret)
		if (priv->fpga_client != NULL) {
			i2c_unregister_device(priv->fpga_client);
			priv->fpga_client = NULL;
		}

	return ret;
}

static int detect_dione1280(struct dione_struct *priv)
{
	struct device *dev = priv->s_data->dev;
	u8 buf[64];
	u32 reg_val;
	int cnt;
	int ret = 0;

	msleep(500);

	priv->fpga_client = i2c_new_dummy(priv->tc35_client->adapter, DIONE1280_I2C_ADDR);

	if (!priv->fpga_client)
		ret = -ENODEV;

	if (!ret) {
		ret = dione1280_i2c_read(priv->fpga_client, DIONE1280_REG_WIDTH_MAX, (u8 *)&reg_val, 4);
		if (ret || (reg_val != 1280))
			ret = -ENODEV;
	}

	if (!ret) {
		ret = dione1280_i2c_read(priv->fpga_client, DIONE1280_REG_FIRMWARE_VERSION, buf, 64);
		if (!ret) {
			cnt = 63;
			while ((cnt > 0) && (buf[cnt] == 0xff))
				buf[cnt--] = 0;
			dev_info(dev, "FirmwareVersion: %s\n", buf);
		}
	}

	if (ret)
		if (priv->fpga_client != NULL) {
			i2c_unregister_device(priv->fpga_client);
			priv->fpga_client = NULL;
		}

	return ret;
}

static int dioneir_board_setup(struct dione_struct *priv)
{
	struct camera_common_data *s_data = priv->s_data;
	struct camera_common_pdata *pdata = s_data->pdata;
	struct device *dev = s_data->dev;
	struct regmap *ctl_regmap = s_data->regmap;
	u32 reg_val;
	int err = 0;
	int my_qm;

	if (pdata->mclk_name) {
		err = camera_common_mclk_enable(s_data);
		if (err) {
			dev_err(dev, "error turning on mclk (%d)\n", err);
			goto done;
		}
	}

	my_qm = priv->quick_mode;
	priv->quick_mode = 0;
	err = s_data->ops->power_on(s_data);
	priv->quick_mode = my_qm;

	if (err) {
		dev_err(dev, "error during power on sensor (%d)\n", err);
		goto err_power_on;
	}

	if ((s_data->ops == &dione640_common_ops) || (s_data->ops == &dione640_reva_common_ops))
		err = detect_dione640(priv);

	if (s_data->ops == &dione1280_common_ops)
		err = detect_dione1280(priv);

	if (err)
		goto err_reg_probe;

	/* Probe sensor model id registers */
	err = regmap_read(ctl_regmap, CHIPID, &reg_val);
	if (err) {
		dev_err(dev, "%s: error during i2c read probe (%d)\n",
			__func__, err);
		goto err_reg_probe;
	}

	if ((reg_val & CHIPID_CHIPID_MASK) != 0x4400) {
		dev_err(dev, "%s: invalid sensor model id: %x\n",
			__func__, reg_val);
		err = -ENODEV;
		goto err_reg_probe;
	}

err_reg_probe:
	s_data->ops->power_off(s_data);

err_power_on:
	if (pdata->mclk_name)
		camera_common_mclk_disable(s_data);

done:
	return err;
}

static int dioneir_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	dev_dbg(&client->dev, "%s:\n", __func__);

	return 0;
}

static const struct v4l2_subdev_internal_ops dioneir_subdev_internal_ops = {
	.open = dioneir_open,
};

static struct tegracam_device *dioneir_probe_one( struct dione_struct *priv,
												  struct camera_common_sensor_ops *ops )
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
	strncpy(tc_dev->name, "dione_ir", sizeof(tc_dev->name));
	tc_dev->dev_regmap_config = &ctl_regmap_config;
	tc_dev->sensor_ops = ops;
	tc_dev->v4l2sd_internal_ops = &dioneir_subdev_internal_ops;
	tc_dev->tcctrl_ops = &dioneir_ctrl_ops;

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

	if (!err)
		err = dioneir_board_setup(priv);

	priv->start_up = ktime_get();
	if (err) {
		if (!!tc_dev)
			tegracam_device_unregister(tc_dev);
		tc_dev = NULL;
	}

	if (!test_mode && (priv->fpga_client != NULL)) {
		i2c_unregister_device(priv->fpga_client);
		priv->fpga_client = NULL;
	}

	return tc_dev;
}


/**
 * sysfs interface function handling ".../restart_mipi"
 */
static ssize_t dioneir_sysfs_restart_mipi(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct camera_common_data *s_data;
	int cmd;

	if ( (my_dione_struct != NULL) && (sscanf(buf, "%x", &cmd) == 1) && (cmd == 1)) {
		s_data = my_dione_struct->s_data;
		dioneir_stop_streaming( my_dione_struct->tc_dev );
		msleep(1000);
		if (s_data->mode == DIONE_IR_MODE_640x480_60FPS)
			dione640_set_mode( my_dione_struct->tc_dev );
		else
			dione1280_set_mode( my_dione_struct->tc_dev );

		dioneir_start_streaming( my_dione_struct->tc_dev );
	}

	return count;
}

static struct kobj_attribute dioneir_sysfs_attr_restart_mipi = {
       .attr = { .name = "restart_mipi", .mode = VERIFY_OCTAL_PERMISSIONS(0220) },
       .show = NULL,
       .store = dioneir_sysfs_restart_mipi,
};


static int dioneir_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct tegracam_device *tc_dev;
	struct dione_struct *priv;
	char *path;
	int err;

	dev_dbg(dev, "probing v4l2 sensor at addr 0x%0x\n", client->addr);

	if (!IS_ENABLED(CONFIG_OF) || !client->dev.of_node)
		return -EINVAL;

	priv = devm_kzalloc(dev, sizeof(struct dione_struct), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->tc35_client = client;
	if (test_mode)
		quick_mode = 1;

	tc_dev = dioneir_probe_one(priv, &dione640_common_ops);

	if (!tc_dev)
		tc_dev = dioneir_probe_one(priv, &dione1280_common_ops);

	if (!tc_dev)
		tc_dev = dioneir_probe_one(priv, &dione640_reva_common_ops);

	if (!tc_dev)
		dev_err(dev, "%s: error, no sensor found\n", __func__);
	else {
		err = tegracam_v4l2subdev_register(tc_dev, true);
		if (err) {
			dev_err(dev, "tegra camera subdev registration failed\n");
			tegracam_device_unregister(tc_dev);
			return err;
		}

		dev_info(dev, "detected Dione IR sensor\n");

		my_dione_struct = priv;
		err = -EINVAL;
		path = kobject_get_path(&dev->kobj, GFP_KERNEL);
		if ( path != NULL )
			err = sysfs_create_file(&dev->kobj, &dioneir_sysfs_attr_restart_mipi.attr);

		if ( !err )
			dev_info(dev, "sysfs path: /sys%s/%s\n", path, dioneir_sysfs_attr_restart_mipi.attr.name );

		return 0;
	}

	return -ENODEV;
}

static int dioneir_remove(struct i2c_client *client)
{
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	struct dione_struct *priv = (struct dione_struct *)s_data->priv;

	tegracam_v4l2subdev_unregister(priv->tc_dev);
	tegracam_device_unregister(priv->tc_dev);
	sysfs_remove_file(&client->dev.kobj, &dioneir_sysfs_attr_restart_mipi.attr);

	return 0;
}

static const struct i2c_device_id dioneir_id[] = {
	{ "dione_ir", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, dioneir_id);

static struct i2c_driver dioneir_i2c_driver = {
	.driver = {
		.name = "dione_ir",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(dioneir_of_match),
	},
	.probe = dioneir_probe,
	.remove = dioneir_remove,
	.id_table = dioneir_id,
};
module_i2c_driver(dioneir_i2c_driver);

MODULE_DESCRIPTION("Media Controller driver for Xenics Dione640 and Dione1280");
MODULE_AUTHOR("Xenics Infrared Solutions / Botond Kardos");
MODULE_LICENSE("GPL v2");
