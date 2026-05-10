#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <string.h>

#include "spi_functions.h"
#include "max30009_bioz.h"

LOG_MODULE_REGISTER(max30009_bioz, LOG_LEVEL_DBG);

#define REG_FIFO_DATA_REG      0x0C
#define REG_FIFO_CONFIG_1      0x0D
#define REG_FIFO_CONFIG_2      0x0E
#define REG_SYS_CONFIG_1       0x11
#define REG_PLL_CONFIG_1       0x17
#define REG_PLL_CONFIG_2       0x18
#define REG_PLL_CONFIG_3       0x19
#define REG_PLL_CONFIG_4       0x1A
#define REG_BIOZ_CONFIG_1      0x20
#define REG_BIOZ_CONFIG_2      0x21
#define REG_BIOZ_CONFIG_3      0x22
#define REG_BIOZ_CONFIG_4      0x23
#define REG_BIOZ_CONFIG_5      0x24
#define REG_BIOZ_CONFIG_6      0x25
#define REG_BIOZ_CONFIG_7      0x28
#define REG_BIOZ_MUX_CFG_1    0x41
#define REG_BIOZ_MUX_CFG_2    0x42
#define REG_BIOZ_MUX_CFG_3    0x43
#define REG_LEAD_BIAS_CFG_1   0x58
#define REG_INT_ENABLE_1       0x80
#define REG_PART_ID            0xFF

#define MAX30009_PART_ID_VALUE  0x42
#define FIFO_TAG_I_LOAD         0x100000

extern struct spi_config spi_cfg;
extern const struct device *spi_bus;
extern void cs_select(void);
extern void cs_deselect(void);

/* ------------------------------------------------------------------ */
/*  Sagedus table                                                    */
/*                                                                     */
/*  1 kHz:   M=500, K=64, N=512,  DAC_OSR=256, ADC_OSR=512  (~62.5 sps) */
/*  50 kHz:  M=781, K=2,  N=1024, DAC_OSR=256, ADC_OSR=512  (~24.4 sps) */
/*  100 kHz: M=781, K=2,  N=1024, DAC_OSR=128, ADC_OSR=512  (~48.8 sps) */
/* ------------------------------------------------------------------ */
const struct freq_config_info freq_configs[NUM_FREQS] = {
    [FREQ_1KHZ]   = { .label = "1kHz",   .pll_cfg1 = 0x4D, .pll_cfg2 = 0xF4, .bioz_cfg1_bg_only = 0xF4, .bioz_cfg1_enable = 0xF7 },
    [FREQ_50KHZ]  = { .label = "50kHz",  .pll_cfg1 = 0xE3, .pll_cfg2 = 0x0D, .bioz_cfg1_bg_only = 0xF4, .bioz_cfg1_enable = 0xF7 },
    [FREQ_100KHZ] = { .label = "100kHz", .pll_cfg1 = 0xE3, .pll_cfg2 = 0x0D, .bioz_cfg1_bg_only = 0xB4, .bioz_cfg1_enable = 0xB7 },
};

static int current_freq = FREQ_50KHZ;  /* default at boot */

/// @brief Seadistab MAX30009 sageduse
/// @param freq_idx Sageduse indeks (FREQ_1KHZ, FREQ_50KHZ, FREQ_100KHZ)
/// @return 0 edukas, negatiivne viga
int max30009_set_frequency(int freq_idx)
{
    if (freq_idx < 0 || freq_idx >= NUM_FREQS) {
        LOG_ERR("Bad freq idx: %d", freq_idx);
        return -EINVAL;
    }
    current_freq = freq_idx;
    LOG_INF("Frequency: %s", freq_configs[freq_idx].label);
    return 0;
}

int max30009_get_frequency(void)
{
    return current_freq;
}

