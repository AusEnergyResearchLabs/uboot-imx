/*
 * Copyright (C) 2012 Variscite, Ltd.
 *
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 * 
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <common.h>
#include <asm/io.h>
#include <asm/arch/mx6.h>
#include <asm/arch/mx6_pins.h>
#if defined(CONFIG_SECURE_BOOT)
#include <asm/arch/mx6_secure.h>
#endif
#include <asm/arch/mx6dl_pins.h>
#include <asm/arch/iomux-v3.h>
#include <asm/arch/regs-anadig.h>
#include <asm/errno.h>
#ifdef CONFIG_MXC_FEC
#include <miiphy.h>
#endif
#include "mx6_ddr_regs.h"

#if defined(CONFIG_VIDEO_MX5)
#include <linux/list.h>
#include <linux/fb.h>
#include <linux/mxcfb.h>
#include <ipu.h>
#endif
#if defined(CONFIG_VIDEO_MX5) || defined(CONFIG_MXC_EPDC)
#include <lcd.h>
#endif

#include "../../../drivers/video/mxc_epdc_fb.h"

#ifdef CONFIG_IMX_ECSPI
#include <imx_spi.h>
#endif

#if CONFIG_I2C_MXC
#include <i2c.h>
#endif

#ifdef CONFIG_CMD_MMC
#include <mmc.h>
#include <fsl_esdhc.h>
#endif

#ifdef CONFIG_ARCH_MMU
#include <asm/mmu.h>
#include <asm/arch/mmu.h>
#endif

#ifdef CONFIG_CMD_CLOCK
#include <asm/clock.h>
#endif

#ifdef CONFIG_CMD_IMXOTP
#include <imx_otp.h>
#endif

#ifdef CONFIG_MXC_GPIO
#include <asm/gpio.h>
#include <asm/arch/gpio.h>
#endif

#ifdef CONFIG_DWC_AHSATA
#include <ahci.h>
#endif

#ifdef CONFIG_ANDROID_RECOVERY
#include <recovery.h>
#endif
DECLARE_GLOBAL_DATA_PTR;

static enum boot_device boot_dev;

#define GPIO_USER_KEY   IMX_GPIO_NR(5, 20)

extern int sata_curr_device;
#ifdef CONFIG_VIDEO_MX5
extern unsigned char fsl_bmp_reversed_600x400[];
extern int fsl_bmp_reversed_600x400_size;
extern int g_ipu_hw_rev;

#if defined(CONFIG_BMP_8BPP)
unsigned short colormap[256];
#elif defined(CONFIG_BMP_16BPP)
unsigned short colormap[65536];
#else
unsigned short colormap[16777216];
#endif

static int di = 1;

extern int ipuv3_fb_init(struct fb_videomode *mode, int di,
			int interface_pix_fmt,
			ipu_di_clk_parent_t di_clk_parent,
			int di_clk_val);

static struct fb_videomode lvds_wvga = {
	 "WVGA", 57, 800, 480, 37037, 40, 60, 10, 10, 20, 10,
	 FB_SYNC_EXT,
	 FB_VMODE_NONINTERLACED,
	 0,
};

vidinfo_t panel_info;
#endif


static void set_gpio_output_val(unsigned base, unsigned mask, unsigned val)
{
	unsigned reg = readl(base + GPIO_DR);
	if (val & 1)
		reg |= mask;	/* set high */
	else
		reg &= ~mask;	/* clear low */
	writel(reg, base + GPIO_DR);

	reg = readl(base + GPIO_GDIR);
	reg |= mask;		/* configure GPIO line as output */
	writel(reg, base + GPIO_GDIR);
}

static inline void setup_boot_device(void)
{
	uint soc_sbmr = readl(SRC_BASE_ADDR + 0x4);
	uint bt_mem_ctl = (soc_sbmr & 0x000000FF) >> 4 ;
	uint bt_mem_type = (soc_sbmr & 0x00000008) >> 3;

	switch (bt_mem_ctl) {
	case 0x0:
		if (bt_mem_type)
			boot_dev = ONE_NAND_BOOT;
		else
			boot_dev = WEIM_NOR_BOOT;
		break;
	case 0x2:
			boot_dev = SATA_BOOT;
		break;
	case 0x3:
		if (bt_mem_type)
			boot_dev = I2C_BOOT;
		else
			boot_dev = SPI_NOR_BOOT;
		break;
	case 0x4:
	case 0x5:
		boot_dev = SD_BOOT;
		break;
	case 0x6:
	case 0x7:
		boot_dev = MMC_BOOT;
		break;
	case 0x8 ... 0xf:
		boot_dev = NAND_BOOT;
		break;
	default:
		boot_dev = UNKNOWN_BOOT;
		break;
	}
}

enum boot_device get_boot_device(void)
{
	return boot_dev;
}

u32 get_board_rev(void)
{
	return fsl_system_rev;
}

#ifdef CONFIG_ARCH_MMU
void board_mmu_init(void)
{
	unsigned long ttb_base = PHYS_SDRAM_1 + 0x4000;
	unsigned long i;

	/*
	* Set the TTB register
	*/
	asm volatile ("mcr  p15,0,%0,c2,c0,0" : : "r"(ttb_base) /*:*/);

	/*
	* Set the Domain Access Control Register
	*/
	i = ARM_ACCESS_DACR_DEFAULT;
	asm volatile ("mcr  p15,0,%0,c3,c0,0" : : "r"(i) /*:*/);

	/*
	* First clear all TT entries - ie Set them to Faulting
	*/
	memset((void *)ttb_base, 0, ARM_FIRST_LEVEL_PAGE_TABLE_SIZE);
	/* Actual   Virtual  Size   Attributes          Function */
	/* Base     Base     MB     cached? buffered?  access permissions */
	/* xxx00000 xxx00000 */
	X_ARM_MMU_SECTION(0x000, 0x000, 0x001,
			ARM_UNCACHEABLE, ARM_UNBUFFERABLE,
			ARM_ACCESS_PERM_RW_RW); /* ROM, 1M */
	X_ARM_MMU_SECTION(0x001, 0x001, 0x008,
			ARM_UNCACHEABLE, ARM_UNBUFFERABLE,
			ARM_ACCESS_PERM_RW_RW); /* 8M */
	X_ARM_MMU_SECTION(0x009, 0x009, 0x001,
			ARM_UNCACHEABLE, ARM_UNBUFFERABLE,
			ARM_ACCESS_PERM_RW_RW); /* IRAM */
	X_ARM_MMU_SECTION(0x00A, 0x00A, 0x0F6,
			ARM_UNCACHEABLE, ARM_UNBUFFERABLE,
			ARM_ACCESS_PERM_RW_RW); /* 246M */

	/* 2 GB memory starting at 0x10000000, only map 1.875 GB */
	X_ARM_MMU_SECTION(0x100, 0x100, 0x780,
			ARM_CACHEABLE, ARM_BUFFERABLE,
			ARM_ACCESS_PERM_RW_RW);
	/* uncached alias of the same 1.875 GB memory */
	X_ARM_MMU_SECTION(0x100, 0x880, 0x780,
			ARM_UNCACHEABLE, ARM_UNBUFFERABLE,
			ARM_ACCESS_PERM_RW_RW);

	/* Enable MMU */
	MMU_ON();
}
#endif


#define ANATOP_PLL_LOCK                 0x80000000
#define ANATOP_PLL_ENABLE_MASK          0x00002000
#define ANATOP_PLL_BYPASS_MASK          0x00010000
#define ANATOP_PLL_PWDN_MASK            0x00001000
#define ANATOP_PLL_HOLD_RING_OFF_MASK   0x00000800
#define ANATOP_SATA_CLK_ENABLE_MASK     0x00100000

#ifdef CONFIG_DWC_AHSATA
/* Staggered Spin-up */
#define	HOST_CAP_SSS			(1 << 27)
/* host version register*/
#define	HOST_VERSIONR			0xfc
#define PORT_SATA_SR			0x128

