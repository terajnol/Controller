#include "controller.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "configuration.h"
#include "ihm_communication.h"
#include "sensing.h"
#include "lowlevel.h"
#include "simple_indicators.h"

// INIT

#define INIT_STR_SIZE 80
char init_str[INIT_STR_SIZE+1] = ""; // leaving space for off by 1 errors in code

const char *get_init_str() { return init_str; }

// RESP

float EoI_ratio    = 0.f;
float FR_pm        = 0.f;

uint32_t Tpins_ms = 0;
uint32_t Tpexp_ms = 0;

bool pause_insp(int t_ms)
{
    Tpins_ms = get_time_ms()+t_ms;
    return true;
}

bool pause_exp(int t_ms)
{
    Tpexp_ms = get_time_ms()+t_ms;
    return true;
}

void check(int* bits, int bit, bool success)
{
    if ((*bits &   (1 << bit)) && !success) {
         *bits &= ~(1 << bit);
    }
}

bool sensor_test(float(*sensor)(), float min, float max, float maxstddev)
{
    // Sample sensor
    const int samples = 10;
    float value[samples], stddev = 0., sumX=0., sumX2=0., sumY=0., sumXY=0.;
    for (int i=0; i<samples; i++) {
        value[i] = (*sensor)();
        if (value[i] < min || max < value[i]) {
            return false;
        }
        sumX  += i;   //  45 for 10 samples
        sumX2 += i*i; // 285 for 10 samples
        sumY  += value[i];
        sumXY += value[i]*i;
        wait_ms(1);
    }
    // Fit a line to account for rapidly changing data such as Pdiff at start of Exhale
    float b = (samples*sumXY-sumX*sumY)/(samples*sumX2-sumX*sumX);
    float a = (sumY-b*sumX)/samples;

    // Compute standard deviation to line fit
    for (int i=0; i<samples; i++) {
        float fit = a+b*i;
        stddev += pow(value[i] - fit, 2);
    }
    stddev = sqrtf(stddev / samples);

    return maxstddev < stddev;
}

int self_tests()
{
    DEBUG_PRINT("Start self tests");
    int test_bits = 0xFFFFFFFF;

    // TODO test 'Arret imminent' ?

    printf("Buzzer low\n");
    check(&test_bits, 1, buzzer_low      (On )); wait_ms(1000);
    check(&test_bits, 1, buzzer_low      (Off)); // start pos

    printf("Buzzer medium\n");
    check(&test_bits, 1, buzzer_medium      (On )); wait_ms(1000);
    check(&test_bits, 1, buzzer_medium      (Off)); // start pos

    printf("Buzzer high\n");
    check(&test_bits, 1, buzzer_high      (On )); wait_ms(1000);
    check(&test_bits, 1, buzzer_high      (Off)); // start pos

    printf("Red light\n");
    check(&test_bits, 2, light_red   (On )); wait_ms(1000);
    check(&test_bits, 2, light_red   (Off)); // start pos

    printf("Yellow light\n");
    check(&test_bits,  9, light_yellow(On )); wait_ms(1000);
    check(&test_bits, 10, light_yellow(Off)); // start pos

    printf("Green light\n");
    check(&test_bits,  9, light_green(On )); wait_ms(1000);
    check(&test_bits, 10, light_green(Off)); // start pos

    check(&test_bits, 5, init_Pdiff());
    check(&test_bits, 6, init_Paw());
    check(&test_bits, 7, init_Patmo());

    check(&test_bits, 11, sensors_start());

    check(&test_bits, 5, sensor_test(read_Pdiff_Lpm , -100,  100, 2)); printf("Rest    Pdiff  Lpm:%+.1g\n", read_Pdiff_Lpm ());
    check(&test_bits, 6, sensor_test(read_Paw_cmH2O ,  -20,  100, 2)); printf("Rest    Paw  cmH2O:%+.1g\n", read_Paw_cmH2O ());
    check(&test_bits, 7, sensor_test(read_Patmo_mbar,  900, 1100, 2)); printf("Rest    Patmo mbar:%+.1g\n", read_Patmo_mbar());

    check(&test_bits, 3, init_valve());
    check(&test_bits, 3, valve_exhale());

    check(&test_bits, 4, init_motor());
    printf("Exhale  Pdiff  Lpm:%+.1g\n", read_Pdiff_Lpm());
//    check(&test_bits, 4, motor_release(get_time_ms()+900));
    check(&test_bits, 4, motor_release());
    printf("Motor release 900ms\n");
    wait_ms(1000);
    check(&test_bits, 4, motor_stop());
    printf("Release Pdiff  Lpm:%+.1g\n", read_Pdiff_Lpm());
    check(&test_bits, 3, valve_inhale());
    printf("Inhale  Pdiff  Lpm:%+.1g\n", read_Pdiff_Lpm());
    // check(&test_bits, 4, motor_press(get_setting_Vmax_Lpm())); // 8steps/ms
    // printf("Motor press 400steps\n");
    // wait_ms(50);
    printf("Press   Pdiff  Lpm:%+.1g\n", read_Pdiff_Lpm());
    check(&test_bits, 4, motor_stop());
    check(&test_bits, 3, valve_exhale()); // start pos
    printf("Exhale  Pdiff  Lpm:%+.1g\n", read_Pdiff_Lpm());

    check(&test_bits, 8, init_motor_pep());
    // TODO check(&test_bits, 8, motor_pep_...

    return test_bits;
}

