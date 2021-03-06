/*
 * rk_hdmi_i2s.c  --  HDMI i2s audio for rockchip
 *
 * Copyright (C) 2015 Fuzhou Rockchip Electronics Co., Ltd
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>

#include "card_info.h"
#include "rk_pcm.h"
#include "rk_i2s.h"

int gpio_hp_det = -ENOENT;
int gpio_hp_det_invert = -ENOENT;
static struct snd_soc_jack hp_jack;
static struct snd_soc_jack_pin hp_jack_pins[] = {
	{
		.pin = "Headphones",
		.mask = SND_JACK_HEADPHONE,
	},
};
static struct snd_soc_jack_gpio hp_jack_gpio = {
	.name = "Headphone detection",
	.report = SND_JACK_HEADPHONE,
	.debounce_time = 150,
};

static int hdmi_i2s_hifi_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;

	unsigned int pll_out = 0, dai_fmt = rtd->dai_link->dai_fmt;
	int div_bclk, div_mclk;
	int ret;

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, dai_fmt);
	if (ret < 0) {
		pr_err("%s: failed to set format for cpu dai\n", __func__);
		return ret;
	}

	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
	case 96000:
		pll_out = 12288000;
		break;
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		pll_out = 11289600;
		break;
	case 176400:
		pll_out = 11289600*2;
		break;
	case 192000:
		pll_out = 12288000*2;
		break;
	default:
		pr_err("%s: %d: rate(%d) not support.\n",
		       __func__, __LINE__, params_rate(params));
		return -EINVAL;
	}

	div_bclk = 127;
	div_mclk = pll_out/(params_rate(params)*(div_bclk+1))-1;

	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK, div_bclk);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, div_mclk);

	pr_debug("%s: %d: div_bclk: %d, div_mclk: %d\n",
		 __func__, __LINE__, div_bclk, div_mclk);

	if (gpio_is_valid(gpio_hp_det)) {
		snd_soc_jack_new(codec_dai->codec, "Headphones",
				SND_JACK_HEADPHONE,
				&hp_jack);
		hp_jack_gpio.gpio = gpio_hp_det;
		hp_jack_gpio.invert = gpio_hp_det_invert;
		snd_soc_jack_add_pins(&hp_jack, ARRAY_SIZE(hp_jack_pins), hp_jack_pins);
		snd_soc_jack_add_gpios(&hp_jack, 1, &hp_jack_gpio);
	}

	return 0;
}



static struct snd_soc_ops hdmi_i2s_hifi_ops = {
	.hw_params = hdmi_i2s_hifi_hw_params,
};

static struct snd_soc_dai_link hdmi_i2s_dai = {
	.name = "HDMI I2S",
	.stream_name = "HDMI PCM",
	.codec_dai_name = "rk-hdmi-i2s-hifi",
	.ops = &hdmi_i2s_hifi_ops,
};

static struct snd_soc_card rockchip_hdmi_i2s_snd_card = {
	.name = "RK-HDMI-I2S",
	.dai_link = &hdmi_i2s_dai,
	.num_links = 1,
};

static int rockchip_hdmi_i2s_audio_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card = &rockchip_hdmi_i2s_snd_card;
	struct device_node *node = pdev->dev.of_node;
	enum of_gpio_flags flags;

	card->dev = &pdev->dev;

	ret = rockchip_of_get_sound_card_info(card);
	if (ret) {
		pr_err("%s() get sound card info failed: %d\n",
		       __func__, ret);
		return ret;
	}

	gpio_hp_det = of_get_named_gpio_flags(node,
			"rockchip-hdmi-i2s,hp-det-gpio", 0, &flags);
	gpio_hp_det_invert = !!(flags && OF_GPIO_ACTIVE_LOW);

	ret = snd_soc_register_card(card);
	if (ret)
		pr_err("%s() register card failed: %d\n",
		       __func__, ret);

	return ret;
}

static int rockchip_hdmi_i2s_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	if (gpio_is_valid(gpio_hp_det)) {
		snd_soc_jack_free_gpios(&hp_jack, 1, &hp_jack_gpio);
	}

	snd_soc_unregister_card(card);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_hdmi_i2s_of_match[] = {
	{ .compatible = "rockchip-hdmi-i2s", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_hdmi_i2s_of_match);
#endif /* CONFIG_OF */

static struct platform_driver rockchip_hdmi_i2s_audio_driver = {
	.driver = {
		.name = "rockchip-hdmi-i2s",
		.pm = &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(rockchip_hdmi_i2s_of_match),
	},
	.probe = rockchip_hdmi_i2s_audio_probe,
	.remove = rockchip_hdmi_i2s_audio_remove,
};

module_platform_driver(rockchip_hdmi_i2s_audio_driver);

MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("Rockchip HDMI I2S Audio Card");
MODULE_LICENSE("GPL");