int sata_initialize(void)
{
	u32 reg = 0;
	u32 iterations = 1000000;

	if (sata_curr_device == -1) {

		/* Reset HBA */
		writel(HOST_RESET, SATA_ARB_BASE_ADDR + HOST_CTL);

		reg = 0;
		while (readl(SATA_ARB_BASE_ADDR + HOST_VERSIONR) == 0) {
			reg++;
			if (reg > 1000000)
				break;
		}

		reg = readl(SATA_ARB_BASE_ADDR + HOST_CAP);
		if (!(reg & HOST_CAP_SSS)) {
			reg |= HOST_CAP_SSS;
			writel(reg, SATA_ARB_BASE_ADDR + HOST_CAP);
		}

		reg = readl(SATA_ARB_BASE_ADDR + HOST_PORTS_IMPL);
		if (!(reg & 0x1))
			writel((reg | 0x1),
					SATA_ARB_BASE_ADDR + HOST_PORTS_IMPL);

		/* Release resources when there is no device on the port */
		do {
			reg = readl(SATA_ARB_BASE_ADDR + PORT_SATA_SR) & 0xF;
			if ((reg & 0xF) == 0)
				iterations--;
			else
				break;

		} while (iterations > 0);
	}

	return __sata_initialize();
}
#endif

static int setup_sata(void)
{
	u32 reg = 0;
	s32 timeout = 100000;

	/* Enable sata clock */
	reg = readl(CCM_BASE_ADDR + 0x7c); /* CCGR5 */
	reg |= 0x30;
	writel(reg, CCM_BASE_ADDR + 0x7c);

	/* Enable PLLs */
	reg = readl(ANATOP_BASE_ADDR + 0xe0); /* ENET PLL */
	reg &= ~ANATOP_PLL_PWDN_MASK;
	writel(reg, ANATOP_BASE_ADDR + 0xe0);
	reg |= ANATOP_PLL_ENABLE_MASK;
	while (timeout--) {
		if (readl(ANATOP_BASE_ADDR + 0xe0) & ANATOP_PLL_LOCK)
			break;
	}
	if (timeout <= 0)
		return -1;
	reg &= ~ANATOP_PLL_BYPASS_MASK;
	writel(reg, ANATOP_BASE_ADDR + 0xe0);
	reg |= ANATOP_SATA_CLK_ENABLE_MASK;
	writel(reg, ANATOP_BASE_ADDR + 0xe0);

	/* Enable sata phy */
	reg = readl(IOMUXC_BASE_ADDR + 0x34); /* GPR13 */

	reg &= ~0x07ffffff;
	/*
	 * rx_eq_val_0 = 5 [26:24]
	 * los_lvl = 0x12 [23:19]
	 * rx_dpll_mode_0 = 0x3 [18:16]
	 * mpll_ss_en = 0x0 [14]
	 * tx_atten_0 = 0x4 [13:11]
	 * tx_boost_0 = 0x0 [10:7]
	 * tx_lvl = 0x11 [6:2]
	 * mpll_ck_off_b = 0x1 [1]
	 * tx_edgerate_0 = 0x0 [0]
	 * */
	reg |= 0x59124c6;
	writel(reg, IOMUXC_BASE_ADDR + 0x34);

	return 0;
}
#ifndef CONFIG_VAR_SOM_MX6_SPL
int dram_init(void)
{
unsigned int volatile * const port1 = (unsigned int *) PHYS_SDRAM_1;
unsigned int volatile * const port2 = (unsigned int *) (PHYS_SDRAM_1 + (PHYS_SDRAM_1_SIZE / 2));

	/*
	 * Switch PL301_FAST2 to DDR Dual-channel mapping
	 * however this block the boot up, temperory redraw
	 */
	/*
	 * u32 reg = 1;
	 * writel(reg, GPV0_BASE_ADDR);
	 */

	gd->bd->bi_dram[0].start = PHYS_SDRAM_1;
	gd->bd->bi_dram[0].size = PHYS_SDRAM_1_SIZE;

	/*
	 * Check if we have only 1/2 GB
	 */
	*port2 = 0;
	*port1 = 0x3f3f3f3f;

	if (0x3f3f3f3f == *port2)
		gd->bd->bi_dram[0].size = PHYS_SDRAM_1_SIZE / 2;

	return 0;
}
#else /* Booted by SPL. get DRAM from Chip Select */
int dram_init(void){
volatile struct mmdc_p_regs *mmdc_p0;
ulong sdram_size, sdram_cs;
unsigned int volatile * const port1 = (unsigned int *) PHYS_SDRAM_1;
unsigned int volatile * port2;
unsigned int *sdram_global;

	mmdc_p0 = (struct mmdc_p_regs *) MMDC_P0_BASE_ADDR;
	sdram_cs = mmdc_p0->mdasp;
	sdram_size = 1024;

	switch(sdram_cs) {
		case 0x00000017:
			sdram_size = 512;
			break;
		case 0x00000027:
			sdram_size = 1024;
			break;
		case 0x00000047:
			sdram_size = 2048;
			break;
		case 0x00000087:
			sdram_size = 3840;
			break;
	}

	sdram_global =  (u32 *)0x917000;
	if (*sdram_global  > sdram_size) sdram_size = 3588;	//Android limitation 3.5GB.;

	do {
		if (sdram_size > 3000) break;
		port2 = (unsigned int volatile *) (PHYS_SDRAM_1 + ((sdram_size * 1024 * 1024) / 2));

		*port2 = 0;				// write zero to start of second half of memory.
		*port1 = 0x3f3f3f3f;	// write pattern to start of memory.

		if ((0x3f3f3f3f == *port2) && (sdram_size > 512))
			sdram_size = sdram_size / 2;	// Next step devide size by half
		else

		if (0 == *port2)		// Done actual size found.
			break;

	} while (sdram_size > 512);

	gd->bd->bi_dram[0].start = PHYS_SDRAM_1;
	gd->bd->bi_dram[0].size= ((ulong)sdram_size * 1024 * 1024);

	return 0;
}
#endif

static void setup_uart(void)
{
	if (mx6_chip_is_dq()) {
		/* UART1 TXD */
		mxc_iomux_v3_setup_pad(MX6Q_PAD_CSI0_DAT10__UART1_TXD);

		/* UART1 RXD */
		mxc_iomux_v3_setup_pad(MX6Q_PAD_CSI0_DAT11__UART1_RXD);
	} else {
		/* UART1 TXD */
		mxc_iomux_v3_setup_pad(MX6DL_PAD_CSI0_DAT10__UART1_TXD);

		/* UART1 RXD */
		mxc_iomux_v3_setup_pad(MX6DL_PAD_CSI0_DAT11__UART1_RXD);
	}
}

#ifdef CONFIG_I2C_MXC
#define I2C1_SDA_GPIO5_26_BIT_MASK  (1 << 26)
#define I2C1_SCL_GPIO5_27_BIT_MASK  (1 << 27)
#define I2C2_SCL_GPIO4_12_BIT_MASK  (1 << 12)
#define I2C2_SDA_GPIO4_13_BIT_MASK  (1 << 13)
#define I2C3_SCL_GPIO1_5_BIT_MASK   (1 << 5)
#define I2C3_SDA_GPIO7_11_BIT_MASK   (1 << 11)


