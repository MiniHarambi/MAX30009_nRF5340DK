#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/console/console.h>
#include <stdbool.h>
#include <errno.h>
#include <math.h>

#include "spi_functions.h"
#include "max30009_bioz.h"

#define M_PI 3.14159265358979323846
LOG_MODULE_REGISTER(app, LOG_LEVEL_DBG);

#define NUM_SAMPLES 30

/* ------------------------------------------------------------------ */
/*  Arvutused                                                         */
/* ------------------------------------------------------------------ */

/// @brief Teisendab I ja Q näidud impedantsiks oomi-des, võttes arvesse seadistatud gain'i ja i_drive väärtust.
/// @param i_val I komponent
/// @param q_val Q komponent
/// @param gain Võimenduse väärtus
/// @param i_drive ergutusvoolu tugevuse väärtus
/// @return Impedants oomi-des
static double calc_z(int32_t i_val, int32_t q_val, double gain, double i_drive)
{
    double mag = sqrt((double)i_val * i_val + (double)q_val * q_val);
    double denom = 524288.0 * gain * (2.0 / M_PI) * i_drive;
    return mag / denom;
}

/// @brief Arvutab I ja Q näidude vahelise faasi
/// @param i_val I komponent
/// @param q_val Q komponent
/// @return Faas kraadides
static double calc_phase(int32_t i_val, int32_t q_val)
{
    return atan2((double)q_val, (double)i_val) * (180.0 / M_PI);
}

/// @brief Kontrollib, kas I või Q väärtus on lähedal maksimaalsele mõõdetavale väärtusele, mis viitab signaali küllastumisele.
/// @param i_val I komponent
/// @param q_val Q komponent
/// @return 1, kui väärtus on küllastatud, 0 muidu
static int is_saturated(int32_t i_val, int32_t q_val)
{
    if (i_val >= 507900 || q_val >= 507900) return 1;
    if (i_val <= -507900 || q_val <= -507900) return 1;
    return 0;
}

/* FIFO tühjendamine */
/// @brief Tühjendab MAX30009 FIFO, lugedes sellest näiteid seni, kuni FIFO on kas tühi või täis vigu
static void flush_fifo(void)
{
    uint8_t raw[6];

    for (int i = 0; i < 256; i++) {
        int ret = max30009_fifo_read(raw, sizeof(raw));
        if (ret) break;

        uint32_t word = ((uint32_t)raw[0] << 16) |
                        ((uint32_t)raw[1] << 8)  |
                         (uint32_t)raw[2];
        if (word >= 0xFFFFFE) break;
    }
}