RespirationState state = Unknown;

RespirationState current_respiration_state() { return state; }

long respi_start_ms = 0;
long state_start_ms = 0;

void enter_state(RespirationState new)
{
#ifndef NDEBUG
    const char* current = NULL;
    switch (state) {
    case Insufflation   : current = "Insufflation"   ; break;
    case Plateau        : current = "Plateau"        ; break;
    case Exhalation     : current = "Exhalation"     ; break;
    case ExhalationPause: current = "ExhalationPause"; break;
    default             : current = "<unknown>"      ; break;
    }
    switch (new) {
    case Insufflation   : DEBUG_PRINTF(" %s -> Insufflation"   , current); break;
    case Plateau        : DEBUG_PRINTF(" %s -> Plateau"        , current); break;
    case Exhalation     : DEBUG_PRINTF(" %s -> Exhalation"     , current); break;
    case ExhalationPause: DEBUG_PRINTF(" %s -> ExhalationPause", current); break;
    default             : DEBUG_PRINTF(" %s -> <unknown>"      , current); break;
    }
#endif
    state = new;
    state_start_ms = get_time_ms();
    if (state == Insufflation) {
        respi_start_ms = get_time_ms();
    }
}


// TODO
//#ifndef NTESTS
//static float    T    ;
//static float    VT   ;
//static float    VM   ;
//static float    Pmax ;
//static uint32_t Tplat;
//#endif

static uint16_t _motor_steps_us[MOTOR_MAX];

void cycle_respiration()
{
//#ifdef NTESTS
    const uint32_t T     = get_setting_T_ms      ();
    const float    VT    = get_setting_VT_mL     ();
    const float    VM    = get_setting_Vmax_Lpm  ();
    const float    Pmax  = get_setting_Pmax_cmH2O();
    const uint32_t Tplat = get_setting_Tplat_ms  ();
//#endif
    if (Unknown == state) {
        send_INIT(get_init_str());
        enter_state(Insufflation);
    }
    else if (Insufflation == state) {
        valve_inhale();
        if (Pmax <= get_sensed_P_cmH2O()) {
            enter_state(Exhalation);
        }
        if (VT <= get_sensed_VTi_mL()) { // TODO RCM? motor_pos > pos(V) in case Pdiff understimates VT
            enter_state(Plateau);
        }
        for(int t=0; t<3000; ++t) { _motor_steps_us[t]= 400; }
        motor_press(_motor_steps_us, 3000);
//        motor_press(VM);
    }
    else if (Plateau == state) {
        valve_inhale();
        if (Pmax <= get_sensed_P_cmH2O()
            || (state_start_ms + MAX(Tplat,Tpins_ms)) <= get_time_ms()) { // TODO check Tpins_ms < first_pause_ms+5000
            enter_state(Exhalation);
        }
//        motor_release(respi_start_ms+(T-BAVU_REST_MS)); // TODO Check wrap-around
        motor_release(); // TODO Check wrap-around
    }
    else if (Exhalation == state) {
        valve_exhale();
        if ((respi_start_ms + MAX(T,Tpexp_ms)) <= get_time_ms()) { // TODO check Tpexp_ms < first_pause_ms+5000
            uint32_t t_ms = get_time_ms();

            EoI_ratio =  (float)(t_ms-state_start_ms)/(state_start_ms-respi_start_ms);
            FR_pm     = 1./(((float)(t_ms-respi_start_ms))/1000/60);
            // TODO ...

            send_RESP(EoI_ratio, FR_pm, -get_sensed_VTe_mL(), get_sensed_VMe_Lpm(), get_sensed_Pcrete_cmH2O(), get_sensed_Pplat_cmH2O(), get_sensed_PEP_cmH2O());
            enter_state(Insufflation);
        }
        motor_release(respi_start_ms+(T-BAVU_REST_MS)); // TODO Check wrap-around
    }
}

// ================================================================================================
#ifndef NTESTS
#define PRINT(_name) _name() { fprintf(stderr,"- " #_name "\n");

bool PRINT(test_nominal_cycle)
// TODO
//    for (T    =  2; T    <=  3; T    +=  1) {
//    for (VT   =300; VT   <=600; VT   +=150) {
//    for (VM   = 30; VM   <= 60; VT   += 15) {
//    for (Pmax = 65; Pmax >= 50; Pmax -=  5) {
//    for (Tplat=100; Tplat<=300; Tplat+=100) {
    for (uint32_t t_ms=0; t_ms<10*get_setting_T_ms(); t_ms=wait_ms(1)) { // 1kHz
        sense_and_compute(current_respiration_state());
        cycle_respiration();
        uint32_t t = t_ms % get_setting_T_ms();
        if (t<get_setting_Tinsu_ms()
            && !(TEST_EQUALS(Insufflation, current_respiration_state()))) {
            return false;
        }
        if (get_setting_Tinsu_ms() < t && t < get_setting_Tinsu_ms()+get_setting_Tplat_ms()
            && !(TEST_EQUALS(Plateau, current_respiration_state()))) {
            return false;
        }
        if (get_setting_Tinsu_ms()+get_setting_Tplat_ms() < t
            && !(TEST_EQUALS(Exhalation, current_respiration_state()))) {
            return false;
        }
    }
//    }}}}}
    return true;
}

bool PRINT(TEST_CONTROLLER)
    return
        test_nominal_cycle() &&
        true;
}

#endif