static void setup_i2c(unsigned int module_base)
{
	unsigned int reg;

	switch (module_base) {
	case I2C1_BASE_ADDR:
	if (mx6_chip_is_dq()) {
		/* i2c1 SDA */
		mxc_iomux_v3_setup_pad(MX6Q_PAD_CSI0_DAT8__I2C1_SDA);

		/* i2c1 SCL */
		mxc_iomux_v3_setup_pad(MX6Q_PAD_CSI0_DAT9__I2C1_SCL);
	} else {
		/* i2c1 SDA */
		mxc_iomux_v3_setup_pad(MX6DL_PAD_CSI0_DAT8__I2C1_SDA);
		/* i2c1 SCL */
		mxc_iomux_v3_setup_pad(MX6DL_PAD_CSI0_DAT9__I2C1_SCL);
	}

		/* Enable i2c clock */
		reg = readl(CCM_BASE_ADDR + CLKCTL_CCGR2);
		reg |= 0xC0;
		writel(reg, CCM_BASE_ADDR + CLKCTL_CCGR2);

		break;
	case I2C2_BASE_ADDR:

	if (mx6_chip_is_dq()) {
		/* i2c2 SDA */
		mxc_iomux_v3_setup_pad(MX6Q_PAD_KEY_ROW3__I2C2_SDA);

		/* i2c2 SCL */
		mxc_iomux_v3_setup_pad(MX6Q_PAD_KEY_COL3__I2C2_SCL);
	} else {
		/* i2c2 SDA */
		mxc_iomux_v3_setup_pad(MX6DL_PAD_KEY_ROW3__I2C2_SDA);

		/* i2c2 SCL */
		mxc_iomux_v3_setup_pad(MX6DL_PAD_KEY_COL3__I2C2_SCL);
	}


		/* Enable i2c clock */
		reg = readl(CCM_BASE_ADDR + CLKCTL_CCGR2);
		reg |= 0x300;
		writel(reg, CCM_BASE_ADDR + CLKCTL_CCGR2);

		break;
	case I2C3_BASE_ADDR:
	if (mx6_chip_is_dq()) {
		/* GPIO_3 for I2C3_SCL */
		mxc_iomux_v3_setup_pad(MX6Q_PAD_GPIO_5__I2C3_SCL);
		/* GPIO_6 for I2C3_SDA */
		mxc_iomux_v3_setup_pad(MX6Q_PAD_GPIO_16__I2C3_SDA);
	} else {
		/* GPIO_3 for I2C3_SCL */
		mxc_iomux_v3_setup_pad(MX6DL_PAD_GPIO_5__I2C3_SCL);
		/* GPIO_6 for I2C3_SDA */
		mxc_iomux_v3_setup_pad(MX6DL_PAD_GPIO_16__I2C3_SDA);
	}

		/* Enable i2c clock */
		reg = readl(CCM_BASE_ADDR + CLKCTL_CCGR2);
		reg |= 0xC00;
		writel(reg, CCM_BASE_ADDR + CLKCTL_CCGR2);

		break;
	default:
		printf("Invalid I2C base: 0x%x\n", module_base);
		break;
	}
}
/* Note: udelay() is not accurate for i2c timing */
static void __udelay(int time)
{
	int i, j;

	for (i = 0; i < time; i++) {
		for (j = 0; j < 200; j++) {
			asm("nop");
			asm("nop");
		}
	}
}
static void mx6q_i2c_gpio_scl_direction(int bus, int output)
{
	u32 reg;

	switch (bus) {
	case 1:
	if (mx6_chip_is_dq())
		mxc_iomux_v3_setup_pad(MX6Q_PAD_CSI0_DAT9__GPIO_5_27);
	else
		mxc_iomux_v3_setup_pad(MX6DL_PAD_CSI0_DAT9__GPIO_5_27);

		reg = readl(GPIO5_BASE_ADDR + GPIO_GDIR);
		if (output)
			reg |= I2C1_SCL_GPIO5_27_BIT_MASK;
		else
			reg &= ~I2C1_SCL_GPIO5_27_BIT_MASK;
		writel(reg, GPIO5_BASE_ADDR + GPIO_GDIR);
		break;
	case 2:
	if (mx6_chip_is_dq())
		mxc_iomux_v3_setup_pad(MX6Q_PAD_KEY_COL3__GPIO_4_12);
	else
		mxc_iomux_v3_setup_pad(MX6DL_PAD_KEY_COL3__GPIO_4_12);

		reg = readl(GPIO4_BASE_ADDR + GPIO_GDIR);
		if (output)
			reg |= I2C2_SCL_GPIO4_12_BIT_MASK;
		else
			reg &= ~I2C2_SCL_GPIO4_12_BIT_MASK;
		writel(reg, GPIO4_BASE_ADDR + GPIO_GDIR);
		break;
	case 3:
	if (mx6_chip_is_dq())
		mxc_iomux_v3_setup_pad(MX6DL_PAD_GPIO_5__GPIO_1_5);
	else
		mxc_iomux_v3_setup_pad(MX6DL_PAD_GPIO_5__GPIO_1_5);

		reg = readl(GPIO1_BASE_ADDR + GPIO_GDIR);
		if (output)
			reg |= I2C3_SCL_GPIO1_5_BIT_MASK;
		else
			reg &= I2C3_SCL_GPIO1_5_BIT_MASK;
		writel(reg, GPIO1_BASE_ADDR + GPIO_GDIR);
		break;
	}
}

/* set 1 to output, sent 0 to input */
static void mx6q_i2c_gpio_sda_direction(int bus, int output)
{
	u32 reg;

	switch (bus) {
	case 1:
		if (mx6_chip_is_dq())
			mxc_iomux_v3_setup_pad(MX6Q_PAD_CSI0_DAT8__GPIO_5_26);
		else
			mxc_iomux_v3_setup_pad(MX6DL_PAD_CSI0_DAT8__GPIO_5_26);

		reg = readl(GPIO5_BASE_ADDR + GPIO_GDIR);
		if (output)
			reg |= I2C1_SDA_GPIO5_26_BIT_MASK;
		else
			reg &= ~I2C1_SDA_GPIO5_26_BIT_MASK;
		writel(reg, GPIO5_BASE_ADDR + GPIO_GDIR);
		break;
	case 2:
		if (mx6_chip_is_dq())
			mxc_iomux_v3_setup_pad(MX6Q_PAD_KEY_ROW3__GPIO_4_13);
		else
			mxc_iomux_v3_setup_pad(MX6DL_PAD_KEY_ROW3__GPIO_4_13);

		reg = readl(GPIO4_BASE_ADDR + GPIO_GDIR);
		if (output)
			reg |= I2C2_SDA_GPIO4_13_BIT_MASK;
		else
			reg &= ~I2C2_SDA_GPIO4_13_BIT_MASK;
		writel(reg, GPIO4_BASE_ADDR + GPIO_GDIR);
	case 3:
		if (mx6_chip_is_dq())
			mxc_iomux_v3_setup_pad(MX6Q_PAD_GPIO_16__GPIO_7_11);
		else
			mxc_iomux_v3_setup_pad(MX6DL_PAD_GPIO_16__GPIO_7_11);

		reg = readl(GPIO7_BASE_ADDR + GPIO_GDIR);
		if (output)
			reg |= I2C3_SDA_GPIO7_11_BIT_MASK;
		else
			reg &= ~I2C3_SDA_GPIO7_11_BIT_MASK;
		writel(reg, GPIO7_BASE_ADDR + GPIO_GDIR);
	default:
		break;
	}
}

/* set 1 to high 0 to low */
static void mx6q_i2c_gpio_scl_set_level(int bus, int high)
{
	u32 reg;

	switch (bus) {
	case 1:
		reg = readl(GPIO5_BASE_ADDR + GPIO_DR);
		if (high)
			reg |= I2C1_SCL_GPIO5_27_BIT_MASK;
		else
			reg &= ~I2C1_SCL_GPIO5_27_BIT_MASK;
		writel(reg, GPIO5_BASE_ADDR + GPIO_DR);
		break;
	case 2:
		reg = readl(GPIO4_BASE_ADDR + GPIO_DR);
		if (high)
			reg |= I2C2_SCL_GPIO4_12_BIT_MASK;
		else
			reg &= ~I2C2_SCL_GPIO4_12_BIT_MASK;
		writel(reg, GPIO4_BASE_ADDR + GPIO_DR);
		break;
	case 3:
		reg = readl(GPIO1_BASE_ADDR + GPIO_DR);
		if (high)
			reg |= I2C3_SCL_GPIO1_5_BIT_MASK;
		else
			reg &= ~I2C3_SCL_GPIO1_5_BIT_MASK;
		writel(reg, GPIO1_BASE_ADDR + GPIO_DR);
		break;
	}
}

