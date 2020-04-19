#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include "platform.h"

//! motor map size
#define MOTOR_STEPS_MAX 4700

#define P_PLATEAU_SLOPE  0.1f // V1 was 0.05
#define P_PLATEAU_MEAN   0.2f // V1 was 0.1

#define MOTOR_STEP_TIME_US_MIN  110.f

#define CALIB_STEP_TIME_S  1.f/*s*/ / MOTOR_STEPS_MAX // V1 was 1/(MOTOR_STEPS_MAX*200/360) ?!
#define CALIB_MAGIC_RATIO  1.f // V1 was 0.8
#define CALIB_A 1.275f
#define CALIB_B 0.046f

// ------------------------------------------------------------------------------------------------
//! Environment simulation
//! \remark only for test purposes

extern float    LUNG_COMPLIANCE       ; //!< dV_mL/dP_cmH2O \see https://outcomerea.fr/docs/day2019/Forel_Mechanical_power.pdf
extern float    LUNG_COMPLIANCE_MAX   ;

extern uint32_t LUNG_EXHALE_MS        ;
extern uint32_t LUNG_EXHALE_MS_MAX    ; //!< time after which 99% of air exceeding lung's volume at rest should be exhaled

extern float    AIRWAYS_RESISTANCE    ; //!< cmH2O/Lpm \see https://outcomerea.fr/docs/day2019/Forel_Mechanical_power.pdf
extern float    AIRWAYS_RESISTANCE_MAX; //!< cmH2O/Lpm \see https://outcomerea.fr/docs/day2019/Forel_Mechanical_power.pdf

extern uint32_t BAVU_REST_MS          ;
extern float    BAVU_V_ML_MAX         ;
extern float    BAVU_Q_LPM_MAX        ; //!< due to BAVU perforation ?!
extern float    BAVU_VALVE_RATIO      ; //!< To simulate BAVU 'anti-retour' valve perforation

extern float    EXHAL_VALVE_P_RATIO   ; // TODO /!\ motor_pep_steps/cmH2O taking into account motor_pep + valve surface ratio

extern float    EXHAL_VALVE_RATIO     ;

extern float    PATMO_VARIATION_MBAR  ; // TODO Estimate required range to maintain precise measures and reliable alarms

#endif // CONFIGURATION_H
