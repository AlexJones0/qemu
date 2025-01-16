/*
 * QEMU RISC-V Board Compatible with OpenTitan EarlGrey FPGA platform
 *
 * Copyright (c) 2022-2024 Rivos, Inc.
 *
 * Author(s):
 *  Emmanuel Blot <eblot@rivosinc.com>
 *  Loïc Lefort <loic@rivosinc.com>
 *
 * This implementation is based on OpenTitan RTL version:
 *  <lowRISC/opentitan@caa3bd0a14ddebbf60760490f7c917901482c8fd>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qlist.h"
#include "exec/address-spaces.h"
#include "hw/boards.h"
#include "hw/intc/sifive_plic.h"
#include "hw/jtag/tap_ctrl.h"
#include "hw/jtag/tap_ctrl_rbb.h"
#include "hw/misc/pulp_rv_dm.h"
#include "hw/misc/unimp.h"
#include "hw/opentitan/ot_aes.h"
#include "hw/opentitan/ot_alert.h"
#include "hw/opentitan/ot_aon_timer.h"
#include "hw/opentitan/ot_ast_eg.h"
#include "hw/opentitan/ot_clkmgr.h"
#include "hw/opentitan/ot_common.h"
#include "hw/opentitan/ot_csrng.h"
#include "hw/opentitan/ot_edn.h"
#include "hw/opentitan/ot_entropy_src.h"
#include "hw/opentitan/ot_flash.h"
#include "hw/opentitan/ot_gpio_eg.h"
#include "hw/opentitan/ot_hmac.h"
#include "hw/opentitan/ot_ibex_wrapper_eg.h"
#include "hw/opentitan/ot_kmac.h"
#include "hw/opentitan/ot_lc_ctrl.h"
#include "hw/opentitan/ot_otbn.h"
#include "hw/opentitan/ot_otp_eg.h"
#include "hw/opentitan/ot_otp_ot_be.h"
#include "hw/opentitan/ot_pinmux_eg.h"
#include "hw/opentitan/ot_plic_ext.h"
#include "hw/opentitan/ot_pwrmgr.h"
#include "hw/opentitan/ot_rom_ctrl.h"
#include "hw/opentitan/ot_rstmgr.h"
#include "hw/opentitan/ot_sensor.h"
#include "hw/opentitan/ot_spi_device.h"
#include "hw/opentitan/ot_spi_host.h"
#include "hw/opentitan/ot_sram_ctrl.h"
#include "hw/opentitan/ot_timer.h"
#include "hw/opentitan/ot_uart.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/dm.h"
#include "hw/riscv/dtm.h"
#include "hw/riscv/ibex_common.h"
#include "hw/riscv/ot_earlgrey.h"
#include "hw/ssi/ssi.h"
#include "sysemu/blockdev.h"
#include "sysemu/hw_accel.h"
#include "sysemu/sysemu.h"

/* ------------------------------------------------------------------------ */
/* Forward Declarations */
/* ------------------------------------------------------------------------ */

static void ot_eg_soc_dm_configure(DeviceState *dev, const IbexDeviceDef *def,
                                   DeviceState *parent);
static void ot_eg_soc_flash_ctrl_configure(
    DeviceState *dev, const IbexDeviceDef *def, DeviceState *parent);
static void ot_eg_soc_hart_configure(DeviceState *dev, const IbexDeviceDef *def,
                                     DeviceState *parent);
static void ot_eg_soc_otp_ctrl_configure(
    DeviceState *dev, const IbexDeviceDef *def, DeviceState *parent);
static void ot_eg_soc_tap_ctrl_configure(
    DeviceState *dev, const IbexDeviceDef *def, DeviceState *parent);
static void ot_eg_soc_spi_device_configure(
    DeviceState *dev, const IbexDeviceDef *def, DeviceState *parent);
static void ot_eg_soc_uart_configure(DeviceState *dev, const IbexDeviceDef *def,
                                     DeviceState *parent);

/* ------------------------------------------------------------------------ */
/* Constants */
/* ------------------------------------------------------------------------ */

enum OtEGSocDevice {
    OT_EG_SOC_DEV_ADC_CTRL,
    OT_EG_SOC_DEV_AES,
    OT_EG_SOC_DEV_ALERT_HANDLER,
    OT_EG_SOC_DEV_AON_TIMER,
    OT_EG_SOC_DEV_AST,
    OT_EG_SOC_DEV_CLKMGR,
    OT_EG_SOC_DEV_CSRNG,
    OT_EG_SOC_DEV_DM,
    OT_EG_SOC_DEV_DTM,
    OT_EG_SOC_DEV_EDN0,
    OT_EG_SOC_DEV_EDN1,
    OT_EG_SOC_DEV_ENTROPY_SRC,
    OT_EG_SOC_DEV_FLASH_CTRL,
    OT_EG_SOC_DEV_GPIO,
    OT_EG_SOC_DEV_HART,
    OT_EG_SOC_DEV_HMAC,
    OT_EG_SOC_DEV_I2C0,
    OT_EG_SOC_DEV_I2C1,
    OT_EG_SOC_DEV_I2C2,
    OT_EG_SOC_DEV_IBEX_WRAPPER,
    OT_EG_SOC_DEV_KEYMGR,
    OT_EG_SOC_DEV_KMAC,
    OT_EG_SOC_DEV_LC_CTRL,
    OT_EG_SOC_DEV_OTBN,
    OT_EG_SOC_DEV_OTP_CTRL,
    OT_EG_SOC_DEV_OTP_BACKEND,
    OT_EG_SOC_DEV_PATTGEN,
    OT_EG_SOC_DEV_PINMUX,
    OT_EG_SOC_DEV_PLIC,
    OT_EG_SOC_DEV_PLIC_EXT,
    OT_EG_SOC_DEV_PWM,
    OT_EG_SOC_DEV_PWRMGR,
    OT_EG_SOC_DEV_SRAM_RET_CTRL,
    OT_EG_SOC_DEV_ROM_CTRL,
    OT_EG_SOC_DEV_RSTMGR,
    OT_EG_SOC_DEV_RV_DM,
    OT_EG_SOC_DEV_SENSOR_CTRL,
    OT_EG_SOC_DEV_SPI_DEVICE,
    OT_EG_SOC_DEV_SPI_HOST0,
    OT_EG_SOC_DEV_SPI_HOST1,
    OT_EG_SOC_DEV_SRAM_MAIN_CTRL,
    OT_EG_SOC_DEV_SYSRST_CTRL,
    OT_EG_SOC_DEV_TAP_CTRL,
    OT_EG_SOC_DEV_TIMER,
    OT_EG_SOC_DEV_UART0,
    OT_EG_SOC_DEV_UART1,
    OT_EG_SOC_DEV_UART2,
    OT_EG_SOC_DEV_UART3,
    OT_EG_SOC_DEV_USBDEV,
};