/* set 1 to high 0 to low */
static void mx6q_i2c_gpio_sda_set_level(int bus, int high)
{
	u32 reg;

	switch (bus) {
	case 1:
		reg = readl(GPIO5_BASE_ADDR + GPIO_DR);
		if (high)
			reg |= I2C1_SDA_GPIO5_26_BIT_MASK;
		else
			reg &= ~I2C1_SDA_GPIO5_26_BIT_MASK;
		writel(reg, GPIO5_BASE_ADDR + GPIO_DR);
		break;
	case 2:
		reg = readl(GPIO4_BASE_ADDR + GPIO_DR);
		if (high)
			reg |= I2C2_SDA_GPIO4_13_BIT_MASK;
		else
			reg &= ~I2C2_SDA_GPIO4_13_BIT_MASK;
		writel(reg, GPIO4_BASE_ADDR + GPIO_DR);
		break;
	case 3:
		reg = readl(GPIO7_BASE_ADDR + GPIO_DR);
		if (high)
			reg |= I2C3_SDA_GPIO7_11_BIT_MASK;
		else
			reg &= ~I2C3_SDA_GPIO7_11_BIT_MASK;
		writel(reg, GPIO7_BASE_ADDR + GPIO_DR);
		break;
	}
}

static int mx6q_i2c_gpio_check_sda(int bus)
{
	u32 reg;
	int result = 0;

	switch (bus) {
	case 1:
		reg = readl(GPIO5_BASE_ADDR + GPIO_PSR);
		result = !!(reg & I2C1_SDA_GPIO5_26_BIT_MASK);
		break;
	case 2:
		reg = readl(GPIO4_BASE_ADDR + GPIO_PSR);
		result = !!(reg & I2C2_SDA_GPIO4_13_BIT_MASK);
		break;
	case 3:
		reg = readl(GPIO7_BASE_ADDR + GPIO_PSR);
		result = !!(reg & I2C3_SDA_GPIO7_11_BIT_MASK);
		break;
	}

	return result;
}

 /* Random reboot cause i2c SDA low issue:
  * the i2c bus busy because some device pull down the I2C SDA
  * line. This happens when Host is reading some byte from slave, and
  * then host is reset/reboot. Since in this case, device is
  * controlling i2c SDA line, the only thing host can do this give the
  * clock on SCL and sending NAK, and STOP to finish this
  * transaction.
  *
  * How to fix this issue:
  * detect if the SDA was low on bus send 8 dummy clock, and 1
  * clock + NAK, and STOP to finish i2c transaction the pending
  * transfer.
  */
int i2c_bus_recovery(void)
{
	int i, bus, result = 0;

	for (bus = 1; bus <= 3; bus++) {
		mx6q_i2c_gpio_sda_direction(bus, 0);

		if (mx6q_i2c_gpio_check_sda(bus) == 0) {
			printf("i2c: I2C%d SDA is low, start i2c recovery...\n", bus);
			mx6q_i2c_gpio_scl_direction(bus, 1);
			mx6q_i2c_gpio_scl_set_level(bus, 1);
			__udelay(10000);

			for (i = 0; i < 9; i++) {
				mx6q_i2c_gpio_scl_set_level(bus, 1);
				__udelay(5);
				mx6q_i2c_gpio_scl_set_level(bus, 0);
				__udelay(5);
			}

			/* 9th clock here, the slave should already
			   release the SDA, we can set SDA as high to
			   a NAK.*/
			mx6q_i2c_gpio_sda_direction(bus, 1);
			mx6q_i2c_gpio_sda_set_level(bus, 1);
			__udelay(1); /* Pull up SDA first */
			mx6q_i2c_gpio_scl_set_level(bus, 1);
			__udelay(5); /* plus pervious 1 us */
			mx6q_i2c_gpio_scl_set_level(bus, 0);
			__udelay(5);
			mx6q_i2c_gpio_sda_set_level(bus, 0);
			__udelay(5);
			mx6q_i2c_gpio_scl_set_level(bus, 1);
			__udelay(5);
			/* Here: SCL is high, and SDA from low to high, it's a
			 * stop condition */
			mx6q_i2c_gpio_sda_set_level(bus, 1);
			__udelay(5);

			mx6q_i2c_gpio_sda_direction(bus, 0);
			if (mx6q_i2c_gpio_check_sda(bus) == 1)
				printf("I2C%d Recovery success\n", bus);
			else {
				printf("I2C%d Recovery failed, I2C1 SDA still low!!!\n", bus);
				result |= 1 << bus;
			}
		}

		/* configure back to i2c */
		switch (bus) {
		case 1:
			setup_i2c(I2C1_BASE_ADDR);
			break;
		case 2:
			setup_i2c(I2C2_BASE_ADDR);
			break;
		case 3:
			setup_i2c(I2C3_BASE_ADDR);
			break;
		}
	}

	return result;
}

static int setup_pmic_voltages(void)
{
	unsigned char value, rev_id = 0 ;
	i2c_init(CONFIG_SYS_I2C_SPEED, CONFIG_SYS_I2C_SLAVE);
	if (!i2c_probe(0x8)) {
		if (i2c_read(0x8, 0, 1, &value, 1)) {
			printf("Read device ID error!\n");
			return -1;
		}
		if (i2c_read(0x8, 3, 1, &rev_id, 1)) {
			printf("Read Rev ID error!\n");
			return -1;
		}
		printf("Found PFUZE100! deviceid=%x,revid=%x\n", value, rev_id);

		/* Set Gigbit Ethernet voltage (SOM v1.1/1.0)*/
		value = 0x60;
		i2c_write(0x8, 0x4a, 1, &value, 1);

		/*set VGEN3 to 2.5V*/
		value = 0x77;
		if (i2c_write(0x8, 0x6e, 1, &value, 1)) {
			printf("Set VGEN3 error!\n");
			return -1;
		}
	}

	return 0;
}
#endif

#ifdef CONFIG_IMX_ECSPI
s32 spi_get_cfg(struct imx_spi_dev_t *dev)
{
	switch (dev->slave.cs) {
	case 0:
		/* SPI-NOR */
		dev->base = ECSPI1_BASE_ADDR;
		dev->freq = 25000000;
		dev->ss_pol = IMX_SPI_ACTIVE_LOW;
		dev->ss = 0;
		dev->fifo_sz = 64 * 4;
		dev->us_delay = 0;
		break;
	case 1:
		/* SPI-NOR */
		dev->base = ECSPI1_BASE_ADDR;
		dev->freq = 25000000;
		dev->ss_pol = IMX_SPI_ACTIVE_LOW;
		dev->ss = 1;
		dev->fifo_sz = 64 * 4;
		dev->us_delay = 0;
		break;
	default:
		printf("Invalid Bus ID!\n");
	}

	return 0;
}