/* ------------------------------------------------------------------ */
/*  BioZ config table                                                 */
/* ------------------------------------------------------------------ */
/// @brief MAX30009 BioZ konfiguratsioonid
const struct bioz_config_info bioz_configs[BIOZ_NUM_CONFIGS] = {
    /* ---- Range 3: 0.27625 kΩ, 1.28 mA ---- */
    [BIOZ_CFG_1280UA_G10] = { .id =  0, .label = "1280uA_G10", .i_drive = 1.28e-3,  .gain = 10.0, .reg_0x22 = 0x3C, .reg_0x24 = 0xB3},
    [BIOZ_CFG_1280UA_G5]  = { .id =  1, .label = "1280uA_G5",  .i_drive = 1.28e-3,  .gain =  5.0, .reg_0x22 = 0x3C, .reg_0x24 = 0xB2},
    [BIOZ_CFG_1280UA_G2]  = { .id =  2, .label = "1280uA_G2",  .i_drive = 1.28e-3,  .gain =  2.0, .reg_0x22 = 0x3C, .reg_0x24 = 0xB1},
    [BIOZ_CFG_1280UA_G1]  = { .id =  3, .label = "1280uA_G1",  .i_drive = 1.28e-3,  .gain =  1.0, .reg_0x22 = 0x3C, .reg_0x24 = 0xB0},
    /* ---- Range 3: 0.27625 kΩ, 640 µA ---- */
    [BIOZ_CFG_640UA_G10]  = { .id =  4, .label = "640uA_G10",  .i_drive = 640e-6,   .gain = 10.0, .reg_0x22 = 0x2C, .reg_0x24 = 0xB3},
    [BIOZ_CFG_640UA_G5]   = { .id =  5, .label = "640uA_G5",   .i_drive = 640e-6,   .gain =  5.0, .reg_0x22 = 0x2C, .reg_0x24 = 0xB2},
    [BIOZ_CFG_640UA_G2]   = { .id =  6, .label = "640uA_G2",   .i_drive = 640e-6,   .gain =  2.0, .reg_0x22 = 0x2C, .reg_0x24 = 0xB1},
    [BIOZ_CFG_640UA_G1]   = { .id =  7, .label = "640uA_G1",   .i_drive = 640e-6,   .gain =  1.0, .reg_0x22 = 0x2C, .reg_0x24 = 0xB0},
    /* ---- Range 3: 0.27625 kΩ, 256 µA ---- */
    [BIOZ_CFG_256UA_G10]  = { .id =  8, .label = "256uA_G10",  .i_drive = 256e-6,   .gain = 10.0, .reg_0x22 = 0x1C, .reg_0x24 = 0xB3},
    [BIOZ_CFG_256UA_G5]   = { .id =  9, .label = "256uA_G5",   .i_drive = 256e-6,   .gain =  5.0, .reg_0x22 = 0x1C, .reg_0x24 = 0xB2},
    [BIOZ_CFG_256UA_G2]   = { .id = 10, .label = "256uA_G2",   .i_drive = 256e-6,   .gain =  2.0, .reg_0x22 = 0x1C, .reg_0x24 = 0xB1},
    [BIOZ_CFG_256UA_G1]   = { .id = 11, .label = "256uA_G1",   .i_drive = 256e-6,   .gain =  1.0, .reg_0x22 = 0x1C, .reg_0x24 = 0xB0},
    /* ---- Range 3: 0.27625 kΩ, 128 µA ---- */
    [BIOZ_CFG_128UA_G10]  = { .id = 12, .label = "128uA_G10",  .i_drive = 128e-6,   .gain = 10.0, .reg_0x22 = 0x0C, .reg_0x24 = 0xB3},
    [BIOZ_CFG_128UA_G5]   = { .id = 13, .label = "128uA_G5",   .i_drive = 128e-6,   .gain =  5.0, .reg_0x22 = 0x0C, .reg_0x24 = 0xB2},
    [BIOZ_CFG_128UA_G2]   = { .id = 14, .label = "128uA_G2",   .i_drive = 128e-6,   .gain =  2.0, .reg_0x22 = 0x0C, .reg_0x24 = 0xB1},
    [BIOZ_CFG_128UA_G1]   = { .id = 15, .label = "128uA_G1",   .i_drive = 128e-6,   .gain =  1.0, .reg_0x22 = 0x0C, .reg_0x24 = 0xB0},
    /* ---- Range 2: 5.525 kΩ, 64 µA ---- */
    [BIOZ_CFG_64UA_G10]   = { .id = 16, .label = "64uA_G10",   .i_drive = 64e-6,    .gain = 10.0, .reg_0x22 = 0x38, .reg_0x24 = 0xB3},
    [BIOZ_CFG_64UA_G5]    = { .id = 17, .label = "64uA_G5",    .i_drive = 64e-6,    .gain =  5.0, .reg_0x22 = 0x38, .reg_0x24 = 0xB2},
    [BIOZ_CFG_64UA_G2]    = { .id = 18, .label = "64uA_G2",    .i_drive = 64e-6,    .gain =  2.0, .reg_0x22 = 0x38, .reg_0x24 = 0xB1},
    [BIOZ_CFG_64UA_G1]    = { .id = 19, .label = "64uA_G1",    .i_drive = 64e-6,    .gain =  1.0, .reg_0x22 = 0x38, .reg_0x24 = 0xB0},
    /* ---- Range 2: 5.525 kΩ, 32 µA ---- */
    [BIOZ_CFG_32UA_G10]   = { .id = 20, .label = "32uA_G10",   .i_drive = 32e-6,    .gain = 10.0, .reg_0x22 = 0x28, .reg_0x24 = 0xB3},
    [BIOZ_CFG_32UA_G5]    = { .id = 21, .label = "32uA_G5",    .i_drive = 32e-6,    .gain =  5.0, .reg_0x22 = 0x28, .reg_0x24 = 0xB2},
    [BIOZ_CFG_32UA_G2]    = { .id = 22, .label = "32uA_G2",    .i_drive = 32e-6,    .gain =  2.0, .reg_0x22 = 0x28, .reg_0x24 = 0xB1},
    [BIOZ_CFG_32UA_G1]    = { .id = 23, .label = "32uA_G1",    .i_drive = 32e-6,    .gain =  1.0, .reg_0x22 = 0x28, .reg_0x24 = 0xB0},
    /* ---- Range 2: 5.525 kΩ, 12.8 µA ---- */
    [BIOZ_CFG_12_8UA_G10] = { .id = 24, .label = "12_8uA_G10", .i_drive = 12.8e-6,  .gain = 10.0, .reg_0x22 = 0x18, .reg_0x24 = 0xB3},
    [BIOZ_CFG_12_8UA_G5]  = { .id = 25, .label = "12_8uA_G5",  .i_drive = 12.8e-6,  .gain =  5.0, .reg_0x22 = 0x18, .reg_0x24 = 0xB2},
    [BIOZ_CFG_12_8UA_G2]  = { .id = 26, .label = "12_8uA_G2",  .i_drive = 12.8e-6,  .gain =  2.0, .reg_0x22 = 0x18, .reg_0x24 = 0xB1},
    [BIOZ_CFG_12_8UA_G1]  = { .id = 27, .label = "12_8uA_G1",  .i_drive = 12.8e-6,  .gain =  1.0, .reg_0x22 = 0x18, .reg_0x24 = 0xB0},
    /* ---- Range 2: 5.525 kΩ, 6.4 µA ---- */
    [BIOZ_CFG_6_4UA_G10]  = { .id = 28, .label = "6_4uA_G10",  .i_drive = 6.4e-6,   .gain = 10.0, .reg_0x22 = 0x08, .reg_0x24 = 0xB3},
    [BIOZ_CFG_6_4UA_G5]   = { .id = 29, .label = "6_4uA_G5",   .i_drive = 6.4e-6,   .gain =  5.0, .reg_0x22 = 0x08, .reg_0x24 = 0xB2},
    [BIOZ_CFG_6_4UA_G2]   = { .id = 30, .label = "6_4uA_G2",   .i_drive = 6.4e-6,   .gain =  2.0, .reg_0x22 = 0x08, .reg_0x24 = 0xB1},
    [BIOZ_CFG_6_4UA_G1]   = { .id = 31, .label = "6_4uA_G1",   .i_drive = 6.4e-6,   .gain =  1.0, .reg_0x22 = 0x08, .reg_0x24 = 0xB0},
};

