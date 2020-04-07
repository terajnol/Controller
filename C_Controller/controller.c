#include <math.h>

#include "controller.h"

#include "ihm_communication.h"

#include "controller_settings.h"

#include "hardware_simulation.h"

int FR_pm      =  18;
int VT_mL      = 300;
int PEP_cmH2O  =   5;
int Vmax_Lpm   =  60;
int Tplat_ms   = 400;

int Pmax_cmH2O =  60;
int Pmin_cmH2O =  20;
int VTmin_mL   = 400;
int FRmin_pm   =  10;
int VMmin_Lm   =   5;

int Tpins_ms   =   0;
int Tpexp_ms   =   0;
int Tpbip_ms   =   0;

const int PEPmax_cmH2O = 2;
const int PEPmin_cmH2O = 2;

enum State { Insufflation, Plateau, Exhalation, ExhalationPEP } current = Exhalation;

// INIT

const char* init_str = "simulation";

// DATA

int P_cmH2O  = 0;
int VolM_Lpm = 0;
int Vol_mL   = 0;

// RESP

int IE           = 0;
int FRs_pm       = 0;
int VTe_mL       = 0;
int VM_Lm        = 0;
int Pcrete_cmH2O = 0;
int Pplat_cmH2O  = 0;
int PEPs_cmH2O   = 0;

void sense_and_compute()
{
    static int sent_DATA_ms = 0;

    // TODO Sense Paw, Patmo, Pdiff, ...
    // TODO Compute Q, ...

    if ((sent_DATA_ms+50 < get_time_ms())
        && send_DATA(P_cmH2O, VolM_Lpm, Vol_mL, Pplat_cmH2O, PEP_cmH2O)) {
        sent_DATA_ms = get_time_ms();
    }
}

void cycle_respiration()
{
    static int sent_RESP_ms = 0;
    int period_ms = 60 * 1000 / FR_pm;
    float end_insufflation = 0.5f - ((float)Tplat_ms / period_ms);
    float cycle_pos = (float)(get_time_ms() % period_ms) / period_ms; // 0 cycle start => 1 cycle end
    if (cycle_pos < end_insufflation)
    {
        float insufflation_pos = cycle_pos / end_insufflation; // 0 start insufflation => 1 end insufflation
        P_cmH2O = (int)(Pmin_cmH2O + sqrtf(insufflation_pos) * (Pmax_cmH2O - Pmin_cmH2O));
    }
    else if (cycle_pos < 0.5) // Plateau
    {
        P_cmH2O = (int)(0.9f * Pmax_cmH2O);
    }
    else
    {
        P_cmH2O = (int)(0.9f * Pmax_cmH2O - sqrtf(cycle_pos * 2.f - 1.f) * (0.9f * Pmax_cmH2O - Pmin_cmH2O));
    }


    if ((sent_RESP_ms+2000 < get_time_ms()) // TODO Insufflation, Plateau, Exhalation, ExhalationPEP
        && send_RESP(IE, FRs_pm, VTe_mL, VM_Lm, Pcrete_cmH2O, Pplat_cmH2O, PEP_cmH2O)) {
        sent_RESP_ms = get_time_ms();
    }
}