void spi_io_init(struct imx_spi_dev_t *dev)
{
	u32 reg;

	switch (dev->base) {
	case ECSPI1_BASE_ADDR:
#if 0
		/* Enable clock */
		reg = readl(CCM_BASE_ADDR + CLKCTL_CCGR1);
		reg |= 0x3;
		writel(reg, CCM_BASE_ADDR + CLKCTL_CCGR1);

#if defined CONFIG_MX6Q
		/* SCLK */
		mxc_iomux_v3_setup_pad(MX6Q_PAD_KEY_COL0__ECSPI1_SCLK);

		/* MISO */
		mxc_iomux_v3_setup_pad(MX6Q_PAD_KEY_COL1__ECSPI1_MISO);

		/* MOSI */
		mxc_iomux_v3_setup_pad(MX6Q_PAD_KEY_ROW0__ECSPI1_MOSI);

		mxc_iomux_v3_setup_pad(MX6Q_PAD_KEY_ROW1__ECSPI1_SS0);
#elif defined CONFIG_MX6DL
		/* SCLK */
		mxc_iomux_v3_setup_pad(MX6DL_PAD_KEY_COL0__ECSPI1_SCLK);

		/* MISO */
		mxc_iomux_v3_setup_pad(MX6DL_PAD_KEY_COL1__ECSPI1_MISO);

		/* MOSI */
		mxc_iomux_v3_setup_pad(MX6DL_PAD_KEY_ROW0__ECSPI1_MOSI);

		mxc_iomux_v3_setup_pad(MX6DL_PAD_KEY_ROW1__ECSPI1_SS0);
#endif
#endif
		break;
	case ECSPI2_BASE_ADDR:
	case ECSPI3_BASE_ADDR:
		/* ecspi2-3 fall through */
		break;
	default:
		break;
	}
	return;
}
#endif

#ifdef CONFIG_NAND_GPMI

iomux_v3_cfg_t nfc_pads_qd[] = {
	MX6Q_PAD_NANDF_CLE__RAWNAND_CLE,
	MX6Q_PAD_NANDF_ALE__RAWNAND_ALE,
	MX6Q_PAD_NANDF_WP_B__RAWNAND_RESETN,
	MX6Q_PAD_NANDF_RB0__RAWNAND_READY0,
	MX6Q_PAD_NANDF_CS0__RAWNAND_CE0N,
	MX6Q_PAD_SD4_CMD__RAWNAND_RDN,
	MX6Q_PAD_SD4_CLK__RAWNAND_WRN,
	MX6Q_PAD_NANDF_D0__RAWNAND_D0,
	MX6Q_PAD_NANDF_D1__RAWNAND_D1,
	MX6Q_PAD_NANDF_D2__RAWNAND_D2,
	MX6Q_PAD_NANDF_D3__RAWNAND_D3,
	MX6Q_PAD_NANDF_D4__RAWNAND_D4,
	MX6Q_PAD_NANDF_D5__RAWNAND_D5,
	MX6Q_PAD_NANDF_D6__RAWNAND_D6,
	MX6Q_PAD_NANDF_D7__RAWNAND_D7,
};

iomux_v3_cfg_t nfc_pads_dl[] = {
	MX6DL_PAD_NANDF_CLE__RAWNAND_CLE,
	MX6DL_PAD_NANDF_ALE__RAWNAND_ALE,
	MX6DL_PAD_NANDF_WP_B__RAWNAND_RESETN,
	MX6DL_PAD_NANDF_RB0__RAWNAND_READY0,
	MX6DL_PAD_NANDF_CS0__RAWNAND_CE0N,
	MX6DL_PAD_SD4_CMD__RAWNAND_RDN,
	MX6DL_PAD_SD4_CLK__RAWNAND_WRN,
	MX6DL_PAD_NANDF_D0__RAWNAND_D0,
	MX6DL_PAD_NANDF_D1__RAWNAND_D1,
	MX6DL_PAD_NANDF_D2__RAWNAND_D2,
	MX6DL_PAD_NANDF_D3__RAWNAND_D3,
	MX6DL_PAD_NANDF_D4__RAWNAND_D4,
	MX6DL_PAD_NANDF_D5__RAWNAND_D5,
	MX6DL_PAD_NANDF_D6__RAWNAND_D6,
	MX6DL_PAD_NANDF_D7__RAWNAND_D7,
};
	

int setup_gpmi_nand(void)
{
	unsigned int reg;

	/* config gpmi nand iomux */
	if (mx6_chip_is_dq())
		mxc_iomux_v3_setup_multiple_pads(nfc_pads_qd, ARRAY_SIZE(nfc_pads_qd));
	else
		mxc_iomux_v3_setup_multiple_pads(nfc_pads_dl, ARRAY_SIZE(nfc_pads_dl));

	/* config gpmi and bch clock to 20Mhz, from pll2 400M pfd*/
	reg = readl(CCM_BASE_ADDR + CLKCTL_CS2CDR);
	reg &= 0xF800FFFF;
	reg |= 0x02630000;
	writel(reg, CCM_BASE_ADDR + CLKCTL_CS2CDR);

	/* enable gpmi and bch clock gating */
	reg = readl(CCM_BASE_ADDR + CLKCTL_CCGR4);
	reg |= 0xFF003000;
	writel(reg, CCM_BASE_ADDR + CLKCTL_CCGR4);

	/* enable apbh clock gating */
	reg = readl(CCM_BASE_ADDR + CLKCTL_CCGR0);
	reg |= 0x0030;
	writel(reg, CCM_BASE_ADDR + CLKCTL_CCGR0);

}
#endif

#ifdef CONFIG_NET_MULTI
int board_eth_init(bd_t *bis)
{
	int rc = -ENODEV;

	return rc;
}
#endif

#ifdef CONFIG_CMD_MMC

/* On this board, only SD3 can support 1.8V signalling
 * that is required for UHS-I mode of operation.
 * Last element in struct is used to indicate 1.8V support.
 */
struct fsl_esdhc_cfg usdhc_cfg[4] = {
	{USDHC1_BASE_ADDR, 1, 1, 1, 0},
	{USDHC2_BASE_ADDR, 1, 1, 1, 0},
	{USDHC3_BASE_ADDR, 1, 1, 1, 0},
	{USDHC4_BASE_ADDR, 1, 1, 1, 0},
};

iomux_v3_cfg_t usdhc1_pads_qd[] = {
	MX6Q_PAD_SD1_CLK__USDHC1_CLK,
	MX6Q_PAD_SD1_CMD__USDHC1_CMD,
	MX6Q_PAD_SD1_DAT0__USDHC1_DAT0,
	MX6Q_PAD_SD1_DAT1__USDHC1_DAT1,
	MX6Q_PAD_SD1_DAT2__USDHC1_DAT2,
	MX6Q_PAD_SD1_DAT3__USDHC1_DAT3,
};

iomux_v3_cfg_t usdhc1_pads_dl[] = {
	MX6DL_PAD_SD1_CLK__USDHC1_CLK,
	MX6DL_PAD_SD1_CMD__USDHC1_CMD,
	MX6DL_PAD_SD1_DAT0__USDHC1_DAT0,
	MX6DL_PAD_SD1_DAT1__USDHC1_DAT1,
	MX6DL_PAD_SD1_DAT2__USDHC1_DAT2,
	MX6DL_PAD_SD1_DAT3__USDHC1_DAT3,
};

iomux_v3_cfg_t usdhc2_pads_qd[] = {
	MX6Q_PAD_SD2_CLK__USDHC2_CLK,
	MX6Q_PAD_SD2_CMD__USDHC2_CMD,
	MX6Q_PAD_SD2_DAT0__USDHC2_DAT0,
	MX6Q_PAD_SD2_DAT1__USDHC2_DAT1,
	MX6Q_PAD_SD2_DAT2__USDHC2_DAT2,
	MX6Q_PAD_SD2_DAT3__USDHC2_DAT3,
};

iomux_v3_cfg_t usdhc2_pads_dl[] = {
	MX6DL_PAD_SD2_CLK__USDHC2_CLK,
	MX6DL_PAD_SD2_CMD__USDHC2_CMD,
	MX6DL_PAD_SD2_DAT0__USDHC2_DAT0,
	MX6DL_PAD_SD2_DAT1__USDHC2_DAT1,
	MX6DL_PAD_SD2_DAT2__USDHC2_DAT2,
	MX6DL_PAD_SD2_DAT3__USDHC2_DAT3,
};


