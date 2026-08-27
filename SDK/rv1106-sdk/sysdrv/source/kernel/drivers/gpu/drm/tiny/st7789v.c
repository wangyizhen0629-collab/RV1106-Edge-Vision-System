// SPDX-License-Identifier: GPL-2.0+
/*
 * DRM driver for Sitronix ST7789V panels using the SPI pixel interface
 *
 * The controller setup mirrors the known-good Echo-Mate fbtft baseline.
 */

#include <linux/backlight.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/spi/spi.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_mipi_dbi.h>
#include <drm/drm_modeset_helper.h>
#include <video/mipi_display.h>

#define ST7789V_PORCTRL		0xb2
#define ST7789V_GCTRL		0xb7
#define ST7789V_VCOMS		0xbb
#define ST7789V_VDVVRHEN	0xc2
#define ST7789V_VRHS		0xc3
#define ST7789V_VDVS		0xc4
#define ST7789V_VCMOFSET	0xc5
#define ST7789V_PWCTRL1		0xd0
#define ST7789V_PVGAMCTRL	0xe0
#define ST7789V_NVGAMCTRL	0xe1

#define ST7789V_MADCTL_BGR	BIT(3)
#define ST7789V_MADCTL_MV	BIT(5)
#define ST7789V_MADCTL_MX	BIT(6)
#define ST7789V_MADCTL_MY	BIT(7)

#define ST7789V_COMMAND(dbi, command, seq...) \
	do { \
		ret = mipi_dbi_command(dbi, command, ##seq); \
		if (ret) \
			return ret; \
	} while (0)

struct st7789v_cfg {
	const struct drm_display_mode mode;
	unsigned int left_offset;
	unsigned int top_offset;
	bool bgr;
	bool write_only;
};

struct st7789v {
	struct mipi_dbi_dev dbidev; /* Must be first for managed release. */
	const struct st7789v_cfg *cfg;
};

static void st7789v_hw_reset(struct mipi_dbi *dbi)
{
	/*
	 * reset-gpios is active-low. Descriptor values are logical, so 1
	 * asserts the physical low level and 0 releases it. This deliberately
	 * does not use mipi_dbi_hw_reset(), whose 0 -> 1 sequence expects the
	 * opposite polarity convention in this 5.10 kernel.
	 */
	gpiod_set_value_cansleep(dbi->reset, 1);
	usleep_range(20, 40);
	gpiod_set_value_cansleep(dbi->reset, 0);
	msleep(120);
}

static int st7789v_display_init(struct mipi_dbi_dev *dbidev,
				const struct st7789v_cfg *cfg)
{
	struct mipi_dbi *dbi = &dbidev->dbi;
	u8 addr_mode = 0;
	int ret;

	st7789v_hw_reset(dbi);

	ST7789V_COMMAND(dbi, MIPI_DCS_EXIT_SLEEP_MODE);
	msleep(120);

	/* Keep the byte-for-byte values from the working fbtft driver. */
	ST7789V_COMMAND(dbi, MIPI_DCS_SET_PIXEL_FORMAT,
			 MIPI_DCS_PIXEL_FMT_16BIT);
	ST7789V_COMMAND(dbi, ST7789V_PORCTRL, 0x08, 0x08, 0x00, 0x22,
			 0x22);
	ST7789V_COMMAND(dbi, ST7789V_GCTRL, 0x35);
	ST7789V_COMMAND(dbi, ST7789V_VDVVRHEN, 0x01, 0xff);
	ST7789V_COMMAND(dbi, ST7789V_VRHS, 0x0b);
	ST7789V_COMMAND(dbi, ST7789V_VDVS, 0x20);
	ST7789V_COMMAND(dbi, ST7789V_VCOMS, 0x20);
	ST7789V_COMMAND(dbi, ST7789V_VCMOFSET, 0x20);
	ST7789V_COMMAND(dbi, ST7789V_PWCTRL1, 0xa4, 0xa1);
	ST7789V_COMMAND(dbi, MIPI_DCS_SET_DISPLAY_ON);

	switch (dbidev->rotation) {
	default:
		break;
	case 90:
		addr_mode = ST7789V_MADCTL_MV | ST7789V_MADCTL_MY;
		break;
	case 180:
		addr_mode = ST7789V_MADCTL_MX | ST7789V_MADCTL_MY;
		break;
	case 270:
		addr_mode = ST7789V_MADCTL_MV | ST7789V_MADCTL_MX;
		break;
	}

	if (cfg->bgr)
		addr_mode |= ST7789V_MADCTL_BGR;

	ST7789V_COMMAND(dbi, MIPI_DCS_SET_ADDRESS_MODE, addr_mode);
	ST7789V_COMMAND(dbi, ST7789V_PVGAMCTRL, 0xd0, 0x05, 0x0a, 0x09,
			 0x08, 0x05, 0x2e, 0x44, 0x45, 0x0f, 0x17, 0x16,
			 0x2b, 0x33);
	ST7789V_COMMAND(dbi, ST7789V_NVGAMCTRL, 0xd0, 0x05, 0x0a, 0x09,
			 0x08, 0x05, 0x2e, 0x43, 0x45, 0x0f, 0x16, 0x16,
			 0x2b, 0x33);

	return 0;
}

static void st7789v_pipe_enable(struct drm_simple_display_pipe *pipe,
				struct drm_crtc_state *crtc_state,
				struct drm_plane_state *plane_state)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct st7789v *priv = container_of(dbidev, struct st7789v, dbidev);
	int idx, ret;

	if (!drm_dev_enter(pipe->crtc.dev, &idx))
		return;

	DRM_DEBUG_KMS("\n");

	/* Avoid exposing reset/initialization traffic on a lit panel. */
	backlight_disable(dbidev->backlight);

	ret = st7789v_display_init(dbidev, priv->cfg);
	if (ret) {
		DRM_DEV_ERROR(dbidev->drm.dev,
			      "Failed to initialize display (%d)\n", ret);
		goto out_exit;
	}

	mipi_dbi_enable_flush(dbidev, crtc_state, plane_state);

out_exit:
	drm_dev_exit(idx);
}