/// @brief BioZ kalibratsioonitabel — iga konfiguratsiooni jaoks on defineeritud lineaarne kalibratsioon (slope ja intercept), 
/// mis teisendab mõõdetud väärtused tegelikuks impedantsiks oomi-des
const struct bioz_cal_info bioz_calibrations[NUM_FREQS][BIOZ_NUM_CONFIGS] = {
    [FREQ_1KHZ] = {
        [BIOZ_CFG_64UA_G10] = { .slope = 1.1062, .intercept = -3.0241 },
        [BIOZ_CFG_64UA_G5]  = { .slope = 1.1055, .intercept = -5.6845 },
        [BIOZ_CFG_64UA_G2]  = { .slope = 1.1038, .intercept = -13.8257 },
        [BIOZ_CFG_64UA_G1]  = { .slope = 1.103, .intercept = -27.1296 },
        [BIOZ_CFG_32UA_G10] = { .slope = 1.1085, .intercept = -5.669 },
        [BIOZ_CFG_32UA_G5]  = { .slope = 1.1074, .intercept = -11.0133 },
        [BIOZ_CFG_32UA_G2]  = { .slope = 1.1055, .intercept = -27.1136 },
        [BIOZ_CFG_32UA_G1]  = { .slope = 1.1047, .intercept = -53.7299 },
    },
    [FREQ_50KHZ] = {
        [BIOZ_CFG_256UA_G2] = { .slope = 1.346, .intercept = -2.493 },
        [BIOZ_CFG_256UA_G1] = { .slope = 1.345, .intercept = -6.760 },
        [BIOZ_CFG_128UA_G5] = { .slope = 1.360, .intercept = -4.279 },
        [BIOZ_CFG_128UA_G2] = { .slope = 1.357, .intercept = -9.464 },
        [BIOZ_CFG_128UA_G1] = { .slope = 1.356, .intercept = -18.059 },
        [BIOZ_CFG_64UA_G10] = { .slope = 1.383, .intercept = -0.666 },
        [BIOZ_CFG_64UA_G5]  = { .slope = 1.381, .intercept = -3.699 },
        [BIOZ_CFG_64UA_G2]  = { .slope = 1.379, .intercept = -12.848 },
        [BIOZ_CFG_64UA_G1]  = { .slope = 1.377, .intercept = -27.631 },
        [BIOZ_CFG_32UA_G10] = { .slope = 1.387, .intercept = -3.719 },
        [BIOZ_CFG_32UA_G5]  = { .slope = 1.386, .intercept = -9.817 },
        [BIOZ_CFG_32UA_G2]  = { .slope = 1.383, .intercept = -27.786 },
        [BIOZ_CFG_32UA_G1]  = { .slope = 1.379, .intercept = -54.870 },
    },
    [FREQ_100KHZ] = {
        [BIOZ_CFG_256UA_G2] = { .slope = 1.437, .intercept = -2.498 },
        [BIOZ_CFG_256UA_G1] = { .slope = 1.436, .intercept = -6.730 },
        [BIOZ_CFG_128UA_G5] = { .slope = 1.452, .intercept = -4.294 },
        [BIOZ_CFG_128UA_G2] = { .slope = 1.449, .intercept = -9.252 },
        [BIOZ_CFG_128UA_G1] = { .slope = 1.448, .intercept = -17.451 },
        [BIOZ_CFG_64UA_G10] = { .slope = 1.427, .intercept = -1.784 },
        [BIOZ_CFG_64UA_G5]  = { .slope = 1.426, .intercept = -4.835 },
        [BIOZ_CFG_64UA_G2]  = { .slope = 1.423, .intercept = -14.033 },
        [BIOZ_CFG_64UA_G1]  = { .slope = 1.421, .intercept = -28.905 },
        [BIOZ_CFG_32UA_G10] = { .slope = 1.432, .intercept = -4.939 },
        [BIOZ_CFG_32UA_G5]  = { .slope = 1.430, .intercept = -11.108 },
        [BIOZ_CFG_32UA_G2]  = { .slope = 1.427, .intercept = -29.225 },
        [BIOZ_CFG_32UA_G1]  = { .slope = 1.424, .intercept = -56.907 },
    },
};