#define USDHC_PAD_CTRL_DEFAULT (PAD_CTL_PKE | PAD_CTL_PUE |		\
		PAD_CTL_PUS_47K_UP  | PAD_CTL_SPEED_LOW |		\
		PAD_CTL_DSE_80ohm   | PAD_CTL_SRE_FAST  | PAD_CTL_HYS)

#define USDHC_PAD_CTRL_100MHZ (PAD_CTL_PKE | PAD_CTL_PUE |	\
		PAD_CTL_PUS_47K_UP  | PAD_CTL_SPEED_MED |		\
		PAD_CTL_DSE_40ohm   | PAD_CTL_SRE_FAST  | PAD_CTL_HYS)

#define USDHC_PAD_CTRL_200MHZ (PAD_CTL_PKE | PAD_CTL_PUE |	\
		PAD_CTL_PUS_47K_UP  | PAD_CTL_SPEED_HIGH |		\
		PAD_CTL_DSE_40ohm   | PAD_CTL_SRE_FAST   | PAD_CTL_HYS)

int usdhc_gpio_init(bd_t *bis)
{
	s32 status = 0;
	u32 index = 0;

	for (index = 0; index < CONFIG_SYS_FSL_USDHC_NUM;
		++index) {
		switch (index) {
		case 0:		//eMMC
			if (mx6_chip_is_dq())

				mxc_iomux_v3_setup_multiple_pads(usdhc1_pads_qd,sizeof(usdhc1_pads_qd) / sizeof(usdhc1_pads_qd[0]));
			else
				mxc_iomux_v3_setup_multiple_pads(usdhc1_pads_dl,sizeof(usdhc1_pads_dl) / sizeof(usdhc1_pads_dl[0]));
			status |= fsl_esdhc_initialize(bis, &usdhc_cfg[index]);
			break;
		case 1:		//sdcard
			if (mx6_chip_is_dq())

				mxc_iomux_v3_setup_multiple_pads(usdhc2_pads_qd,sizeof(usdhc2_pads_qd) / sizeof(usdhc2_pads_qd[0]));
			else
				mxc_iomux_v3_setup_multiple_pads(usdhc2_pads_dl,sizeof(usdhc2_pads_dl) / sizeof(usdhc2_pads_dl[0]));
			status |= fsl_esdhc_initialize(bis, &usdhc_cfg[index]);
			break;
		case 2:
			break;
		case 3:
			break;
		default:
			printf("Warning: you configured more USDHC controllers"
				"(%d) then supported by the board (%d)\n",
				index+1, CONFIG_SYS_FSL_USDHC_NUM);
			return status;
		}
	}

	return status;
}

static void usdhc_switch_pad(iomux_v3_cfg_t *pad_list, unsigned count,
	iomux_v3_cfg_t *new_pad_list, iomux_v3_cfg_t pad_val)
{
	u32 i;

	for (i = 0; i < count; i++) {
		new_pad_list[i] = pad_list[i] & (~MUX_PAD_CTRL_MASK);
		new_pad_list[i] |= MUX_PAD_CTRL(pad_val);
	}
}

int board_mmc_io_switch(u32 index, u32 clock)
{
	iomux_v3_cfg_t new_pads[14];
	u32 count;
	iomux_v3_cfg_t pad_ctrl = USDHC_PAD_CTRL_DEFAULT;

	if (clock >= 200000000)
		pad_ctrl = USDHC_PAD_CTRL_200MHZ;
	else if (clock == 100000000)
		pad_ctrl = USDHC_PAD_CTRL_100MHZ;

	switch (index) {
	case 0:
		if (mx6_chip_is_dq()) {
			count = sizeof(usdhc2_pads_qd) / sizeof(usdhc2_pads_qd[0]);
			usdhc_switch_pad(usdhc2_pads_qd, count, new_pads, pad_ctrl);
		} else {
			count = sizeof(usdhc2_pads_dl) / sizeof(usdhc2_pads_dl[0]);
			usdhc_switch_pad(usdhc2_pads_dl, count, new_pads, pad_ctrl);
		}
		break;
	case 1:
		if (mx6_chip_is_dq()) {
			count = sizeof(usdhc1_pads_qd) / sizeof(usdhc1_pads_qd[0]);
			usdhc_switch_pad(usdhc1_pads_qd, count, new_pads, pad_ctrl);
		} else {
			count = sizeof(usdhc1_pads_dl) / sizeof(usdhc1_pads_dl[0]);
			usdhc_switch_pad(usdhc1_pads_dl, count, new_pads, pad_ctrl);
		}
		break;
	case 2:
		break;
	case 3:
		break;
	default:
		printf("Warning: you configured more USDHC controllers"
			"(%d) then supported by the board (%d)\n",
			index+1, CONFIG_SYS_FSL_USDHC_NUM);
		return -1;
	}

	mxc_iomux_v3_setup_multiple_pads(new_pads, count);

	return 0;
}

int board_mmc_init(bd_t *bis)
{
	if (!usdhc_gpio_init(bis))
		return 0;
	else
		return -1;
}

/* For DDR mode operation, provide target delay parameter for each SD port.
 * Use cfg->esdhc_base to distinguish the SD port #. The delay for each port
 * is dependent on signal layout for that particular port.  If the following
 * CONFIG is not defined, then the default target delay value will be used.
 */
#ifdef CONFIG_GET_DDR_TARGET_DELAY
u32 get_ddr_delay(struct fsl_esdhc_cfg *cfg)
{
	/* No delay required on VAR-SOM-MX6 board ports */
	return 0;
}
#endif

#endif

#ifdef CONFIG_LCD
void lcd_enable(void)
{
	char *s;
	int ret;
	unsigned int reg;

	s = getenv("lvds_num");
	di = simple_strtol(s, NULL, 10);

	g_ipu_hw_rev = IPUV3_HW_REV_IPUV3H;

#if defined CONFIG_MX6Q
	/* GPIO backlight */
	mxc_iomux_v3_setup_pad(MX6Q_PAD_DISP0_DAT9__GPIO_4_30);
#elif defined CONFIG_MX6DL
	/* GPIO backlight */
	mxc_iomux_v3_setup_pad(MX6DL_PAD_DISP0_DAT9__GPIO_4_30);
#endif

	/* Set GPIO backlight to high. */
	reg = readl(GPIO4_BASE_ADDR + GPIO_GDIR);
	reg |= (1 << 30);
	writel(reg, GPIO4_BASE_ADDR + GPIO_GDIR);

	reg = readl(GPIO4_BASE_ADDR + GPIO_DR);
	reg |= (1 << 30);
	writel(reg, GPIO4_BASE_ADDR + GPIO_DR);

	/* Enable IPU clock */
	if (di == 1) {
		reg = readl(CCM_BASE_ADDR + CLKCTL_CCGR3);
		reg |= 0xC033;
		writel(reg, CCM_BASE_ADDR + CLKCTL_CCGR3);
	} else {
		reg = readl(CCM_BASE_ADDR + CLKCTL_CCGR3);
		reg |= 0x300F;
		writel(reg, CCM_BASE_ADDR + CLKCTL_CCGR3);
	}

	ret = ipuv3_fb_init(&lvds_wvga, di, IPU_PIX_FMT_RGB666,
			DI_PCLK_LDB, 65000000);
	if (ret)
		puts("LCD cannot be configured\n");

	reg = readl(ANATOP_BASE_ADDR + 0xF0);
	reg &= ~0x00003F00;
	reg |= 0x00001300;
	writel(reg, ANATOP_BASE_ADDR + 0xF4);

	reg = readl(CCM_BASE_ADDR + CLKCTL_CS2CDR);
	reg &= ~0x00007E00;
	reg |= 0x00003600;
	writel(reg, CCM_BASE_ADDR + CLKCTL_CS2CDR);

	reg = readl(CCM_BASE_ADDR + CLKCTL_CSCMR2);
	reg |= 0x00000C00;
	writel(reg, CCM_BASE_ADDR + CLKCTL_CSCMR2);

	reg = 0x0002A953;
	writel(reg, CCM_BASE_ADDR + CLKCTL_CHSCCDR);

	/*
	 * LVDS0 mux to IPU1 DI0.
	 * LVDS1 mux to IPU1 DI1.
	 */
	reg = readl(IOMUXC_BASE_ADDR + 0xC);
	reg &= ~(0x000003C0);
	reg |= 0x00000100;
	writel(reg, IOMUXC_BASE_ADDR + 0xC);

	if (di == 1)
		writel(0x40C, IOMUXC_BASE_ADDR + 0x8);
	else
		writel(0x201, IOMUXC_BASE_ADDR + 0x8);
}
#else /* CONFIG_LCD */
void __lcd_disable(void)
{
	unsigned int reg;

#if defined CONFIG_MX6Q
	/* GPIO backlight */
	mxc_iomux_v3_setup_pad(MX6Q_PAD_DISP0_DAT9__GPIO_4_30);
#elif defined CONFIG_MX6DL
	/* GPIO backlight */
	mxc_iomux_v3_setup_pad(MX6DL_PAD_DISP0_DAT9__GPIO_4_30);
#endif
	/* Set GPIO backlight to high. */
	reg = readl(GPIO4_BASE_ADDR + GPIO_GDIR);
	reg |= (1 << 30);
	writel(reg, GPIO4_BASE_ADDR + GPIO_GDIR);

	reg = readl(GPIO4_BASE_ADDR + GPIO_DR);
	reg &= ~(1 << 30);
	writel(reg, GPIO4_BASE_ADDR + GPIO_DR);
}
#endif