enum OtEgResetRequest {
    OT_EG_RESET_SYSRST_CTRL,
    OT_EG_RESET_AON_TIMER,
    OT_EG_RESET_SENSOR_CTRL,
    OT_EG_RESET_COUNT
};

/* EarlGrey/CW310 Peripheral clock is 6 MHz */
#define OT_EG_PERIPHERAL_CLK_HZ 6000000u

/* EarlGrey/CW310 AON clock is 250 kHz */
#define OT_EG_AON_CLK_HZ 250000u

static const uint8_t ot_eg_pmp_cfgs[] = {
    /* clang-format off */
    IBEX_PMP_CFG(0, IBEX_PMP_MODE_OFF, 0, 0, 0),
    IBEX_PMP_CFG(0, IBEX_PMP_MODE_OFF, 0, 0, 0),
    IBEX_PMP_CFG(1, IBEX_PMP_MODE_NAPOT, 1, 0, 1), /* rgn 2  [ROM: LRX] */
    IBEX_PMP_CFG(0, IBEX_PMP_MODE_OFF, 0, 0, 0),
    IBEX_PMP_CFG(0, IBEX_PMP_MODE_OFF, 0, 0, 0),
    IBEX_PMP_CFG(0, IBEX_PMP_MODE_OFF, 0, 0, 0),
    IBEX_PMP_CFG(0, IBEX_PMP_MODE_OFF, 0, 0, 0),
    IBEX_PMP_CFG(0, IBEX_PMP_MODE_OFF, 0, 0, 0),
    IBEX_PMP_CFG(0, IBEX_PMP_MODE_OFF, 0, 0, 0),
    IBEX_PMP_CFG(0, IBEX_PMP_MODE_OFF, 0, 0, 0),
    IBEX_PMP_CFG(0, IBEX_PMP_MODE_OFF, 0, 0, 0),
    IBEX_PMP_CFG(1, IBEX_PMP_MODE_TOR, 0, 1, 1), /* rgn 11 [MMIO: LRW] */
    IBEX_PMP_CFG(0, IBEX_PMP_MODE_OFF, 0, 0, 0),
    IBEX_PMP_CFG(1, IBEX_PMP_MODE_NAPOT, 1, 1, 1), /* rgn 13 [DV_ROM: LRWX] */
    IBEX_PMP_CFG(0, IBEX_PMP_MODE_OFF, 0, 0, 0),
    IBEX_PMP_CFG(0, IBEX_PMP_MODE_OFF, 0, 0, 0)
    /* clang-format on */
};

static const uint32_t ot_eg_pmp_addrs[] = {
    /* clang-format off */
    IBEX_PMP_ADDR(0x00000000),
    IBEX_PMP_ADDR(0x00000000),
    IBEX_PMP_ADDR(0x000083fc), /* rgn 2 [ROM: base=0x0000_8000 sz (2KiB)] */
    IBEX_PMP_ADDR(0x00000000),
    IBEX_PMP_ADDR(0x00000000),
    IBEX_PMP_ADDR(0x00000000),
    IBEX_PMP_ADDR(0x00000000),
    IBEX_PMP_ADDR(0x00000000),
    IBEX_PMP_ADDR(0x00000000),
    IBEX_PMP_ADDR(0x00000000),
    IBEX_PMP_ADDR(0x40000000), /* rgn 10 [MMIO: lo=0x4000_0000] */
    IBEX_PMP_ADDR(0x42010000), /* rgn 11 [MMIO: hi=0x4201_0000] */
    IBEX_PMP_ADDR(0x00000000),
    IBEX_PMP_ADDR(0x000107fc), /* rgn 13 [DV_ROM: base=0x0001_0000 sz (4KiB)] */
    IBEX_PMP_ADDR(0x00000000),
    IBEX_PMP_ADDR(0x00000000)
    /* clang-format on */
};

#define OT_EG_MSECCFG IBEX_MSECCFG(1, 1, 0)

#define OT_EG_SOC_RST_REQ TYPE_RISCV_OT_EG_SOC "-reset"