/// @brief Seadistab MAX30009 konfiguratsiooni
/// @param reg22 BioZ voolu seadistus (0x00..0x3F)
/// @param reg24 BioZ võimenduse seadistus (0x00..0x3F)
/// @return 0 edukas, negatiivne viga
static int max30009_configure(uint8_t reg22, uint8_t reg24)
{
    const struct freq_config_info *fc = &freq_configs[current_freq];
    uint8_t id;

    /*
     * Phase 0: SAFE SHUTDOWN — Tuleb teha enne iga konfiguratsiooni, et vältida võimalikke kahjustusi või mittetoimimist.
     */
    write_register(REG_BIOZ_CONFIG_1, 0x00);   /* BioZ OFF */
    k_msleep(1);
    write_register(REG_PLL_CONFIG_1,  0x00);   /* PLL OFF  */
    k_msleep(1);

    /*
     * Phase 1: Soft reset — andmelehe lk 24 soovitus
     */
    write_register(REG_BIOZ_CONFIG_1, 0x04);   /* BG_EN = 1 only            */
    write_register(REG_SYS_CONFIG_1,  0x00);   /* SHDN = 0                  */
    write_register(REG_PLL_CONFIG_4,  0x00);   /* REF_CLK_SEL = 0           */
    k_msleep(1);
    write_register(REG_SYS_CONFIG_1,  0x01);   /* RESET = 1                 */
    k_msleep(2);

    /*
     * Phase 2: Kõikide registrite muutmine
     */
    write_register(REG_INT_ENABLE_1,   0x80);

    /* PLL — frequency-dependent */
    /* PLL — sagedusepõhine */
    write_register(REG_PLL_CONFIG_1,   fc->pll_cfg1);
    write_register(REG_PLL_CONFIG_2,   fc->pll_cfg2);
    write_register(REG_PLL_CONFIG_3,   0x01);
    write_register(REG_PLL_CONFIG_4,   0x20);

    /* BioZ — set current + gain while I/Q OFF (frequency-dependent OSR template) */
    /* BioZ — voolu- ja võimenduse seadistamine */
    write_register(REG_BIOZ_CONFIG_1,  fc->bioz_cfg1_bg_only);
    write_register(REG_BIOZ_CONFIG_2,  0x20);
    write_register(REG_BIOZ_CONFIG_3,  reg22);   /* current */
    write_register(REG_BIOZ_CONFIG_4,  0x00);
    write_register(REG_BIOZ_CONFIG_5,  reg24);   /* gain    */
    write_register(REG_BIOZ_CONFIG_6,  0xCF);
    write_register(REG_BIOZ_CONFIG_7,  0x02);

    /* MUX — Tetrapolar */
    write_register(REG_BIOZ_MUX_CFG_1, 0x06);
    write_register(REG_BIOZ_MUX_CFG_2, 0x01);
    write_register(REG_BIOZ_MUX_CFG_3, 0xA0);

    /* Lead bias */
    write_register(REG_LEAD_BIAS_CFG_1, 0x07);

    /* FIFO */
    write_register(REG_INT_ENABLE_1,   0x00);
    write_register(REG_FIFO_CONFIG_1,  0xFF);
    write_register(REG_FIFO_CONFIG_2,  0x1E);

    /* Phase 3: PLL lock */
    k_msleep(5000);

    id = read_register(REG_PART_ID);
    if (id != MAX30009_PART_ID_VALUE) {
        LOG_ERR("Part ID: 0x%02X (expected 0x42)", id);
        return -ENODEV;
    }
    k_msleep(50);

    /* Verify registers were accepted */
    /* Kontroll, kas seadistus õnnestus */
    uint8_t chk22 = read_register(REG_BIOZ_CONFIG_3);
    uint8_t chk24 = read_register(REG_BIOZ_CONFIG_5);
    if (chk22 != reg22) {
        LOG_WRN("0x22: wrote 0x%02X, read 0x%02X", reg22, chk22);
    }
    if (chk24 != reg24) {
        LOG_WRN("0x24: wrote 0x%02X, read 0x%02X", reg24, chk24);
    }

    /* Phase 4: enable — restart PLL cleanly, then BioZ last */
    /* PLL ja BioZ sisselülitamine */
    write_register(REG_SYS_CONFIG_1,   0x00);                  k_msleep(20);
    write_register(REG_PLL_CONFIG_4,   0x20);                  k_msleep(20);
    write_register(REG_PLL_CONFIG_1,   fc->pll_cfg1);          k_msleep(40);
    write_register(REG_PLL_CONFIG_2,   fc->pll_cfg2);          k_msleep(20);
    write_register(REG_BIOZ_CONFIG_1,  fc->bioz_cfg1_enable);  k_msleep(20);

    dump_registers();

    return 0;
}