#ifdef CONFIG_VIDEO_MX5
void panel_info_init(void)
{
	panel_info.vl_bpix = LCD_BPP;
	panel_info.vl_col = lvds_wvga.xres;
	panel_info.vl_row = lvds_wvga.yres;
	panel_info.cmap = colormap;
}
#endif

#ifdef CONFIG_SPLASH_SCREEN
void setup_splash_image(void)
{
	char *s;
	ulong addr;

	s = getenv("splashimage");

	if (s != NULL) {
		addr = simple_strtoul(s, NULL, 16);

#if defined(CONFIG_ARCH_MMU)
		addr = ioremap_nocache(iomem_to_phys(addr),
				fsl_bmp_reversed_600x400_size);
#endif
		memcpy((char *)addr, (char *)fsl_bmp_reversed_600x400,
				fsl_bmp_reversed_600x400_size);
	}
}
#endif

int board_init(void)
{
/* need set Power Supply Glitch to 0x41736166
*and need clear Power supply Glitch Detect bit
* when POR or reboot or power on Otherwise system
*could not be power off anymore*/
	u32 reg;
	writel(0x41736166, SNVS_BASE_ADDR + 0x64);/*set LPPGDR*/
	udelay(10);
	reg = readl(SNVS_BASE_ADDR + 0x4c);
	reg |= (1 << 3);
	writel(reg, SNVS_BASE_ADDR + 0x4c);/*clear LPSR*/

	mxc_iomux_v3_init((void *)IOMUXC_BASE_ADDR);
	setup_boot_device();
	fsl_set_system_rev();

	/* board id for linux */
	gd->bd->bi_arch_number = MACH_TYPE_VAR_SOM_MX6;

	/* address of boot parameters */
	gd->bd->bi_boot_params = PHYS_SDRAM_1 + 0x100;

	setup_uart();

#ifndef CONFIG_LCD
	__lcd_disable();
#endif	

	if (cpu_is_mx6q())
		setup_sata();

#ifdef CONFIG_VIDEO_MX5
	panel_info_init();

	gd->fb_base = CONFIG_FB_BASE;
#ifdef CONFIG_ARCH_MMU
	gd->fb_base = ioremap_nocache(iomem_to_phys(gd->fb_base), 0);
#endif
#endif

#ifdef CONFIG_NAND_GPMI
	setup_gpmi_nand();
#endif

	return 0;
}

#ifdef CONFIG_ANDROID_RECOVERY

int check_recovery_cmd_file(void)
{
	int button_pressed = 0;
	int recovery_mode = 0;

	recovery_mode = check_and_clean_recovery_flag();

	/* Check Recovery Combo Button press or not. */
	mxc_iomux_v3_setup_pad(MX6X_IOMUX(PAD_CSI0_DATA_EN__GPIO_5_20));

	gpio_direction_input(GPIO_USER_KEY);

	if (gpio_get_value(GPIO_USER_KEY) == 0) { /* USER key is low assert */
		button_pressed = 1;
		printf("Recovery key pressed\n");
	}

	return recovery_mode || button_pressed;
}
#endif

int board_late_init(void)
{
#ifdef CONFIG_I2C_MXC
	int ret = 0;
	setup_i2c(CONFIG_SYS_I2C_PORT);
	setup_i2c(I2C3_BASE_ADDR);
	i2c_bus_recovery();
	ret = setup_pmic_voltages();
	if (ret)
		return -1;
#endif
	return 0;
}

#ifdef CONFIG_MXC_FEC
static int phy_read(char *devname, unsigned char addr, unsigned char reg,
		    unsigned short *pdata)
{
	int ret = miiphy_read(devname, addr, reg, pdata);
	if (ret)
		printf("Error reading from %s PHY addr=%02x reg=%02x\n",
		       devname, addr, reg);
	return ret;
}

static int phy_write(char *devname, unsigned char addr, unsigned char reg,
		     unsigned short value)
{
	int ret = miiphy_write(devname, addr, reg, value);
	if (ret)
		printf("Error writing to %s PHY addr=%02x reg=%02x\n", devname,
		       addr, reg);
	return ret;
}

int mx6_rgmii_rework(char *devname, int phy_addr)
{
	/* enable master mode, force phy to 100Mbps */
	phy_write(devname, phy_addr, 0x9, 0x1c00);

	/* min rx data delay */
	phy_write(devname, phy_addr, 0x0d, 0x2);
	phy_write(devname, phy_addr, 0x0e, 0x4);
	phy_write(devname, phy_addr, 0x0d, 0x4002);
	phy_write(devname, phy_addr, 0x0e, 0);

    phy_write(devname, phy_addr, 0x0d, 0x2);
    phy_write(devname, phy_addr, 0x0e, 0x5);
    phy_write(devname, phy_addr, 0x0d, 0x4002);
    phy_write(devname, phy_addr, 0x0e, 0);

    phy_write(devname, phy_addr, 0x0d, 0x2);
    phy_write(devname, phy_addr, 0x0e, 0x6);
    phy_write(devname, phy_addr, 0x0d, 0x4002);
    phy_write(devname, phy_addr, 0x0e, 0);

    phy_write(devname, phy_addr, 0x0d, 0x2);
    phy_write(devname, phy_addr, 0x0e, 0x8);
    phy_write(devname, phy_addr, 0x0d, 0x4002);
    phy_write(devname, phy_addr, 0x0e, 0x3ff);
	/* max rx/tx clock delay, min rx/tx control delay */
	phy_write(devname, phy_addr, 0x0b, 0x8104);
	phy_write(devname, phy_addr, 0x0c, 0xf0f0);
	phy_write(devname, phy_addr, 0x0b, 0x104);
	return 0;
}