/* ------------------------------------------------------------------ */
/*  Run one config                                                    */
/* ------------------------------------------------------------------ */
/// @brief Käivitab ühe konfiguratsiooni: seadistab MAX30009, laseb süsteemil stabiliseeruda, tühjendab FIFO,
/// kogub võendeid ja prindib need Exceli-sõbralikus formaadis koos arvutatud impedantsi ja faasiga
/// @param cfg_id Konfiguratsiooni ID, mis viitab bioz_configs tabeli indeksile
/// @return 0 edukas, negatiivne viga
static int run_config(int cfg_id)
{
    const struct bioz_config_info *cfg = &bioz_configs[cfg_id];
    const struct bioz_cal_info *cal = &bioz_calibrations[max30009_get_frequency()][cfg_id];
    const char *flabel = freq_configs[max30009_get_frequency()].label;
    int ret;

    printk("\n--- Configuring: %s @ %s ---\n", cfg->label, flabel);

    ret = max30009_set_config(cfg_id);
    if (ret) {
        printk("  FAILED: %d\n", ret);
        return ret;
    }

    uint8_t r22 = read_register(0x22);
    uint8_t r24 = read_register(0x24);
    printk("  0x22=0x%02X (want 0x%02X)  0x24=0x%02X (want 0x%02X)\n",
           r22, cfg->reg_0x22, r24, cfg->reg_0x24);

    printk("  Settling 2s...\n");
    k_msleep(2000);

    flush_fifo();
    printk("  FIFO flushed.\n");

    printk("  Collecting for 2s...\n");
    k_msleep(2000);

    int32_t si[NUM_SAMPLES];
    int32_t sq[NUM_SAMPLES];
    int good = 0;
    int sat_count = 0;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        struct bioz_sample s;
        ret = max30009_get_bioz_sample(&s);
        if (ret) {
            printk("  Sample %d read error: %d\n", i, ret);
            break;
        }
        si[i] = s.i;
        sq[i] = s.q;
        if (is_saturated(s.i, s.q)) sat_count++;
        good++;
    }

    printk("  Read %d/%d samples (%d saturated)\n", good, NUM_SAMPLES, sat_count);

    printk("--- EXCEL START: %s @ %s ---\n", cfg->label, flabel);
    printk("Sample\tI_raw\tQ_raw\tMagnitude\tZ_meas\tZ_corr\tPhase_deg\t"
           "Config\tFreq\tI_drive_uA\tGain_VV\tSaturated\n");

    for (int j = 0; j < good; j++) {
        double mag   = sqrt((double)si[j] * si[j] + (double)sq[j] * sq[j]);
        double z     = calc_z(si[j], sq[j], cfg->gain, cfg->i_drive);
        double z_corr = (z - cal->intercept) / cal->slope;
        double phase = calc_phase(si[j], sq[j]);
        int sat      = is_saturated(si[j], sq[j]);

        int z_i  = (int)z;
        int z_f  = (int)((z - z_i) * 100);
        if (z_f < 0) z_f = -z_f;

        /* Z_corr nii, nagu faasiga allpool tehtud*/
        int zc_neg = (z_corr < 0) ? 1 : 0;
        double zc_abs = zc_neg ? -z_corr : z_corr;
        int zc_i  = (int)zc_abs;
        int zc_f  = (int)((zc_abs - zc_i) * 100);

        /*eemalda märk; absoluut värätus; täis ja murdosa eraldi*/
        int p_neg = (phase < 0) ? 1 : 0;
        double p_abs = p_neg ? -phase : phase;
        int p_i  = (int)p_abs;
        int p_f  = (int)((p_abs - p_i) * 100);

        printk("%d\t%d\t%d\t%d\t%d.%02d\t%s%d.%02d\t%s%d.%02d\t%s\t%s\t%d\t%d\t%d\n",
               j + 1, si[j], sq[j], (int)mag,
               z_i, z_f,
               zc_neg ? "-" : "", zc_i, zc_f,
               p_neg ? "-" : "", p_i, p_f,
               cfg->label,
               flabel,
               (int)(cfg->i_drive * 1e6),
               (int)(cfg->gain),
               sat);
    }
    printk("--- EXCEL END: %s @ %s ---\n", cfg->label, flabel);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Näita registreid                                                  */
/* ------------------------------------------------------------------ */
/// @brief Loeb ja prindib kõik MAX30009 registrid, kasutades register_t struktuuri tabelit, mis sisaldab registreid ja nende nimesid
void dump_registers(void)
{
    printk("\n=== KÕIK REGISTRID ===\n");
    for (size_t i = 0; i < num_registers; i++) {
        uint8_t val = read_register(registers[i].address);
        printk("%-25s 0x%02X = 0x%02X\n",
               registers[i].name, registers[i].address, val);
    }
    printk("=====================\n");
}

