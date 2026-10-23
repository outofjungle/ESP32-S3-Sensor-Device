#pragma once

// ---------------------------------------------------------------------------
// Registers this module uses. See docs/19-sensor-ldc1612.md for the full
// register map, including the three registers (OFFSET0, OFFSET1,
// ERROR_CONFIG) this version does not use.
// ---------------------------------------------------------------------------

#define REG_DATA0_MSB       0x00
#define REG_DATA0_LSB       0x01
#define REG_DATA1_MSB       0x02
#define REG_DATA1_LSB       0x03
#define REG_RCOUNT0         0x08
#define REG_RCOUNT1         0x09
#define REG_SETTLECOUNT0    0x10
#define REG_SETTLECOUNT1    0x11
#define REG_CLOCK_DIVIDERS0 0x14
#define REG_CLOCK_DIVIDERS1 0x15
#define REG_STATUS          0x18
#define REG_CONFIG          0x1A
#define REG_MUX_CONFIG      0x1B
#define REG_RESET_DEV       0x1C
#define REG_DRIVE_CURRENT0  0x1E
#define REG_DRIVE_CURRENT1  0x1F

// CONFIG (0x1A) field positions.
#define CONFIG_ACTIVE_CHAN_SHIFT         14
#define CONFIG_ACTIVE_CHAN_MASK          (0x3u << CONFIG_ACTIVE_CHAN_SHIFT)
#define CONFIG_SLEEP_MODE_EN_SHIFT       13
#define CONFIG_SLEEP_MODE_EN_BIT         (1u << CONFIG_SLEEP_MODE_EN_SHIFT)
#define CONFIG_RP_OVERRIDE_EN_SHIFT      12
#define CONFIG_RP_OVERRIDE_EN_BIT        (1u << CONFIG_RP_OVERRIDE_EN_SHIFT)
#define CONFIG_SENSOR_ACTIVATE_SEL_SHIFT 11
#define CONFIG_SENSOR_ACTIVATE_SEL_BIT   (1u << CONFIG_SENSOR_ACTIVATE_SEL_SHIFT)
#define CONFIG_AUTO_AMP_DIS_SHIFT        10
#define CONFIG_AUTO_AMP_DIS_BIT          (1u << CONFIG_AUTO_AMP_DIS_SHIFT)
#define CONFIG_REF_CLK_SRC_SHIFT         9
#define CONFIG_REF_CLK_SRC_BIT           (1u << CONFIG_REF_CLK_SRC_SHIFT)
#define CONFIG_HIGH_CURRENT_DRV_SHIFT    6
#define CONFIG_HIGH_CURRENT_DRV_BIT      (1u << CONFIG_HIGH_CURRENT_DRV_SHIFT)

// MUX_CONFIG (0x1B) field positions. RR_SEQUENCE (bits 14:13) is left
// alone everywhere in this file -- it stays at the chip's reset value,
// b00 (Ch0, Ch1), the only sequence a 2-channel chip needs.
#define MUX_CONFIG_AUTOSCAN_EN_SHIFT 15
#define MUX_CONFIG_AUTOSCAN_EN_BIT   (1u << MUX_CONFIG_AUTOSCAN_EN_SHIFT)
#define MUX_CONFIG_DEGLITCH_MASK     0x0007u

// CLOCK_DIVIDERSx (0x14/0x15) field positions.
#define CLKDIV_FIN_DIVIDER_SHIFT 12
#define CLKDIV_FIN_DIVIDER_MASK  (0xFu << CLKDIV_FIN_DIVIDER_SHIFT)
#define CLKDIV_FREF_DIVIDER_MASK 0x03FFu

// DRIVE_CURRENTx (0x1E/0x1F) field position.
#define DRIVE_CURRENT_IDRIVE_SHIFT 11
#define DRIVE_CURRENT_IDRIVE_MASK  (0x1Fu << DRIVE_CURRENT_IDRIVE_SHIFT)
