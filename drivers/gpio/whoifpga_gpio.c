/*
 *  whoifpga_gpio.c - GPIO interface for WHOI FPGA
 *
 *  Author: Steve Sakoman <steve@sakoman.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License 2 as published
 *  by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/platform_device.h>

#include <linux/gpio.h>
#include <linux/gpio_whoifpga.h>

static DEFINE_SPINLOCK(gpio_lock);

/*
 * Map the Version API Registers so the driver can load them at initialization time.
 */

#define FPGA_API_LEVEL (0x00) //0x00
#define FPGA_VER_MAJOR (0x02) //0x01
#define FPGA_VER_MILE  (0x04) //0x02
#define FPGA_VER_MINOR (0x06) //0x03
#define FPGA_VER_DEVEL (0x08) //0x04
#define FPGA_VER_FLAG  (0x0A) //0x05

/*
 * Define the Magic Number registers for Verification of Hardware
 */
#define MAGIC1	       (0x20) //0x010
#define MAGIC2	       (0x22) //0x011

/*
 * Define the Fixed Hibernate Register
 */
#define HIBERNATE	(0x2A) //0x15

/*
 * Map to the Ctrl Register in the FPGA
 */
#define IO_CTRL_BASE	(0x42) //0x21

/*
 * Map to the Status Register in the FPGA
 */
#define IO_CTRL_STATUS	(0xC2) //0x61

void __iomem *fpga_base;

static int whoifpga_gpio_direction_in(struct gpio_chip *gc, unsigned  gpio_num)
{
	void __iomem *reg = fpga_base;
	u16 dat;

	spin_lock(&gpio_lock);

	reg += IO_CTRL_BASE;
	reg += gpio_num * 2;

	dat = __raw_readw(reg);
	dat &= 0x02;
	__raw_writew(dat, reg);

	spin_unlock(&gpio_lock);
	return 0;
}

static int whoifpga_gpio_get(struct gpio_chip *gc, unsigned gpio_num)
{
	void __iomem *reg = fpga_base;
	u16 dat;

	reg += IO_CTRL_STATUS;
	reg += gpio_num * 2;

	dat = __raw_readw(reg) & 0x01;
	return (int) dat;
}

static void whoifpga_gpio_set(struct gpio_chip *gc, unsigned gpio_num, int val)
{
	void __iomem *reg = fpga_base;
	u16 dat;

	spin_lock(&gpio_lock);

	reg += IO_CTRL_BASE;
	reg += gpio_num * 2;

	dat = __raw_readw(reg);
	if (val)
		dat |= 1;
	else
		dat &= ~1;

	__raw_writew(dat, reg);
	spin_unlock(&gpio_lock);
}

static int whoifpga_gpio_direction_out(struct gpio_chip *gc,
					unsigned gpio_num, int val)
{
	void __iomem *reg = fpga_base;
	u16 dat;

	spin_lock(&gpio_lock);

	reg += IO_CTRL_BASE;
	reg += gpio_num * 2;

	dat = __raw_readw(reg);
	dat |= 0x02;
	__raw_writew(dat, reg);

	spin_unlock(&gpio_lock);
	return 0;
}

static struct gpio_chip whoifpga_gpio = {
	.label			= "whoifpga_gpio",
	.owner			= THIS_MODULE,
	.direction_input	= whoifpga_gpio_direction_in,
	.get			= whoifpga_gpio_get,
	.direction_output	= whoifpga_gpio_direction_out,
	.set			= whoifpga_gpio_set,
};

static int __devinit whoifpga_gpio_probe(struct platform_device *pdev)
{
	struct whoifpga_platform_data *pdata;
	int err;
	u16 api, major, minor, mile, devel;
	char flag;
	u16 magic1, magic2;
	char version_string[26];

	pdata = pdev->dev.platform_data;
	if (!pdata || !pdata->gpio_base || !pdata->fpga_base_address) {
		dev_dbg(&pdev->dev, "incorrect or missing platform data\n");
		return -EINVAL;
	}

	/* Static mapping, never released */
	fpga_base = ioremap(pdata->fpga_base_address, 1024);
	if (!fpga_base) {
		dev_dbg(&pdev->dev, "Could not ioremap fpga_base\n");
		goto err_whoifpga_gpio;
	}

	whoifpga_gpio.base = pdata->gpio_base;
	whoifpga_gpio.ngpio = WHOIFPGA_NR_GPIOS;
	whoifpga_gpio.dev = &pdev->dev;

	err = gpiochip_add(&whoifpga_gpio);
	if (err < 0)
		goto err_whoifpga_gpio;

	//Lets check the magic numbers
	magic1 = __raw_readw(fpga_base + MAGIC1);
	magic2 = __raw_readw(fpga_base + MAGIC2);
	if (magic1 != magic2)
		goto err_whoifpga_gpio;

	//Let's Read the Current Version info of the FPGA API.
	api = __raw_readw(fpga_base + FPGA_API_LEVEL);
	major = __raw_readw(fpga_base + FPGA_VER_MAJOR);
	mile = __raw_readw(fpga_base + FPGA_VER_MILE);
	minor = __raw_readw(fpga_base + FPGA_VER_MINOR);
	devel = __raw_readw(fpga_base + FPGA_VER_DEVEL);
	flag = (char)__raw_readw(fpga_base + FPGA_VER_FLAG);

	//Print Version information
	if(flag != '\0')
	{
		snprintf(version_string, sizeof(version_string),"%d.%d.%d.%d-%c",
			 major,mile,minor,devel,flag);
	}
	else
	{
		snprintf(version_string, sizeof(version_string),"%d.%d.%d.%d",
			 major,mile,minor,devel);
	}

	//Print Info about the Location of the GPIO base
	dev_info(&pdev->dev, "WHOI FPGA(Version %s) at 0x%08x, %d GPIO's based at %d\n", version_string,
		pdata->fpga_base_address, WHOIFPGA_NR_GPIOS, whoifpga_gpio.base);

	return 0;

err_whoifpga_gpio:
	fpga_base = 0;

	return err;
}

static int __devexit whoifpga_gpio_remove(struct platform_device *pdev)
{
	if (fpga_base) {
		int err;

		err  = gpiochip_remove(&whoifpga_gpio);
		if (err)
			dev_err(&pdev->dev, "%s failed, %d\n",
				"gpiochip_remove()", err);

		fpga_base = 0;

		return err;
	}

	return 0;
}

static struct platform_driver whoifpga_gpio_driver = {
	.driver = {
		.name = "whoifpga_gpio",
		.owner = THIS_MODULE,
	},
	.probe		= whoifpga_gpio_probe,
	.remove		= __devexit_p(whoifpga_gpio_remove),
};

static int __init whoifpga_gpio_init(void)
{
	return platform_driver_register(&whoifpga_gpio_driver);
}

static void __exit whoifpga_gpio_exit(void)
{
	platform_driver_unregister(&whoifpga_gpio_driver);
}

module_init(whoifpga_gpio_init);
module_exit(whoifpga_gpio_exit);

MODULE_AUTHOR("Steve Sakoman <steve@sakoman.com>");
MODULE_DESCRIPTION("GPIO interface for WHOI FPGA");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:whoifpga_gpio");