/* ------------------------------------------------------------------ */
/*  Mõõda kõik konfiguratsioonid jooksva sageduse jaoks               */
/* ------------------------------------------------------------------ */
/// @brief Käivitab kõik konfiguratsioonid jooksva sageduse jaoks, kutsudes run_config funktsiooni iga konfiguratsiooni ID-ga, mis on määratletud bioz_configs tabelis
/// @param freq_idx Sageduse indeks, mis viitab freq_configs tabeli indeksile
static void run_all_configs(int freq_idx)
{
    if (freq_idx == FREQ_1KHZ) {
        run_config(BIOZ_CFG_64UA_G10);
        run_config(BIOZ_CFG_64UA_G5);
        run_config(BIOZ_CFG_64UA_G2);
        run_config(BIOZ_CFG_64UA_G1);
        run_config(BIOZ_CFG_32UA_G10);
        run_config(BIOZ_CFG_32UA_G5);
        run_config(BIOZ_CFG_32UA_G2);
        run_config(BIOZ_CFG_32UA_G1);
    } else if (freq_idx == FREQ_50KHZ) {
        run_config(BIOZ_CFG_256UA_G2);
        run_config(BIOZ_CFG_256UA_G1);
        run_config(BIOZ_CFG_128UA_G5);
        run_config(BIOZ_CFG_128UA_G2);
        run_config(BIOZ_CFG_128UA_G1);
        run_config(BIOZ_CFG_64UA_G10);
        run_config(BIOZ_CFG_64UA_G5);
        run_config(BIOZ_CFG_64UA_G2);
        run_config(BIOZ_CFG_64UA_G1);
        run_config(BIOZ_CFG_32UA_G10);
        run_config(BIOZ_CFG_32UA_G5);
        run_config(BIOZ_CFG_32UA_G2);
        run_config(BIOZ_CFG_32UA_G1);
    } else if (freq_idx == FREQ_100KHZ) {
        run_config(BIOZ_CFG_256UA_G2);
        run_config(BIOZ_CFG_256UA_G1);
        run_config(BIOZ_CFG_128UA_G5);
        run_config(BIOZ_CFG_128UA_G2);
        run_config(BIOZ_CFG_128UA_G1);
        run_config(BIOZ_CFG_64UA_G10);
        run_config(BIOZ_CFG_64UA_G5);
        run_config(BIOZ_CFG_64UA_G2);
        run_config(BIOZ_CFG_64UA_G1);
        run_config(BIOZ_CFG_32UA_G10);
        run_config(BIOZ_CFG_32UA_G5);
        run_config(BIOZ_CFG_32UA_G2);
        run_config(BIOZ_CFG_32UA_G1);
    }
}