/// @brief Seadistab MAX30009 konfiguratsiooni
/// @param config_id Konfiguratsiooni ID
/// @return 0 edukas, negatiivne viga
int max30009_set_config(int config_id)
{
    if (config_id < 0 || config_id >= BIOZ_NUM_CONFIGS) {
        LOG_ERR("Bad config: %d", config_id);
        return -EINVAL;
    }

    const struct bioz_config_info *cfg = &bioz_configs[config_id];
    LOG_INF("Config: %s @ %s (0x22=0x%02X, 0x24=0x%02X)",
            cfg->label, freq_configs[current_freq].label,
            cfg->reg_0x22, cfg->reg_0x24);

    int ret = max30009_configure(cfg->reg_0x22, cfg->reg_0x24);
    if (ret == 0) {
        LOG_INF("Ready: %s @ %s", cfg->label, freq_configs[current_freq].label);
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/*  FIFO + sample reading                                             */
/* ------------------------------------------------------------------ */
/// @brief Loeb MAX30009 FIFO-st BioZ näidud
/// @param buf Puhver, kuhu salvestada lugemised (iga näid koosneb 6 baitist: 3 baiti I ja 3 baiti Q jaoks)
/// @param num_bytes Puhvri suurus baitides (soovitatavalt mitme näidu jagu, nt 6*10=60 baitit 10 näidu jaoks)
/// @return 0 edukas, negatiivne viga
int max30009_fifo_read(uint8_t *buf, uint16_t num_bytes)
{
    if (!buf || num_bytes == 0) return -EINVAL;

    uint8_t hdr[2] = { REG_FIFO_DATA_REG, 0x80 };
    uint8_t tx_dummy[num_bytes];
    memset(tx_dummy, 0xFF, num_bytes);

    const struct spi_buf tx_bufs[] = {
        { .buf = hdr,      .len = 2          },
        { .buf = tx_dummy, .len = num_bytes  },
    };
    const struct spi_buf rx_bufs[] = {
        { .buf = NULL, .len = 2          },
        { .buf = buf,  .len = num_bytes  },
    };
    const struct spi_buf_set tx_set = { .buffers = tx_bufs, .count = 2 };
    const struct spi_buf_set rx_set = { .buffers = rx_bufs, .count = 2 };

    cs_select();
    int ret = spi_transceive(spi_bus, &spi_cfg, &tx_set, &rx_set);
    cs_deselect();

    if (ret) LOG_ERR("FIFO read fail: %d", ret);
    return ret;
}

/// @brief 20-bitise väärtuse sign-extendimine 32-bitiseks, võttes arvesse, et sisend on tegelikult 24-bitine (3 baiti),
/// kuid ainult alumised 20 bitti on väärtus, ülejäänud on nullid või täisarvuline laiendus
/// @param raw24 24-bitine sisend, kus ainult alumised 20 bitti on mõõteväärtus
/// @return 32-bitine sign-extended väärtus
static int32_t sign_extend_20bit(uint32_t raw24)
{
    int32_t val = (int32_t)(raw24 & 0x0FFFFF);
    if (val & 0x080000) val |= 0xFFF00000;
    return val;
}

/// @brief Loeb MAX30009 FIFO-st ühe BioZ näidu ja salvestab selle struktuuri, kus on eraldi I ja Q komponendid.
/// Eeldab, et FIFO on juba õigesti seadistatud ja täidetud näitudega.
/// @param sample BioZ näidu struktuur, kuhu salvestada lugemine
/// @return 0 edukas, negatiivne viga
int max30009_get_bioz_sample(struct bioz_sample *sample)
{
    if (!sample) return -EINVAL;

    uint8_t raw[6];
    int ret = max30009_fifo_read(raw, sizeof(raw));
    if (ret) return ret;

    uint32_t w0 = ((uint32_t)raw[0]<<16)|((uint32_t)raw[1]<<8)|(uint32_t)raw[2];
    uint32_t w1 = ((uint32_t)raw[3]<<16)|((uint32_t)raw[4]<<8)|(uint32_t)raw[5];

    int32_t v0 = sign_extend_20bit(w0);
    int32_t v1 = sign_extend_20bit(w1);

    if (w0 & FIFO_TAG_I_LOAD) {
        sample->i = v0; sample->q = v1;
    } else {
        sample->i = v1; sample->q = v0;
    }
    return 0;
}