iomux_v3_cfg_t enet_pads_qd[] = {
	MX6Q_PAD_ENET_MDIO__ENET_MDIO,
	MX6Q_PAD_ENET_MDC__ENET_MDC,
	MX6Q_PAD_RGMII_TXC__ENET_RGMII_TXC,
	MX6Q_PAD_RGMII_TD0__ENET_RGMII_TD0,
	MX6Q_PAD_RGMII_TD1__ENET_RGMII_TD1,
	MX6Q_PAD_RGMII_TD2__ENET_RGMII_TD2,
	MX6Q_PAD_RGMII_TD3__ENET_RGMII_TD3,
	MX6Q_PAD_RGMII_TX_CTL__ENET_RGMII_TX_CTL,
	/* pin 35 - 1 (PHY_AD2) on reset */
	MX6Q_PAD_RGMII_RXC__GPIO_6_30,
	/* pin 32 - 1 - (MODE0) all */
	MX6Q_PAD_RGMII_RD0__GPIO_6_25,
	/* pin 31 - 1 - (MODE1) all */
	MX6Q_PAD_RGMII_RD1__GPIO_6_27,
	/* pin 28 - 1 - (MODE2) all */
	MX6Q_PAD_RGMII_RD2__GPIO_6_28,
	/* pin 27 - 1 - (MODE3) all */
	MX6Q_PAD_RGMII_RD3__GPIO_6_29,
	/* pin 33 - 1 - (CLK125_EN) 125Mhz clockout enabled */
	MX6Q_PAD_RGMII_RX_CTL__GPIO_6_24,
	MX6Q_PAD_GPIO_0__CCM_CLKO,
	MX6Q_PAD_GPIO_3__CCM_CLKO2,
	MX6Q_PAD_ENET_REF_CLK__ENET_TX_CLK,
};

iomux_v3_cfg_t enet_pads_final_qd[] = {
	MX6Q_PAD_RGMII_RXC__ENET_RGMII_RXC,
	MX6Q_PAD_RGMII_RD0__ENET_RGMII_RD0,
	MX6Q_PAD_RGMII_RD1__ENET_RGMII_RD1,
	MX6Q_PAD_RGMII_RD2__ENET_RGMII_RD2,
	MX6Q_PAD_RGMII_RD3__ENET_RGMII_RD3,
	MX6Q_PAD_RGMII_RX_CTL__ENET_RGMII_RX_CTL,
};

iomux_v3_cfg_t enet_pads_dl[] = {
	MX6DL_PAD_ENET_MDIO__ENET_MDIO,
	MX6DL_PAD_ENET_MDC__ENET_MDC,
	MX6DL_PAD_RGMII_TXC__ENET_RGMII_TXC,
	MX6DL_PAD_RGMII_TD0__ENET_RGMII_TD0,
	MX6DL_PAD_RGMII_TD1__ENET_RGMII_TD1,
	MX6DL_PAD_RGMII_TD2__ENET_RGMII_TD2,
	MX6DL_PAD_RGMII_TD3__ENET_RGMII_TD3,
	MX6DL_PAD_RGMII_TX_CTL__ENET_RGMII_TX_CTL,
	/* pin 35 - 1 (PHY_AD2) on reset */
	MX6DL_PAD_RGMII_RXC__GPIO_6_30,
	/* pin 32 - 1 - (MODE0) all */
	MX6DL_PAD_RGMII_RD0__GPIO_6_25,
	/* pin 31 - 1 - (MODE1) all */
	MX6DL_PAD_RGMII_RD1__GPIO_6_27,
	/* pin 28 - 1 - (MODE2) all */
	MX6DL_PAD_RGMII_RD2__GPIO_6_28,
	/* pin 27 - 1 - (MODE3) all */
	MX6DL_PAD_RGMII_RD3__GPIO_6_29,
	/* pin 33 - 1 - (CLK125_EN) 125Mhz clockout enabled */
	MX6DL_PAD_RGMII_RX_CTL__GPIO_6_24,
	MX6DL_PAD_GPIO_0__CCM_CLKO,
	MX6DL_PAD_GPIO_3__CCM_CLKO2,
	MX6DL_PAD_ENET_REF_CLK__ENET_TX_CLK,
};

iomux_v3_cfg_t enet_pads_final_dl[] = {
	MX6DL_PAD_RGMII_RXC__ENET_RGMII_RXC,
	MX6DL_PAD_RGMII_RD0__ENET_RGMII_RD0,
	MX6DL_PAD_RGMII_RD1__ENET_RGMII_RD1,
	MX6DL_PAD_RGMII_RD2__ENET_RGMII_RD2,
	MX6DL_PAD_RGMII_RD3__ENET_RGMII_RD3,
	MX6DL_PAD_RGMII_RX_CTL__ENET_RGMII_RX_CTL,
};


void enet_board_init(void)
{
	unsigned int reg;
	iomux_v3_cfg_t enet_reset;

		if (mx6_chip_is_dq())
		enet_reset = (_MX6Q_PAD_ENET_CRS_DV__GPIO_1_25 &
				~MUX_PAD_CTRL_MASK)           |
			 	MUX_PAD_CTRL(0x88);
		else
	        enet_reset =(MX6DL_PAD_ENET_CRS_DV__GPIO_1_25 &
				~MUX_PAD_CTRL_MASK)           |
			 	MUX_PAD_CTRL(0x88);

	/* phy reset */
	set_gpio_output_val(GPIO1_BASE_ADDR, (1 << 25), 0);
	set_gpio_output_val(GPIO6_BASE_ADDR, (1 << 30), 1);
	set_gpio_output_val(GPIO6_BASE_ADDR, (1 << 25), 1);
	set_gpio_output_val(GPIO6_BASE_ADDR, (1 << 27), 1);
	set_gpio_output_val(GPIO6_BASE_ADDR, (1 << 28), 1);
	set_gpio_output_val(GPIO6_BASE_ADDR, (1 << 29), 1);

		if (mx6_chip_is_dq())
			mxc_iomux_v3_setup_multiple_pads(enet_pads_qd, ARRAY_SIZE(enet_pads_qd));
		else
			mxc_iomux_v3_setup_multiple_pads(enet_pads_dl, ARRAY_SIZE(enet_pads_dl));

	mxc_iomux_v3_setup_pad(enet_reset);

	set_gpio_output_val(GPIO6_BASE_ADDR, (1 << 24), 1);

	udelay(500);

	// De-assert reset
	set_gpio_output_val(GPIO1_BASE_ADDR, (1 << 25), 1);

		if (mx6_chip_is_dq())
			mxc_iomux_v3_setup_multiple_pads(enet_pads_final_qd, ARRAY_SIZE(enet_pads_final_qd));
		else
			mxc_iomux_v3_setup_multiple_pads(enet_pads_final_dl, ARRAY_SIZE(enet_pads_final_dl));
}
#endif

int checkboard(void)
{
	printf("Board: %s-VAR_SOM: %s Board: 0x%x [",
	mx6_chip_name(),
	mx6_board_rev_name(),
	fsl_system_rev);

	switch (__REG(SRC_BASE_ADDR + 0x8)) {
	case 0x0001:
		printf("POR");
		break;
	case 0x0009:
		printf("RST");
		break;
	case 0x0010:
	case 0x0011:
		printf("WDOG");
		break;
	default:
		printf("unknown");
	}
	printf(" ]\n");

	printf("Boot Device: ");
	switch (get_boot_device()) {
	case WEIM_NOR_BOOT:
		printf("NOR\n");
		break;
	case ONE_NAND_BOOT:
		printf("ONE NAND\n");
		break;
	case PATA_BOOT:
		printf("PATA\n");
		break;
	case SATA_BOOT:
		printf("SATA\n");
		break;
	case I2C_BOOT:
		printf("I2C\n");
		break;
	case SPI_NOR_BOOT:
		printf("SPI NOR\n");
		break;
	case SD_BOOT:
		printf("SD\n");
		break;
	case MMC_BOOT:
		printf("MMC\n");
		break;
	case NAND_BOOT:
		printf("NAND\n");
		break;
	case UNKNOWN_BOOT:
	default:
		printf("UNKNOWN\n");
		break;
	}

#ifdef CONFIG_SECURE_BOOT
	if (check_hab_enable() == 1)
		get_hab_status();
#endif
	return 0;
}


#ifdef CONFIG_IMX_UDC

void udc_pins_setting(void)
{
	mxc_iomux_set_gpr_register(1, 13, 1, 0);
}
#endif