/* ------------------------------------------------------------------ */
/*  Sageduse menu                                                    */
/* ------------------------------------------------------------------ */
/// @brief Kuvab sageduse valiku menüü, kus kasutaja saab valida ühe kolme sageduse vahel või väljuda.
/// @return Valitud sageduse indeks või -1, kui kasutaja valib väljumise.
static int show_freq_menu(void)
{
    while (true) {
        printk("\n");
        printk("=== SAGEDUSE VALIK ===\n");
        printk(" 1 - 1 kHz\n");
        printk(" 2 - 50 kHz\n");
        printk(" 3 - 100 kHz\n");
        printk(" q - Quit\n");
        printk("Select: ");

        uint8_t opt = console_getchar();
        printk("%c\n", opt);

        switch (opt) {
        case '1': return FREQ_1KHZ;
        case '2': return FREQ_50KHZ;
        case '3': return FREQ_100KHZ;
        case 'q':
        case 'Q': return -1;
        default:
            printk("Invalid option: %c\n", opt);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Konfiguratsiooni menu — sagedusepõhine                            */
/* ------------------------------------------------------------------ */
/// @brief Kuvab konfiguratsiooni valiku menüü, mis on kohandatud vastavalt valitud sagedusele, ja võimaldab kasutajal valida konfiguratsiooni,
/// mõõta kõiki konfiguratsioone, kuvada registreid, vahetada sagedust või väljuda
/// @param freq_idx Sageduse indeks, mis viitab freq_configs tabeli indeksile
/// @return Valitud konfiguratsiooni ID või -1, kui kasutaja valib väljumise
static int show_config_menu(int freq_idx)
{
    const char *flabel = freq_configs[freq_idx].label;

    while (true) {
        printk("\n");
        printk("=== MENÜÜ (sagedus: %s) ===\n", flabel);

        /* --- kuva menüü vastavalt sagedusele --- */
        if (freq_idx == FREQ_1KHZ) {
            printk(" 0 - 64uA  G10  (AVG Z viga -0,005 %)\n");
            printk(" 1 - 64uA  G5   (AVG Z viga -0,005 %)\n");
            printk(" 2 - 64uA  G2   (AVG Z viga  0,002 %)\n");
            printk(" 3 - 64uA  G1   (AVG Z viga -0,003 %)\n");
            printk(" 4 - 32uA  G10  (AVG Z viga -0,007 %)\n");
            printk(" 5 - 32uA  G5   (AVG Z viga  0,000 %)\n");
            printk(" 6 - 32uA  G2   (AVG Z viga -0,004 %)\n");
            printk(" 7 - 32uA  G1   (AVG Z viga  0,009 %)\n");
        } else if (freq_idx == FREQ_50KHZ) {
            printk(" 0 - 256uA G2   (AVG Z viga -0,112 %)\n");
            printk(" 1 - 256uA G1   (AVG Z viga -0,396 %)\n");
            printk(" 2 - 128uA G5   (AVG Z viga  0,102 %)\n");
            printk(" 3 - 128uA G2   (AVG Z viga  0,095 %)\n");
            printk(" 4 - 128uA G1   (AVG Z viga  0,098 %)\n");
            printk(" 5 - 64uA  G10  (AVG Z viga -0,070 %)\n");
            printk(" 6 - 64uA  G5   (AVG Z viga -0,077 %)\n");
            printk(" 7 - 64uA  G2   (AVG Z viga -0,076 %)\n");
            printk(" 8 - 64uA  G1   (AVG Z viga -0,041 %)\n");
            printk(" 9 - 32uA  G10  (AVG Z viga -0,080 %)\n");
            printk(" b - 32uA  G5   (AVG Z viga -0,073 %)\n");
            printk(" c - 32uA  G2   (AVG Z viga -0,038 %)\n");
            printk(" e - 32uA  G1   (AVG Z viga  0,144 %)\n");
        } else if (freq_idx == FREQ_100KHZ) {
            printk(" 0 - 256uA G2   (AVG Z viga -0,159 %)\n");
            printk(" 1 - 256uA G1   (AVG Z viga -0,157 %)\n");
            printk(" 2 - 128uA G5   (AVG Z viga  0,014 %)\n");
            printk(" 3 - 128uA G2   (AVG Z viga  0,014 %)\n");
            printk(" 4 - 128uA G1   (AVG Z viga  0,012 %)\n");
            printk(" 5 - 64uA  G10  (AVG Z viga -0,088 %)\n");
            printk(" 6 - 64uA  G5   (AVG Z viga -0,092 %)\n");
            printk(" 7 - 64uA  G2   (AVG Z viga -0,090 %)\n");
            printk(" 8 - 64uA  G1   (AVG Z viga -0,062 %)\n");
            printk(" 9 - 32uA  G10  (AVG Z viga -0,095 %)\n");
            printk(" b - 32uA  G5   (AVG Z viga -0,089 %)\n");
            printk(" c - 32uA  G2   (AVG Z viga -0,071 %)\n");
            printk(" e - 32uA  G1   (AVG Z viga  0,115 %)\n");
        }

        printk(" a - Mõõda kõikidel valikutel\n");
        printk(" d - Kuva registrid\n");
        printk(" v - Vaheta sagedust\n");
        printk(" q - Välju\n");
        printk("Select: ");

        uint8_t option = console_getchar();
        printk("%c\n", option);

        /* --- ühised käsud --- */
        if (option == 'a' || option == 'A') {
            run_all_configs(freq_idx);
            printk("\n=== COMPLETE ===\n");
            continue;
        }
        if (option == 'd' || option == 'D') {
            dump_registers();
            continue;
        }
        if (option == 'v' || option == 'V') {
            return 0;
        }
        if (option == 'q' || option == 'Q') {
            return 1;
        }

        /* --- sagedusepõhine konfig valik --- */
        int cfg_id = -1;

        if (freq_idx == FREQ_1KHZ) {
            switch (option) {
            case '0': cfg_id = BIOZ_CFG_64UA_G10; break;
            case '1': cfg_id = BIOZ_CFG_64UA_G5;  break;
            case '2': cfg_id = BIOZ_CFG_64UA_G2;  break;
            case '3': cfg_id = BIOZ_CFG_64UA_G1;  break;
            case '4': cfg_id = BIOZ_CFG_32UA_G10; break;
            case '5': cfg_id = BIOZ_CFG_32UA_G5;  break;
            case '6': cfg_id = BIOZ_CFG_32UA_G2;  break;
            case '7': cfg_id = BIOZ_CFG_32UA_G1;  break;
            }
        } else if (freq_idx == FREQ_50KHZ) {
            switch (option) {
            case '0': cfg_id = BIOZ_CFG_256UA_G2; break;
            case '1': cfg_id = BIOZ_CFG_256UA_G1; break;
            case '2': cfg_id = BIOZ_CFG_128UA_G5; break;
            case '3': cfg_id = BIOZ_CFG_128UA_G2; break;
            case '4': cfg_id = BIOZ_CFG_128UA_G1; break;
            case '5': cfg_id = BIOZ_CFG_64UA_G10; break;
            case '6': cfg_id = BIOZ_CFG_64UA_G5;  break;
            case '7': cfg_id = BIOZ_CFG_64UA_G2;  break;
            case '8': cfg_id = BIOZ_CFG_64UA_G1;  break;
            case '9': cfg_id = BIOZ_CFG_32UA_G10; break;
            case 'b':
            case 'B': cfg_id = BIOZ_CFG_32UA_G5;  break;
            case 'c':
            case 'C': cfg_id = BIOZ_CFG_32UA_G2;  break;
            case 'e':
            case 'E': cfg_id = BIOZ_CFG_32UA_G1;  break;
            }
        } else if (freq_idx == FREQ_100KHZ) {
            switch (option) {
            case '0': cfg_id = BIOZ_CFG_256UA_G2; break;
            case '1': cfg_id = BIOZ_CFG_256UA_G1; break;
            case '2': cfg_id = BIOZ_CFG_128UA_G5; break;
            case '3': cfg_id = BIOZ_CFG_128UA_G2; break;
            case '4': cfg_id = BIOZ_CFG_128UA_G1; break;
            case '5': cfg_id = BIOZ_CFG_64UA_G10; break;
            case '6': cfg_id = BIOZ_CFG_64UA_G5;  break;
            case '7': cfg_id = BIOZ_CFG_64UA_G2;  break;
            case '8': cfg_id = BIOZ_CFG_64UA_G1;  break;
            case '9': cfg_id = BIOZ_CFG_32UA_G10; break;
            case 'b':
            case 'B': cfg_id = BIOZ_CFG_32UA_G5;  break;
            case 'c':
            case 'C': cfg_id = BIOZ_CFG_32UA_G2;  break;
            case 'e':
            case 'E': cfg_id = BIOZ_CFG_32UA_G1;  break;
            }
        }

        if (cfg_id >= 0) {
            run_config(cfg_id);
        } else {
            printk("Invalid option: %c\n", option);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */
/// @brief Peamine funktsioon, mis käivitab programmi: kuvab tervitussõnumi, initsialiseerib SPI ja konsooli, kontrollib MAX30009 olemasolu,
/// ning seejärel kuvab sageduse valiku menüü, mille kaudu kasutaja saab valida sageduse ja konfiguratsiooni, mõõta näiteid ning kuvada registreid
/// @return 0 edukas, negatiivne viga
int main(void)
{
    printk("\n\n");
    printk("================================================\n");
    printk("  MAX30009 — Bioimpedantsi mõõtmine\n");
    printk("  %d võendit konfugratsiooni kohta\n", NUM_SAMPLES);
    printk("================================================\n\n");

    int ret = spi_init();
    if (ret) { printk("SPI fail\n"); return ret; }
    k_msleep(3000);

    uint8_t id = read_register(0xFF);
    printk("Part ID: 0x%02X %s\n", id, id == 0x42 ? "(OK)" : "(BAD)");
    if (id != 0x42) return -ENODEV;

    console_init();

    while (true) {
        int freq_idx = show_freq_menu();
        if (freq_idx < 0) {
            goto pause;
        }

        max30009_set_frequency(freq_idx);

        if (show_config_menu(freq_idx) == 1) {
            goto pause;
        }
        continue;

    pause:
        printk("\n=== Menüü suletud. Vajuta klahvi taasavamiseks ===\n");
        (void)console_getchar();
    }

    /* not reached */
    return 0;
}