static const struct drm_simple_display_pipe_funcs st7789v_pipe_funcs = {
	.enable = st7789v_pipe_enable,
	.disable = mipi_dbi_pipe_disable,
	.update = mipi_dbi_pipe_update,
	.prepare_fb = drm_gem_fb_simple_display_pipe_prepare_fb,
};

static const struct st7789v_cfg echo_mate_st7789v_cfg = {
	.mode = { DRM_SIMPLE_MODE(240, 320, 0, 0) },
	.left_offset = 0,
	.top_offset = 0,
	.bgr = false,
	.write_only = true,
};

DEFINE_DRM_GEM_CMA_FOPS(st7789v_fops);

static struct drm_driver st7789v_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops = &st7789v_fops,
	DRM_GEM_CMA_DRIVER_OPS_VMAP,
	.debugfs_init = mipi_dbi_debugfs_init,
	.name = "st7789v-dbi",
	.desc = "Sitronix ST7789V SPI DBI",
	.date = "20260827",
	.major = 1,
	.minor = 0,
};

static const struct of_device_id st7789v_of_match[] = {
	{
		.compatible = "sitronix,st7789v-dbi",
		.data = &echo_mate_st7789v_cfg,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, st7789v_of_match);

static const struct spi_device_id st7789v_id[] = {
	{ "st7789v-dbi", (uintptr_t)&echo_mate_st7789v_cfg },
	{ }
};
MODULE_DEVICE_TABLE(spi, st7789v_id);

static int st7789v_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	const struct st7789v_cfg *cfg;
	struct mipi_dbi_dev *dbidev;
	struct st7789v *priv;
	struct drm_device *drm;
	struct mipi_dbi *dbi;
	struct gpio_desc *dc;
	u32 rotation = 0;
	int ret;

	cfg = device_get_match_data(dev);
	if (!cfg)
		cfg = (void *)spi_get_device_id(spi)->driver_data;

	priv = devm_drm_dev_alloc(dev, &st7789v_driver,
				  struct st7789v, dbidev.drm);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	dbidev = &priv->dbidev;
	priv->cfg = cfg;
	dbi = &dbidev->dbi;
	drm = &dbidev->drm;

	/* Logical high asserts the active-low reset until pipe enable. */
	dbi->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(dbi->reset)) {
		DRM_DEV_ERROR(dev, "Failed to get gpio 'reset'\n");
		return PTR_ERR(dbi->reset);
	}

	dc = devm_gpiod_get(dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(dc)) {
		DRM_DEV_ERROR(dev, "Failed to get gpio 'dc'\n");
		return PTR_ERR(dc);
	}

	dbidev->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(dbidev->backlight))
		return PTR_ERR(dbidev->backlight);

	device_property_read_u32(dev, "rotation", &rotation);

	ret = mipi_dbi_spi_init(spi, dbi, dc);
	if (ret)
		return ret;

	if (cfg->write_only)
		dbi->read_commands = NULL;

	dbidev->left_offset = cfg->left_offset;
	dbidev->top_offset = cfg->top_offset;

	ret = mipi_dbi_dev_init(dbidev, &st7789v_pipe_funcs, &cfg->mode,
				rotation);
	if (ret)
		return ret;

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	spi_set_drvdata(spi, drm);
	drm_fbdev_generic_setup(drm, 0);

	return 0;
}

static int st7789v_remove(struct spi_device *spi)
{
	struct drm_device *drm = spi_get_drvdata(spi);

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);

	return 0;
}

static void st7789v_shutdown(struct spi_device *spi)
{
	drm_atomic_helper_shutdown(spi_get_drvdata(spi));
}

static struct spi_driver st7789v_spi_driver = {
	.driver = {
		.name = "st7789v-dbi",
		.of_match_table = st7789v_of_match,
	},
	.id_table = st7789v_id,
	.probe = st7789v_probe,
	.remove = st7789v_remove,
	.shutdown = st7789v_shutdown,
};
module_spi_driver(st7789v_spi_driver);

MODULE_DESCRIPTION("Sitronix ST7789V DRM driver for SPI pixel data");
MODULE_AUTHOR("Echo-Mate project");
MODULE_LICENSE("GPL");
