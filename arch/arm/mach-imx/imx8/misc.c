// SPDX-License-Identifier: GPL-2.0+
#include <common.h>
#include <asm/arch/sci/sci.h>
#include <asm/mach-imx/sys_proto.h>
#include <generated/version_autogenerated.h>
#include <linux/libfdt.h>
#include <asm/arch/lpcg.h>
#include <linux/psci.h>
#include <dm.h>

DECLARE_GLOBAL_DATA_PTR;

int sc_pm_setup_uart(sc_rsrc_t uart_rsrc, sc_pm_clock_rate_t clk_rate)
{
	sc_pm_clock_rate_t rate = clk_rate;
	int ret;

	if (uart_rsrc < SC_R_UART_0 || uart_rsrc > SC_R_UART_4)
		return -EINVAL;

	/* Power up UARTn */
	ret = sc_pm_set_resource_power_mode(-1, uart_rsrc, SC_PM_PW_MODE_ON);
	if (ret)
		return ret;

	/* Set UARTn clock root to 'rate' MHz */
	ret = sc_pm_set_clock_rate(-1, uart_rsrc, SC_PM_CLK_PER, &rate);
	if (ret)
		return ret;

	/* Enable UARTn clock root */
	ret = sc_pm_clock_enable(-1, uart_rsrc, SC_PM_CLK_PER, true, false);
	if (ret)
		return ret;

	lpcg_all_clock_on(LPUART_0_LPCG + (uart_rsrc - SC_R_UART_0) * 0x10000);

	return 0;
}

#define FSL_SIP_BUILDINFO			0xC2000003
#define FSL_SIP_BUILDINFO_GET_COMMITHASH	0x00
extern uint32_t _end_ofs;

#define V2X_PROD_VER(X)      (((X) >> 16) & 0x7FFF)
#define V2X_MAJOR_VER(X)     (((X) >> 4) & 0xFFF)
#define V2X_MINOR_VER(X)     ((X) & 0xF)

static void set_buildinfo_to_env(uint32_t scfw, uint32_t secofw, char *mkimage, char *atf)
{
	if (!mkimage || !atf)
		return;

	env_set("commit_mkimage", mkimage);
	env_set("commit_atf", atf);
	env_set_hex("commit_scfw", (ulong)scfw);
	env_set_hex("commit_secofw", (ulong)secofw);
}

static void set_v2x_buildinfo_to_env(u32 v2x_build, u32 v2x_commit)
{
	env_set_hex("commit_v2x", (ulong)v2x_commit);
	env_set_hex("version_v2x", (ulong)v2x_build);
}

void build_info(void)
{
	u32 seco_build = 0, seco_commit = 0;
	u32 sc_build = 0, sc_commit = 0;
	char *mkimage_commit, *temp;
	ulong atf_commit = 0;
	u32 v2x_build = 0, v2x_commit = 0;

	/* Get SCFW build and commit id */
	sc_misc_build_info(-1, &sc_build, &sc_commit);
	if (!sc_build) {
		printf("SCFW does not support build info\n");
		sc_commit = 0; /* Display 0 if build info not supported */
	}

	/* Get SECO FW build and commit id */
	sc_seco_build_info(-1, &seco_build, &seco_commit);
	if (!seco_build) {
		debug("SECO FW does not support build info\n");
		/* Display 0 when the build info is not supported */
		seco_commit = 0;
	}

	if (is_imx8dxl()) {
		int ret;
		ret = sc_seco_v2x_build_info(-1, &v2x_build, &v2x_commit);
		if (ret) {
			debug("Failed to get V2X FW build info\n");
			/* Display 0 when the build info is not supported */
			v2x_build = 0;
			v2x_commit = 0;
		}
	}

	/* Get imx-mkimage commit id.
	 * The imx-mkimage puts the commit hash behind the end of u-boot.bin
	 */
	mkimage_commit = (char *)(ulong)(CONFIG_SYS_TEXT_BASE +
		_end_ofs + fdt_totalsize(gd->fdt_blob));
	temp = mkimage_commit + 8;
	*temp = '\0';

	if (strlen(mkimage_commit) == 0) {
		debug("IMX-MKIMAGE does not support build info\n");
		mkimage_commit = "0"; /* Display 0 */
	}

	/* Get ARM Trusted Firmware commit id */
	atf_commit = call_imx_sip(FSL_SIP_BUILDINFO,
				  FSL_SIP_BUILDINFO_GET_COMMITHASH, 0, 0, 0);
	if (atf_commit == 0xffffffff) {
		debug("ATF does not support build info\n");
		atf_commit = 0x30; /* Display 0 */
	}

	/* Set all to env */
	set_buildinfo_to_env(sc_commit, seco_commit, mkimage_commit, (char *)&atf_commit);

	printf("\n BuildInfo: \n  - SCFW %08x, SECO-FW %08x, IMX-MKIMAGE %s, ATF %s\n  - %s \n",
		sc_commit, seco_commit, mkimage_commit, (char *)&atf_commit, U_BOOT_VERSION);

	if (is_imx8dxl() && v2x_build != 0 && v2x_commit != 0) {
		set_v2x_buildinfo_to_env(v2x_build, v2x_commit);
		printf("  - V2X-FW %08x version %u.%u.%u\n", v2x_commit,
			V2X_PROD_VER(v2x_build), V2X_MAJOR_VER(v2x_build), V2X_MINOR_VER(v2x_build));
	}
	printf("\n");
}

#if !defined(CONFIG_SPL_BUILD) && defined(CONFIG_PSCI_BOARD_REBOOT)

#define PSCI_SYSTEM_RESET2_AARCH64		0xc4000012
#define PSCI_RESET2_SYSTEM_BOARD_RESET		0x80000002

int do_board_reboot(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	struct udevice *dev;

	uclass_get_device_by_name(UCLASS_FIRMWARE, "psci", &dev);
	invoke_psci_fn(PSCI_SYSTEM_RESET2_AARCH64, PSCI_RESET2_SYSTEM_BOARD_RESET, 0, 0);

	return 1;
}

U_BOOT_CMD(
	reboot,	1,	1,	do_board_reboot,
	"reboot\n",
	"system board reboot for i.MX 8 Quad devices \n"
);
#endif