#define OT_EG_SOC_GPIO(_irq_, _target_, _num_) \
    IBEX_GPIO(_irq_, OT_EG_SOC_DEV_##_target_, _num_)

#define OT_EG_SOC_GPIO_SYSBUS_IRQ(_irq_, _target_, _num_) \
    IBEX_GPIO_SYSBUS_IRQ(_irq_, OT_EG_SOC_DEV_##_target_, _num_)

#define OT_EG_SOC_DEVLINK(_pname_, _target_) \
    IBEX_DEVLINK(_pname_, OT_EG_SOC_DEV_##_target_)

#define OT_EG_SOC_SIGNAL(_sname_, _snum_, _tgt_, _tname_, _tnum_) \
    { \
        .out = { \
            .name = (_sname_), \
            .num = (_snum_), \
        }, \
        .in = { \
            .name = (_tname_), \
            .index = (OT_EG_SOC_DEV_ ## _tgt_), \
            .num = (_tnum_), \
        } \
    }

/* Request link */
#define OT_EG_SOC_REQ(_req_, _tgt_) \
    OT_EG_SOC_SIGNAL(_req_##_REQ, 0, _tgt_, _req_##_REQ, 0)

/* Response link */
#define OT_EG_SOC_RSP(_rsp_, _tgt_) \
    OT_EG_SOC_SIGNAL(_rsp_##_RSP, 0, _tgt_, _rsp_##_RSP, 0)


#define OT_EG_SOC_CLKMGR_HINT(_num_) \
    OT_EG_SOC_SIGNAL(OT_CLOCK_ACTIVE, 0, CLKMGR, OT_CLKMGR_HINT, _num_)

#define OT_EG_SOC_DM_CONNECTION(_dst_dev_, _num_) \
    { \
        .out = { \
            .name = PULP_RV_DM_ACK_OUT_LINES, \
            .num = (_num_), \
        }, \
        .in = { \
            .name = RISCV_DM_ACK_LINES, \
            .index = (_dst_dev_), \
            .num = (_num_), \
        } \
    }

/* Earlgrey M2.5.2-RC0 RV DM */
#define EG_TAP_IDCODE IBEX_JTAG_IDCODE(0, 1, 0)

#define PULP_DM_BASE   0x00010000u
#define SRAM_MAIN_SIZE 0x20000u

/*
 * MMIO/interrupt mapping as per:
 * lowRISC/opentitan: hw/top_earlgrey/sw/autogen/top_earlgrey_memory.h
 * and
 * lowRISC/opentitan: hw/top_earlgrey/sw/autogen/top_earlgrey.h
 */
static const IbexDeviceDef ot_eg_soc_devices[] = {
    /* clang-format off */
    [OT_EG_SOC_DEV_HART] = {
        .type = TYPE_RISCV_CPU_LOWRISC_OPENTITAN,
        .cfg = &ot_eg_soc_hart_configure,
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("resetvec", 0x8080u),
            IBEX_DEV_UINT_PROP("mtvec", 0x8001u),
            IBEX_DEV_UINT_PROP("dmhaltvec", PULP_DM_BASE +
                PULP_RV_DM_ROM_BASE + PULP_RV_DM_HALT_OFFSET),
            IBEX_DEV_UINT_PROP("dmexcpvec", PULP_DM_BASE +
                PULP_RV_DM_ROM_BASE + PULP_RV_DM_EXCEPTION_OFFSET),
            IBEX_DEV_BOOL_PROP("start-powered-off", true)
        ),
    },
    [OT_EG_SOC_DEV_TAP_CTRL] = {
        .type = TYPE_TAP_CTRL_RBB,
        .cfg = &ot_eg_soc_tap_ctrl_configure,
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("ir_length", IBEX_TAP_IR_LENGTH),
            IBEX_DEV_UINT_PROP("idcode", EG_TAP_IDCODE)
        ),
    },
    [OT_EG_SOC_DEV_DTM] = {
        .type = TYPE_RISCV_DTM,
        .link = IBEXDEVICELINKDEFS(
            OT_EG_SOC_DEVLINK("tap_ctrl", TAP_CTRL)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("abits", 7u)
        ),
    },
    [OT_EG_SOC_DEV_DM] = {
        .type = TYPE_RISCV_DM,
        .cfg = &ot_eg_soc_dm_configure,
        .link = IBEXDEVICELINKDEFS(
            OT_EG_SOC_DEVLINK("dtm", DTM)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("nscratch", PULP_RV_DM_NSCRATCH_COUNT),
            IBEX_DEV_UINT_PROP("progbuf_count",
                PULP_RV_DM_PROGRAM_BUFFER_COUNT),
            IBEX_DEV_UINT_PROP("data_count", PULP_RV_DM_DATA_COUNT),
            IBEX_DEV_UINT_PROP("abstractcmd_count",
                PULP_RV_DM_ABSTRACTCMD_COUNT),
            IBEX_DEV_UINT_PROP("dm_phyaddr", PULP_DM_BASE),
            IBEX_DEV_UINT_PROP("rom_phyaddr",
                PULP_DM_BASE + PULP_RV_DM_ROM_BASE),
            IBEX_DEV_UINT_PROP("whereto_phyaddr",
                PULP_DM_BASE + PULP_RV_DM_WHERETO_OFFSET),
            IBEX_DEV_UINT_PROP("data_phyaddr",
                PULP_DM_BASE + PULP_RV_DM_DATAADDR_OFFSET),
            IBEX_DEV_UINT_PROP("progbuf_phyaddr",
                PULP_DM_BASE + PULP_RV_DM_PROGRAM_BUFFER_OFFSET),
            IBEX_DEV_UINT_PROP("resume_offset", PULP_RV_DM_RESUME_OFFSET),
            IBEX_DEV_BOOL_PROP("sysbus_access", true),
            IBEX_DEV_BOOL_PROP("abstractauto", true)
        ),
    },
    [OT_EG_SOC_DEV_UART0] = {
        .type = TYPE_OT_UART,
        .cfg = &ot_eg_soc_uart_configure,
        .instance = IBEX_MAKE_INSTANCE_NUM(0),
        .memmap = MEMMAPENTRIES(
            { .base = 0x40000000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 1),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(1, PLIC, 2),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(2, PLIC, 3),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(3, PLIC, 4),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(4, PLIC, 5),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(5, PLIC, 6),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(6, PLIC, 7),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(7, PLIC, 8),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(8, PLIC, 9)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("pclk", OT_EG_PERIPHERAL_CLK_HZ)
        ),
    },
    [OT_EG_SOC_DEV_UART1] = {
        .type = TYPE_OT_UART,
        .cfg = &ot_eg_soc_uart_configure,
        .instance = IBEX_MAKE_INSTANCE_NUM(1),
        .memmap = MEMMAPENTRIES(
            { .base = 0x40010000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 10),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(1, PLIC, 11),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(2, PLIC, 12),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(3, PLIC, 13),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(4, PLIC, 14),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(5, PLIC, 15),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(6, PLIC, 16),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(7, PLIC, 17),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(8, PLIC, 18)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("pclk", OT_EG_PERIPHERAL_CLK_HZ)
        ),
    },
    [OT_EG_SOC_DEV_UART2] = {
        .type = TYPE_OT_UART,
        .cfg = &ot_eg_soc_uart_configure,
        .instance = IBEX_MAKE_INSTANCE_NUM(2),
        .memmap = MEMMAPENTRIES(
            { .base = 0x40020000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 19),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(1, PLIC, 20),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(2, PLIC, 21),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(3, PLIC, 22),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(4, PLIC, 23),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(5, PLIC, 24),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(6, PLIC, 25),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(7, PLIC, 26),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(8, PLIC, 27)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("pclk", OT_EG_PERIPHERAL_CLK_HZ)
        ),
    },
    [OT_EG_SOC_DEV_UART3] = {
        .type = TYPE_OT_UART,
        .cfg = &ot_eg_soc_uart_configure,
        .instance = IBEX_MAKE_INSTANCE_NUM(3),
        .memmap = MEMMAPENTRIES(
            { .base = 0x40030000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 28),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(1, PLIC, 29),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(2, PLIC, 30),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(3, PLIC, 31),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(4, PLIC, 32),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(5, PLIC, 33),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(6, PLIC, 34),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(7, PLIC, 35),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(8, PLIC, 36)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("pclk", OT_EG_PERIPHERAL_CLK_HZ)
        ),
    },
    [OT_EG_SOC_DEV_GPIO] = {
        .type = TYPE_OT_GPIO_EG,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40040000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 37),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(1, PLIC, 38),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(2, PLIC, 39),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(3, PLIC, 40),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(4, PLIC, 41),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(5, PLIC, 42),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(6, PLIC, 43),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(7, PLIC, 44),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(8, PLIC, 45),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(9, PLIC, 46),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(10, PLIC, 47),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(11, PLIC, 48),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(12, PLIC, 49),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(13, PLIC, 50),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(14, PLIC, 51),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(15, PLIC, 52),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(16, PLIC, 53),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(17, PLIC, 54),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(18, PLIC, 55),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(19, PLIC, 56),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(20, PLIC, 57),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(21, PLIC, 58),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(22, PLIC, 59),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(23, PLIC, 60),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(24, PLIC, 61),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(25, PLIC, 62),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(26, PLIC, 63),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(27, PLIC, 64),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(28, PLIC, 65),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(29, PLIC, 66),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(30, PLIC, 67),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(31, PLIC, 68)
        )
    },
    [OT_EG_SOC_DEV_SPI_DEVICE] = {
        .type = TYPE_OT_SPI_DEVICE,
        .cfg = &ot_eg_soc_spi_device_configure,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40050000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 69),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(1, PLIC, 70),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(2, PLIC, 71),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(3, PLIC, 72),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(4, PLIC, 73),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(5, PLIC, 74),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(6, PLIC, 75),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(7, PLIC, 76)
        ),
    },
    [OT_EG_SOC_DEV_I2C0] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-i2c",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40080000u }
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("size", 0x80u)
        )
    },
    [OT_EG_SOC_DEV_I2C1] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-i2c",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40090000u }
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("size", 0x80u)
        )
    },
    [OT_EG_SOC_DEV_I2C2] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-i2c",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { .base = 0x400a0000u }
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("size", 0x80u)
        )
    },
    [OT_EG_SOC_DEV_PATTGEN] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-pattgen",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { .base = 0x400e0000u }
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("size", 0x80u)
        )
    },
    [OT_EG_SOC_DEV_TIMER] = {
        .type = TYPE_OT_TIMER,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40100000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO(0, HART, IRQ_M_TIMER),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 124)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("pclk", OT_EG_PERIPHERAL_CLK_HZ)
        ),
    },
    [OT_EG_SOC_DEV_OTP_CTRL] = {
        .type = TYPE_OT_OTP_EG,
        .cfg = &ot_eg_soc_otp_ctrl_configure,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40130000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 125),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(1, PLIC, 126)
        ),
        .link = IBEXDEVICELINKDEFS(
            OT_EG_SOC_DEVLINK("edn", EDN0),
            OT_EG_SOC_DEVLINK("backend", OTP_BACKEND)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("edn-ep", 1u)
            // Scrambling support is yet to be backported from Darjeeling
            // implementation. Those are the Earlgrey constants defined for
            // Earlgrey-M2.5.2-RC0:
            // IBEX_DEV_STRING_PROP("scrmbl_key",
            //     "605fefe9977b00b6fdc21d577a172d04"
            //     "7dcf0eebbdd268afd4e2506df1d0603f"),
            // IBEX_DEV_STRING_PROP("digest_const",
            //     "30faa0c47e3809585a24109fbc53e920"),
            // IBEX_DEV_STRING_PROP("digest_iv",
            //     "af12b341a53780ab"),
            // IBEX_DEV_STRING_PROP("sram_const",
            //     "5f2c075769000c39cda36eab93cd263d"),
            // IBEX_DEV_STRING_PROP("sram_iv",
            //     "f2dae31d857d1d39")
        ),
    },
    [OT_EG_SOC_DEV_OTP_BACKEND] = {
        .type = TYPE_OT_OTP_OT_BE,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40132000u }
        ),
        .link = IBEXDEVICELINKDEFS(
            OT_EG_SOC_DEVLINK("parent", OTP_CTRL)
        ),
    },
    [OT_EG_SOC_DEV_LC_CTRL] = {
        .type = TYPE_OT_LC_CTRL,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40140000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_RSP(OT_PWRMGR_LC, PWRMGR)
        ),
        .link = IBEXDEVICELINKDEFS(
            OT_EG_SOC_DEVLINK("otp_ctrl", OTP_CTRL),
            OT_EG_SOC_DEVLINK("kmac", KMAC)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("edn-ep", 4u)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("silicon_creator_id", 0x4001u),
            IBEX_DEV_UINT_PROP("product_id", 0x0002u),
            IBEX_DEV_UINT_PROP("revision_id", 0x1u),
            IBEX_DEV_BOOL_PROP("volatile_raw_unlock", true),
            IBEX_DEV_UINT_PROP("kmac-app", 1u),
            IBEX_DEV_STRING_PROP("raw_unlock_token",
                                 "51e6121c8694c6bc41f36e2175199296"),
            IBEX_DEV_STRING_PROP("lc_state_first",
                "f29f2eb011e290c9210fb1d4302b323db0e81df4"
                "a59985e47749732c6c910d3015a62e61b0c383c1"),
            IBEX_DEV_STRING_PROP("lc_state_last",
                "f29f3fb41fe3d2fda7afffd676abb3ffbaeefff4"
                "a5ffefe4ff4f7fbeed9ddff29db77ee5b7d3d7e5"),
            IBEX_DEV_STRING_PROP("lc_trscnt_first",
                "3cfc8321c4f818ac4d53d244a4c4631e90656423004b81ba"
                "aa5b692c13f2f21d609b685ec45d05042876e8628a8b0dd0"),
            IBEX_DEV_STRING_PROP("lc_trscnt_last",
                "3cfcfb23eef99fad6f7ffb44e6ce7b5ed47767e753cbabfe"
                "bf5fe96e77f3f35d6f9f68ffde5d5564be76fd6bfb8fcdfb")
        )
    },
    [OT_EG_SOC_DEV_ALERT_HANDLER] = {
        .type = TYPE_OT_ALERT,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40150000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 127),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(1, PLIC, 128),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(2, PLIC, 129),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(3, PLIC, 130)
        ),
        .link = IBEXDEVICELINKDEFS(
            OT_EG_SOC_DEVLINK("edn", EDN0)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("pclk", OT_EG_PERIPHERAL_CLK_HZ),
            IBEX_DEV_UINT_PROP("n_alerts", 65u),
            IBEX_DEV_UINT_PROP("n_classes", 4u),
            IBEX_DEV_UINT_PROP("n_lpg", 22u),
            IBEX_DEV_UINT_PROP("edn-ep", 4u)
        ),
    },
    [OT_EG_SOC_DEV_SPI_HOST0] = {
        .type = TYPE_OT_SPI_HOST,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40300000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 131),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(1, PLIC, 132)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("bus-num", 0)
        ),
    },
    [OT_EG_SOC_DEV_SPI_HOST1] = {
        .type = TYPE_OT_SPI_HOST,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40310000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 133),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(1, PLIC, 134)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("bus-num", 1)
        ),
    },
    [OT_EG_SOC_DEV_USBDEV] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-usbdev",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40320000u }
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("size", 0x1000u)
        )
    },
    [OT_EG_SOC_DEV_PWRMGR] = {
        .type = TYPE_OT_PWRMGR,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40400000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 153),
            /* loopback signal since Earlgrey OTP signal are not supported yet*/
            OT_EG_SOC_SIGNAL(OT_PWRMGR_OTP_REQ, 0, PWRMGR,
                             OT_PWRMGR_OTP_RSP, 0),
            OT_EG_SOC_REQ(OT_PWRMGR_LC, LC_CTRL),
            OT_EG_SOC_SIGNAL(OT_PWRMGR_CPU_EN, 0, IBEX_WRAPPER,
                             OT_IBEX_WRAPPER_CPU_EN,
                             OT_IBEX_PWRMGR_CPU_EN),
            OT_EG_SOC_SIGNAL(OT_PWRMGR_RST_REQ, 0, RSTMGR,
                             OT_RSTMGR_RST_REQ, 0)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("num-rom", 1u),
            IBEX_DEV_UINT_PROP("version", OT_PWMGR_VERSION_EG)
        ),
    },
    [OT_EG_SOC_DEV_RSTMGR] = {
        .type = TYPE_OT_RSTMGR,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40410000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_SIGNAL(OT_RSTMGR_SW_RST, 0, PWRMGR, \
                                   OT_PWRMGR_SW_RST, 0)
        ),
    },
    [OT_EG_SOC_DEV_CLKMGR] = {
        .type = TYPE_OT_CLKMGR,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40420000u }
        ),
    },
    [OT_EG_SOC_DEV_SYSRST_CTRL] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-sysrst_ctrl",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40430000u }
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("size", 0x100u)
        )
    },
    [OT_EG_SOC_DEV_ADC_CTRL] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-adc_ctrl",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40440000u }
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("size", 0x80u)
        )
    },
    [OT_EG_SOC_DEV_PWM] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-pwm",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40450000u }
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("size", 0x80u)
        )
    },
    [OT_EG_SOC_DEV_PINMUX] = {
        .type = TYPE_OT_PINMUX_EG,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40460000u }
        ),
    },
    [OT_EG_SOC_DEV_AON_TIMER] = {
        .type = TYPE_OT_AON_TIMER,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40470000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 156),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(1, PLIC, 157),
            OT_EG_SOC_SIGNAL(OT_AON_TIMER_WKUP, 0, PWRMGR, \
                             OT_PWRMGR_WKUP, OT_PWRMGR_WAKEUP_AON_TIMER),
            OT_EG_SOC_SIGNAL(OT_AON_TIMER_BITE, 0, PWRMGR, \
                             OT_PWRMGR_RST, OT_EG_RESET_AON_TIMER)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("pclk", OT_EG_AON_CLK_HZ)
        ),
    },
    [OT_EG_SOC_DEV_AST] = {
        .type = TYPE_OT_AST_EG,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40480000u }
        ),
    },
    [OT_EG_SOC_DEV_SENSOR_CTRL] = {
        .type = TYPE_OT_SENSOR,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40490000u }
        ),
    },
    [OT_EG_SOC_DEV_SRAM_RET_CTRL] = {
        .type = TYPE_OT_SRAM_CTRL,
        .memmap = MEMMAPENTRIES(
            { .base = 0x40500000u },
            { .base = 0x40600000u }
        ),
        .link = IBEXDEVICELINKDEFS(
            OT_EG_SOC_DEVLINK("otp_ctrl", OTP_CTRL)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("size", 0x1000u),
            IBEX_DEV_STRING_PROP("ot_id", "ret")
        ),
    },
    [OT_EG_SOC_DEV_FLASH_CTRL] = {
        .type = TYPE_OT_FLASH,
        .cfg = &ot_eg_soc_flash_ctrl_configure,
        .memmap = MEMMAPENTRIES(
            { .base = 0x41000000u },
            { .base = 0x41008000u },
            { .base = 0x20000000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 160),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(1, PLIC, 161),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(2, PLIC, 162),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(3, PLIC, 163),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(4, PLIC, 164),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(5, PLIC, 165)
        ),
    },
    [OT_EG_SOC_DEV_AES] = {
        .type = TYPE_OT_AES,
        .memmap = MEMMAPENTRIES(
            { .base = 0x41100000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_CLKMGR_HINT(OT_CLKMGR_HINT_AES)
        ),
        .link = IBEXDEVICELINKDEFS(
            OT_EG_SOC_DEVLINK("edn", EDN0)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("edn-ep", 5u)
        ),
    },
    [OT_EG_SOC_DEV_HMAC] = {
        .type = TYPE_OT_HMAC,
        .memmap = MEMMAPENTRIES(
            { .base = 0x41110000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 166),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(1, PLIC, 167),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(2, PLIC, 168),
            OT_EG_SOC_CLKMGR_HINT(OT_CLKMGR_HINT_HMAC)
        ),
    },
    [OT_EG_SOC_DEV_KMAC] = {
        .type = TYPE_OT_KMAC,
        .memmap = MEMMAPENTRIES(
            { .base = 0x41120000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 169),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(1, PLIC, 170),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(2, PLIC, 171)
        ),
        .link = IBEXDEVICELINKDEFS(
            OT_EG_SOC_DEVLINK("edn", EDN0)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("edn-ep", 3u),
            IBEX_DEV_UINT_PROP("num-app", 3u)
        ),
    },
    [OT_EG_SOC_DEV_OTBN] = {
        .type = TYPE_OT_OTBN,
        .memmap = MEMMAPENTRIES(
            { .base = 0x41130000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 172),
            OT_EG_SOC_CLKMGR_HINT(OT_CLKMGR_HINT_OTBN)
        ),
        .link = IBEXDEVICELINKDEFS(
            OT_EG_SOC_DEVLINK("edn-u", EDN0),
            OT_EG_SOC_DEVLINK("edn-r", EDN1)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("edn-u-ep", 6u),
            IBEX_DEV_UINT_PROP("edn-r-ep", 0u)
        ),
    },
    [OT_EG_SOC_DEV_KEYMGR] = {
        .type = TYPE_UNIMPLEMENTED_DEVICE,
        .name = "ot-keymgr",
        .cfg = &ibex_unimp_configure,
        .memmap = MEMMAPENTRIES(
            { .base = 0x41140000u }
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("size", 0x100u),
            IBEX_DEV_BOOL_PROP("warn-once", true)
        )
    },
    [OT_EG_SOC_DEV_CSRNG] = {
        .type = TYPE_OT_CSRNG,
        .memmap = MEMMAPENTRIES(
            { .base = 0x41150000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 174),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(1, PLIC, 175),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(2, PLIC, 176),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(3, PLIC, 177)
        ),
        .link = IBEXDEVICELINKDEFS(
            OT_EG_SOC_DEVLINK("random_src", ENTROPY_SRC),
            OT_EG_SOC_DEVLINK("otp_ctrl", OTP_CTRL)
        ),
    },
    [OT_EG_SOC_DEV_ENTROPY_SRC] = {
        .type = TYPE_OT_ENTROPY_SRC,
        .memmap = MEMMAPENTRIES(
            { .base = 0x41160000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 178),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(1, PLIC, 179),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(2, PLIC, 180),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(3, PLIC, 181)
        ),
        .link = IBEXDEVICELINKDEFS(
            OT_EG_SOC_DEVLINK("ast", AST),
            OT_EG_SOC_DEVLINK("otp_ctrl", OTP_CTRL)
        ),
    },
    [OT_EG_SOC_DEV_EDN0] = {
        .type = TYPE_OT_EDN,
        .memmap = MEMMAPENTRIES(
            { .base = 0x41170000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 182),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(1, PLIC, 183)
        ),
        .link = IBEXDEVICELINKDEFS(
            OT_EG_SOC_DEVLINK("csrng", CSRNG)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("csrng-app", 0u)
        ),
    },
    [OT_EG_SOC_DEV_EDN1] = {
        .type = TYPE_OT_EDN,
        .memmap = MEMMAPENTRIES(
            { .base = 0x41180000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO_SYSBUS_IRQ(0, PLIC, 184),
            OT_EG_SOC_GPIO_SYSBUS_IRQ(1, PLIC, 185)
        ),
        .link = IBEXDEVICELINKDEFS(
            OT_EG_SOC_DEVLINK("csrng", CSRNG)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("csrng-app", 1u)
        ),
    },
    [OT_EG_SOC_DEV_SRAM_MAIN_CTRL] = {
        .type = TYPE_OT_SRAM_CTRL,
        .memmap = MEMMAPENTRIES(
            { .base = 0x411c0000u },
            { .base = 0x10000000u }
        ),
        .link = IBEXDEVICELINKDEFS(
            OT_EG_SOC_DEVLINK("otp_ctrl", OTP_CTRL)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("size", SRAM_MAIN_SIZE),
            IBEX_DEV_STRING_PROP("ot_id", "ram")
        ),
    },
    [OT_EG_SOC_DEV_ROM_CTRL] = {
        .type = TYPE_OT_ROM_CTRL,
        .name = "ot-rom_ctrl",
        .memmap = MEMMAPENTRIES(
            { .base = 0x411e0000u },
            { .base = 0x00008000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_SIGNAL(OT_ROM_CTRL_GOOD, 0, PWRMGR, \
                                   OT_PWRMGR_ROM_GOOD, 0),
            OT_EG_SOC_SIGNAL(OT_ROM_CTRL_DONE, 0, PWRMGR, \
                                   OT_PWRMGR_ROM_DONE, 0)
        ),
        .link = IBEXDEVICELINKDEFS(
            OT_EG_SOC_DEVLINK("kmac", KMAC)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_STRING_PROP("ot_id", "rom"),
            IBEX_DEV_UINT_PROP("size", 0x8000u),
            IBEX_DEV_UINT_PROP("kmac-app", 2u),
            /* Earlgrey-M2.5.2-RC0 */
            IBEX_DEV_STRING_PROP("nonce", "755cf00bd7432c3f"),
            IBEX_DEV_STRING_PROP("key", "8cd4e7eff1b9ec59ce812447c5714595")
        ),
    },
    [OT_EG_SOC_DEV_IBEX_WRAPPER] = {
        .type = TYPE_OT_IBEX_WRAPPER_EG,
        .memmap = MEMMAPENTRIES(
            { .base = 0x411f0000u }
        ),
        .link = IBEXDEVICELINKDEFS(
            OT_EG_SOC_DEVLINK("edn", EDN0)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_UINT_PROP("edn-ep", 7u)
        ),
    },
    [OT_EG_SOC_DEV_RV_DM] = {
        .type = TYPE_PULP_RV_DM,
        .memmap = MEMMAPENTRIES(
            { .base = PULP_DM_BASE },
            { .base = 0x41200000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_DM_CONNECTION(OT_EG_SOC_DEV_DM, 0),
            OT_EG_SOC_DM_CONNECTION(OT_EG_SOC_DEV_DM, 1),
            OT_EG_SOC_DM_CONNECTION(OT_EG_SOC_DEV_DM, 2),
            OT_EG_SOC_DM_CONNECTION(OT_EG_SOC_DEV_DM, 3)
        ),
    },
    [OT_EG_SOC_DEV_PLIC] = {
        .type = TYPE_SIFIVE_PLIC,
        .memmap = MEMMAPENTRIES(
            { .base = 0x48000000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO(1, HART, IRQ_M_EXT)
        ),
        .prop = IBEXDEVICEPROPDEFS(
            IBEX_DEV_STRING_PROP("hart-config", "M"),
            IBEX_DEV_UINT_PROP("hartid-base", 0u),
            /* note: should always be max_irq + 1 */
            IBEX_DEV_UINT_PROP("num-sources", 186u),
            IBEX_DEV_UINT_PROP("num-priorities", 3u),
            IBEX_DEV_UINT_PROP("priority-base", 0x0u),
            IBEX_DEV_UINT_PROP("pending-base", 0x1000u),
            IBEX_DEV_UINT_PROP("enable-base", 0x2000u),
            IBEX_DEV_UINT_PROP("enable-stride", 32u),
            IBEX_DEV_UINT_PROP("context-base", 0x200000u),
            IBEX_DEV_UINT_PROP("context-stride", 8u),
            IBEX_DEV_UINT_PROP("aperture-size", 0x4000000u)
        ),
    },
    [OT_EG_SOC_DEV_PLIC_EXT] = {
        .type = TYPE_OT_PLIC_EXT,
        .memmap = MEMMAPENTRIES(
            { .base = 0x2c000000u }
        ),
        .gpio = IBEXGPIOCONNDEFS(
            OT_EG_SOC_GPIO(0, HART, IRQ_M_SOFT)
        ),
    },
    /* clang-format on */
};

enum OtEGBoardDevice {
    OT_EG_BOARD_DEV_SOC,
    OT_EG_BOARD_DEV_FLASH,
    OT_EG_BOARD_DEV_COUNT,
};

/* ------------------------------------------------------------------------ */
/* Type definitions */
/* ------------------------------------------------------------------------ */

struct OtEGSoCClass {
    DeviceClass parent_class;
    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};

struct OtEGSoCState {
    SysBusDevice parent_obj;

    DeviceState **devices;
};

struct OtEGBoardState {
    DeviceState parent_obj;

    DeviceState **devices;
};

struct OtEGMachineState {
    MachineState parent_obj;

    bool no_epmp_cfg;
    bool ignore_elf_entry;
};

/* ------------------------------------------------------------------------ */
/* Device Configuration */
/* ------------------------------------------------------------------------ */

static void ot_eg_soc_dm_configure(DeviceState *dev, const IbexDeviceDef *def,
                                   DeviceState *parent)
{
    (void)def;
    (void)parent;

    QList *hart = qlist_new();
    qlist_append_int(hart, 0);
    qdev_prop_set_array(dev, "hart", hart);

    RISCVDMMemAttrs pulp_attrs = {
        .attrs = {
            .requester_id = PULP_RV_DM_REQUESTER_ID,
        },
    };
    qdev_prop_set_uint64(dev, "mta_dm", pulp_attrs.value);
}

static void ot_eg_soc_flash_ctrl_configure(
    DeviceState *dev, const IbexDeviceDef *def, DeviceState *parent)
{
    DriveInfo *dinfo = drive_get(IF_MTD, 1, 0);
    (void)def;
    (void)parent;

    if (dinfo) {
        qdev_prop_set_drive_err(dev, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
    }
}

static void ot_eg_soc_hart_configure(DeviceState *dev, const IbexDeviceDef *def,
                                     DeviceState *parent)
{
    OtEGMachineState *ms = RISCV_OT_EG_MACHINE(qdev_get_machine());
    QList *pmp_cfg, *pmp_addr;
    (void)def;
    (void)parent;

    if (ms->no_epmp_cfg) {
        /* skip default PMP config */
        return;
    }

    pmp_cfg = qlist_new();
    for (unsigned ix = 0; ix < ARRAY_SIZE(ot_eg_pmp_cfgs); ix++) {
        qlist_append_int(pmp_cfg, ot_eg_pmp_cfgs[ix]);
    }
    qdev_prop_set_array(dev, "pmp_cfg", pmp_cfg);

    pmp_addr = qlist_new();
    for (unsigned ix = 0; ix < ARRAY_SIZE(ot_eg_pmp_addrs); ix++) {
        qlist_append_int(pmp_addr, ot_eg_pmp_addrs[ix]);
    }
    qdev_prop_set_array(dev, "pmp_addr", pmp_addr);

    qdev_prop_set_uint64(dev, "mseccfg", (uint64_t)OT_EG_MSECCFG);
}

static void ot_eg_soc_otp_ctrl_configure(
    DeviceState *dev, const IbexDeviceDef *def, DeviceState *parent)
{
    DriveInfo *dinfo = drive_get(IF_PFLASH, 0, 0);
    (void)def;
    (void)parent;

    if (dinfo) {
        qdev_prop_set_drive_err(dev, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
    }
}

static void ot_eg_soc_tap_ctrl_configure(
    DeviceState *dev, const IbexDeviceDef *def, DeviceState *parent)
{
    (void)parent;
    (void)def;

    Chardev *chr;

    chr = ibex_get_chardev_by_id("taprbb");
    if (chr) {
        qdev_prop_set_chr(dev, "chardev", chr);
    }
}

static void ot_eg_soc_spi_device_configure(
    DeviceState *dev, const IbexDeviceDef *def, DeviceState *parent)
{
    (void)parent;
    (void)def;

    Chardev *chr;

    chr = ibex_get_chardev_by_id("spidev");
    if (chr) {
        qdev_prop_set_chr(dev, "chardev", chr);
    }
}

static void ot_eg_soc_uart_configure(DeviceState *dev, const IbexDeviceDef *def,
                                     DeviceState *parent)
{
    (void)def;
    (void)parent;
    qdev_prop_set_chr(dev, "chardev", serial_hd(IBEX_GET_INSTANCE_NUM(def)));
}

/* ------------------------------------------------------------------------ */
/* SoC */
/* ------------------------------------------------------------------------ */

static void ot_eg_soc_hw_reset(void *opaque, int irq, int level)
{
    OtEGSoCState *s = opaque;

    g_assert(irq == 0);

    if (level) {
        CPUState *cs = CPU(s->devices[OT_EG_SOC_DEV_HART]);
        cpu_synchronize_state(cs);
        bus_cold_reset(sysbus_get_default());
        cpu_synchronize_post_reset(cs);
    }
}

static void ot_eg_soc_reset_hold(Object *obj, ResetType type)
{
    OtEGSoCClass *c = RISCV_OT_EG_SOC_GET_CLASS(obj);
    OtEGSoCState *s = RISCV_OT_EG_SOC(obj);

    if (c->parent_phases.hold) {
        c->parent_phases.hold(obj, type);
    }

    Object *dtm = OBJECT(s->devices[OT_EG_SOC_DEV_DTM]);
    resettable_reset(dtm, type);

    Object *dm = OBJECT(s->devices[OT_EG_SOC_DEV_DM]);
    resettable_reset(dm, type);

    /* keep ROM_CTRL in reset, we'll release it last */
    resettable_assert_reset(OBJECT(s->devices[OT_EG_SOC_DEV_ROM_CTRL]), type);

    /*
     * Power-On-Reset: leave hart on reset
     * PowerManager takes care of managing Ibex reset when ready
     *
     * Note that an initial, extra single reset cycle (assert/release) is
     * performed from the generic #riscv_cpu_realize function on machine
     * realization.
     */
    CPUState *cs = CPU(s->devices[OT_EG_SOC_DEV_HART]);
    resettable_assert_reset(OBJECT(cs), type);
}

static void ot_eg_soc_reset_exit(Object *obj, ResetType type)
{
    OtEGSoCClass *c = RISCV_OT_EG_SOC_GET_CLASS(obj);
    OtEGSoCState *s = RISCV_OT_EG_SOC(obj);

    if (c->parent_phases.exit) {
        c->parent_phases.exit(obj, type);
    }

    /* let ROM_CTRL get out of reset now */
    resettable_release_reset(OBJECT(s->devices[OT_EG_SOC_DEV_ROM_CTRL]), type);
}

static void ot_eg_soc_realize(DeviceState *dev, Error **errp)
{
    OtEGSoCState *s = RISCV_OT_EG_SOC(dev);
    (void)errp;

    /* Link, define properties and realize devices, then connect GPIOs */
    BusState *bus = sysbus_get_default();
    ot_common_configure_devices_with_id(s->devices, bus, "", false,
                                        ot_eg_soc_devices,
                                        ARRAY_SIZE(ot_eg_soc_devices));

    MemoryRegion *mrs[] = { get_system_memory(), NULL, NULL, NULL };
    ibex_map_devices(s->devices, mrs, ot_eg_soc_devices,
                     ARRAY_SIZE(ot_eg_soc_devices));

    qdev_connect_gpio_out_named(DEVICE(s->devices[OT_EG_SOC_DEV_RSTMGR]),
                                OT_RSTMGR_SOC_RST, 0,
                                qdev_get_gpio_in_named(DEVICE(s),
                                                       OT_EG_SOC_RST_REQ, 0));

    ot_common_check_rom_configuration();

    /* load kernel if provided */
    ibex_load_kernel(NULL);
}

static void ot_eg_soc_init(Object *obj)
{
    OtEGSoCState *s = RISCV_OT_EG_SOC(obj);

    s->devices = ibex_create_devices(ot_eg_soc_devices,
                                     ARRAY_SIZE(ot_eg_soc_devices), DEVICE(s));

    qdev_init_gpio_in_named(DEVICE(obj), &ot_eg_soc_hw_reset, OT_EG_SOC_RST_REQ,
                            1);
}

static void ot_eg_soc_class_init(ObjectClass *oc, void *data)
{
    OtEGSoCClass *sc = RISCV_OT_EG_SOC_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(dc);
    (void)data;

    resettable_class_set_parent_phases(rc, NULL, &ot_eg_soc_reset_hold,
                                       &ot_eg_soc_reset_exit,
                                       &sc->parent_phases);
    dc->realize = &ot_eg_soc_realize;
    dc->user_creatable = false;
}

static const TypeInfo ot_eg_soc_type_info = {
    .name = TYPE_RISCV_OT_EG_SOC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(OtEGSoCState),
    .instance_init = &ot_eg_soc_init,
    .class_init = &ot_eg_soc_class_init,
    .class_size = sizeof(OtEGSoCClass),
};

static void ot_eg_soc_register_types(void)
{
    type_register_static(&ot_eg_soc_type_info);
}

type_init(ot_eg_soc_register_types);

/* ------------------------------------------------------------------------ */
/* Board */
/* ------------------------------------------------------------------------ */

static void ot_eg_board_realize(DeviceState *dev, Error **errp)
{
    OtEGBoardState *board = RISCV_OT_EG_BOARD(dev);

    DeviceState *soc = board->devices[OT_EG_BOARD_DEV_SOC];
    object_property_add_child(OBJECT(board), "soc", OBJECT(soc));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(soc), &error_fatal);

    DeviceState *spihost =
        RISCV_OT_EG_SOC(soc)->devices[OT_EG_SOC_DEV_SPI_HOST0];
    DeviceState *flash = board->devices[OT_EG_BOARD_DEV_FLASH];
    BusState *spibus = qdev_get_child_bus(spihost, "spi0");
    g_assert(spibus);

    DriveInfo *dinfo = drive_get(IF_MTD, 0, 0);
    if (dinfo) {
        qdev_prop_set_drive_err(DEVICE(flash), "drive",
                                blk_by_legacy_dinfo(dinfo), &error_fatal);
    }
    object_property_add_child(OBJECT(board), "dataflash", OBJECT(flash));
    ssi_realize_and_unref(flash, SSI_BUS(spibus), errp);

    qemu_irq cs = qdev_get_gpio_in_named(flash, SSI_GPIO_CS, 0);
    qdev_connect_gpio_out_named(spihost, SSI_GPIO_CS, 0, cs);
}

static void ot_eg_board_init(Object *obj)
{
    OtEGBoardState *s = RISCV_OT_EG_BOARD(obj);

    s->devices = g_new0(DeviceState *, OT_EG_BOARD_DEV_COUNT);
    s->devices[OT_EG_BOARD_DEV_SOC] = qdev_new(TYPE_RISCV_OT_EG_SOC);
    s->devices[OT_EG_BOARD_DEV_FLASH] = qdev_new("is25wp128");
}

static void ot_eg_board_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    (void)data;

    dc->realize = &ot_eg_board_realize;
}

static const TypeInfo ot_eg_board_type_info = {
    .name = TYPE_RISCV_OT_EG_BOARD,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(OtEGBoardState),
    .instance_init = &ot_eg_board_init,
    .class_init = &ot_eg_board_class_init,
};

static void ot_eg_board_register_types(void)
{
    type_register_static(&ot_eg_board_type_info);
}

type_init(ot_eg_board_register_types);

/* ------------------------------------------------------------------------ */
/* Machine */
/* ------------------------------------------------------------------------ */

static bool ot_eg_machine_get_no_epmp_cfg(Object *obj, Error **errp)
{
    OtEGMachineState *s = RISCV_OT_EG_MACHINE(obj);
    (void)errp;

    return s->no_epmp_cfg;
}

static void ot_eg_machine_set_no_epmp_cfg(Object *obj, bool value, Error **errp)
{
    OtEGMachineState *s = RISCV_OT_EG_MACHINE(obj);
    (void)errp;

    s->no_epmp_cfg = value;
}

static bool ot_eg_machine_get_ignore_elf_entry(Object *obj, Error **errp)
{
    OtEGMachineState *s = RISCV_OT_EG_MACHINE(obj);
    (void)errp;

    return s->ignore_elf_entry;
}

static void
ot_eg_machine_set_ignore_elf_entry(Object *obj, bool value, Error **errp)
{
    OtEGMachineState *s = RISCV_OT_EG_MACHINE(obj);
    (void)errp;

    s->ignore_elf_entry = value;
}

static void ot_eg_machine_instance_init(Object *obj)
{
    OtEGMachineState *s = RISCV_OT_EG_MACHINE(obj);

    s->no_epmp_cfg = false;
    object_property_add_bool(obj, "no-epmp-cfg", &ot_eg_machine_get_no_epmp_cfg,
                             &ot_eg_machine_set_no_epmp_cfg);
    object_property_set_description(obj, "no-epmp-cfg",
                                    "Skip default ePMP configuration");
    object_property_add_bool(obj, "ignore-elf-entry",
                             &ot_eg_machine_get_ignore_elf_entry,
                             &ot_eg_machine_set_ignore_elf_entry);
    object_property_set_description(obj, "ignore-elf-entry",
                                    "Do not set vCPU PC with ELF entry point");
}

static void ot_eg_machine_init(MachineState *state)
{
    DeviceState *dev = qdev_new(TYPE_RISCV_OT_EG_BOARD);

    object_property_add_child(OBJECT(state), "board", OBJECT(dev));
    qdev_realize(dev, NULL, &error_fatal);
}

static void ot_eg_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    (void)data;

    mc->desc = "RISC-V Board compatible with OpenTitan EarlGrey FPGA platform";
    mc->init = ot_eg_machine_init;
    mc->max_cpus = 1u;
    mc->default_cpu_type = ot_eg_soc_devices[OT_EG_SOC_DEV_HART].type;
    const IbexDeviceDef *sram =
        &ot_eg_soc_devices[OT_EG_SOC_DEV_SRAM_MAIN_CTRL];
    mc->default_ram_id = sram->type;
    mc->default_ram_size = SRAM_MAIN_SIZE;
}

static const TypeInfo ot_eg_machine_type_info = {
    .name = TYPE_RISCV_OT_EG_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(OtEGMachineState),
    .instance_init = &ot_eg_machine_instance_init,
    .class_init = &ot_eg_machine_class_init,
};

static void ot_eg_machine_register_types(void)
{
    type_register_static(&ot_eg_machine_type_info);
}

type_init(ot_eg_machine_register_types);
