#ifndef MAX30009_BIOZ_H
#define MAX30009_BIOZ_H

#include <stdint.h>

struct bioz_sample {
    int32_t i;
    int32_t q;
};


// Konfiguratsioonide seadistus
enum bioz_config_id {
    /* Range 3: CDR 0.27625 kΩ */
    BIOZ_CFG_1280UA_G10 = 0,
    BIOZ_CFG_1280UA_G5,
    BIOZ_CFG_1280UA_G2,
    BIOZ_CFG_1280UA_G1,
    BIOZ_CFG_640UA_G10,
    BIOZ_CFG_640UA_G5,
    BIOZ_CFG_640UA_G2,
    BIOZ_CFG_640UA_G1,
    BIOZ_CFG_256UA_G10,
    BIOZ_CFG_256UA_G5,
    BIOZ_CFG_256UA_G2,
    BIOZ_CFG_256UA_G1,
    BIOZ_CFG_128UA_G10,
    BIOZ_CFG_128UA_G5,
    BIOZ_CFG_128UA_G2,
    BIOZ_CFG_128UA_G1,
    /* Range 2: CDR 5.525 kΩ */
    BIOZ_CFG_64UA_G10,
    BIOZ_CFG_64UA_G5,
    BIOZ_CFG_64UA_G2,
    BIOZ_CFG_64UA_G1,
    BIOZ_CFG_32UA_G10,
    BIOZ_CFG_32UA_G5,
    BIOZ_CFG_32UA_G2,
    BIOZ_CFG_32UA_G1,
    BIOZ_CFG_12_8UA_G10,
    BIOZ_CFG_12_8UA_G5,
    BIOZ_CFG_12_8UA_G2,
    BIOZ_CFG_12_8UA_G1,
    BIOZ_CFG_6_4UA_G10,
    BIOZ_CFG_6_4UA_G5,
    BIOZ_CFG_6_4UA_G2,
    BIOZ_CFG_6_4UA_G1,
    BIOZ_NUM_CONFIGS,
};

struct bioz_config_info {
    int         id;
    const char *label;
    double      i_drive;    /* amps */
    double      gain;       /* V/V  */
    uint8_t     reg_0x22;   /* drive current */
    uint8_t     reg_0x24;   /* gain + AHPF   */
};

extern const struct bioz_config_info bioz_configs[BIOZ_NUM_CONFIGS];

// Sagedusvaliku seaded
enum bioz_freq {
    FREQ_1KHZ = 0,
    FREQ_50KHZ,
    FREQ_100KHZ,
    NUM_FREQS
};

struct freq_config_info {
    const char *label;
    uint8_t pll_cfg1;
    uint8_t pll_cfg2;
    uint8_t bioz_cfg1_bg_only;
    uint8_t bioz_cfg1_enable;
};

extern const struct freq_config_info freq_configs[NUM_FREQS];

struct bioz_cal_info {
    double slope;
    double intercept;
};

extern const struct bioz_cal_info bioz_calibrations[NUM_FREQS][BIOZ_NUM_CONFIGS];

int max30009_set_frequency(int freq_idx);
int max30009_get_frequency(void);


int max30009_set_config(int config_id);
int max30009_fifo_read(uint8_t *buf, uint16_t num_bytes);
int max30009_get_bioz_sample(struct bioz_sample *sample);

#endif /* MAX30009_BIOZ_H */