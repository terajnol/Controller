#include "common.h"
#include "controller.h"
#include "breathing.h"
#include "platform.h"
#include "platform_config.h"

#include <compute_motor.h>
#include <math.h>


static float EoI_ratio;       
static float FR_pm;           
static float VTe_mL; 
static float VTi_mL; 
static float VMe_Lpm; 
static float Pcrete_cmH2O; 
static float Pplat_cmH2O; 
static float PEP_cmH2O; 
static float PEP_cmH2O_samples[MAX_PEP_SAMPLES]; 
static unsigned int PEP_cmH2O_samples_index; 
static float Pplat_cmH2O_samples[MAX_PPLAT_SAMPLES]; 
static unsigned int Pplat_cmH2O_samples_index; 

static void init_sample_PEP_cmH2O()
{
    //Samples PEP for a rolling average 
  PEP_cmH2O_samples_index = 0;
  for(int i = 0; i < MAX_PEP_SAMPLES; i++)
    PEP_cmH2O_samples[i] = 0;

}

static void sample_PEP_cmH2O( float Paw_cmH2O)
{
  PEP_cmH2O_samples[PEP_cmH2O_samples_index] = Paw_cmH2O;
  PEP_cmH2O_samples_index = (PEP_cmH2O_samples_index + 1) % MAX_PEP_SAMPLES; 
}

static float get_PEP_avg_cmH2O()
{
  float sum_PEP = 0;
  for(int i=0; i < MAX_PEP_SAMPLES; i++)
  {
    sum_PEP += PEP_cmH2O_samples[i];
  }
  return (sum_PEP / MAX_PEP_SAMPLES);
}

static void init_sample_Pplat_cmH2O()
{
    //Samples Pplat for a rolling average 
  Pplat_cmH2O_samples_index = 0;
  for(int i = 0; i < MAX_PPLAT_SAMPLES; i++)
    Pplat_cmH2O_samples[i] = 0;

}

static void sample_Pplat_cmH2O( float Paw_cmH2O)
{
  Pplat_cmH2O_samples[Pplat_cmH2O_samples_index] = Paw_cmH2O;
  Pplat_cmH2O_samples_index = (Pplat_cmH2O_samples_index + 1) % MAX_PPLAT_SAMPLES; 
}

static float get_Pplat_avg_cmH2O()
{
  float sum_Pplat = 0;
  for(int i=0; i < MAX_PPLAT_SAMPLES; i++)
  {
    sum_Pplat += Pplat_cmH2O_samples[i];
  }
  return (sum_Pplat / MAX_PPLAT_SAMPLES);
}
static float VTe_start_mL=0.;



typedef enum { Insufflation, Plateau, Exhalation, Finished } BreathingState;

static BreathingState _state; 

static uint32_t       _state_start_ms; 
static uint32_t       _cycle_start_ms;





static uint32_t _motor_steps_us[MAX_MOTOR_STEPS] = {0};  // TODO: Make it configurable with a define. This represent a physical limit a the system.
static uint32_t _steps;


// static float A_calibrated;
// static float B_calibrated;

// #define MAX_FLOW_SAMPLES	        400
// #define FLOW_SAMPLING_PERIOD_MS   5
// static float 			_flow_samples[MAX_FLOW_SAMPLES];		//  used as buffer for flow analysis and motor command computing
// static uint32_t 	_flow_samples_count;



// static float compte_motor_step_time(uint32_t step_number, float desired_flow_Ls, float A, float B, float speed);
// static void adaptation(float target_flow_Lm, float* flow_samples, uint32_t nb_samples, float time_step_sec, float* A, float* B);
// static float linear_fit(float* samples, uint32_t samples_len, float time_step_sec, float* slope);
// static int32_t get_plateau(float* samples, uint32_t samples_len, float time_step_sec, uint8_t windows_number, uint32_t* low_bound, uint32_t* high_bound);


static void enter_state(BreathingState newState);


void breathing_run(void *args) {
  UNUSED(args);
  EventBits_t events;

  while(true) {
    brth_printf("BRTH: Standby\n");
    events= xEventGroupWaitBits(ctrlEventFlags, BREATHING_RUN_FLAG, pdFALSE, pdTRUE, portMAX_DELAY );
    brth_printf("BRTH: Started\n");

    // _flow_samples_count=0;
    // A_calibrated= 3.577;
    // B_calibrated= -0.455;

    motor_enable(true);

    EoI_ratio=0;
    FR_pm=0;
    VTe_mL=0;
    Pcrete_cmH2O=0;
    Pplat_cmH2O=0;
    PEP_cmH2O=0;



    do  {
      brth_printf("BRTH: start cycle\n");

      enter_state(Insufflation);

      // TODO: Take into account the time to compute adaptation for the FR calculation ??!!??

      // Get current controller settings
      uint32_t T        = get_setting_T_ms      ();
      float    VT       = get_setting_VT_mL     ();
      float    VM       = get_setting_Vmax_Lpm  ();
      float    Pmax     = get_setting_Pmax_cmH2O();
      uint32_t Tplat    = get_setting_Tplat_ms  ();
	 
      init_sample_PEP_cmH2O();
      init_sample_Pplat_cmH2O();

      brth_printf("BRTH: T     : %ld\n", T);
      brth_printf("BRTH: VT    : %ld\n", (uint32_t)(VT*100));
      brth_printf("BRTH: VM    : %ld\n", (uint32_t)(VM*100));
      brth_printf("BRTH: Pmax  : %ld\n", (uint32_t)(Pmax*100));
      brth_printf("BRTH: Tplat : %ld\n", Tplat);

      // Compute adaptation based on current settings and previous collected data if any.
      uint32_t Ti = T*1/3;  // TODO: Check how to calculate Tinsuflation
      brth_printf("BRTH: Ti    : %ld\n", Ti);

      // adaptation(VM, _flow_samples, _flow_samples_count, 0.001*FLOW_SAMPLING_PERIOD_MS, &A_calibrated, &B_calibrated);
      // _flow_samples_count = 0;

	  uint32_t d = 300;
	  unsigned int _steps = 4000;
	  //compute_constant_motor_steps(d, _steps, _motor_steps_us);
	  compute_motor_press_christophe(350000, 2000, 65000, 20, 14, 350000, 4000, _steps, _motor_steps_us);
	  brth_printf("T_C = %d Patmo = %d\n", (int) (read_temp_degreeC()*100), (int) (read_Patmo_mbar()*100));

      // Start Inhalation
      valve_inhale();
      motor_press(_motor_steps_us, _steps);
      reset_Vol_mL();
      brth_print("BRTH: Insuflation\n");      
      while(Insufflation == _state ) {
          if (Pmax <= read_Paw_cmH2O()) {
              brth_printf("BRTH: Paw [%ld]> Pmax --> Exhalation\n", (int32_t)(read_Paw_cmH2O()));
              enter_state(Exhalation);
              break;
          } else if (VT <= read_Vol_mL()) {
              brth_printf("BRTH: vol [%ld]>= VT --> Plateau\n", (int32_t)(read_Vol_mL()));
              enter_state(Plateau);
              break;
          } else 
          if( Ti <= (get_time_ms() - _cycle_start_ms) ) {
              brth_printf("BRTH: dt [%lu]>= Ti\n", (get_time_ms() - _cycle_start_ms));
              enter_state(Plateau);
              break;
          }
          // Sample flow for later adaptation.
          // if(_flow_samples_count<MAX_FLOW_SAMPLES) {
          //   _flow_samples[_flow_samples_count] = read_Pdiff_Lpm()/60.;  // in sls
          //   ++_flow_samples_count;          
          // }
          // wait_ms(FLOW_SAMPLING_PERIOD_MS);
          Pcrete_cmH2O = MAX(Pcrete_cmH2O, read_Paw_cmH2O());
          wait_ms(PERIOD_BREATING_MS);
      }
      motor_release();
      while(Plateau == _state) {
        if (Pmax <= read_Paw_cmH2O()) { 
            brth_print("BRTH: Paw > Pmax --> Exhalation\n");
            Pplat_cmH2O = get_Pplat_avg_cmH2O();
            enter_state(Exhalation);
        } else if ( is_command_Tpins_expired() && (Tplat <= (get_time_ms() - _state_start_ms)) ) {
            brth_print("BRTH: Tpins expired && (dt > Tplat)\n");
            Pplat_cmH2O = get_Pplat_avg_cmH2O();
            enter_state(Exhalation);
        }
        sample_Pplat_cmH2O(read_Paw_cmH2O());
        Pcrete_cmH2O = MAX(Pcrete_cmH2O, read_Paw_cmH2O());
        wait_ms(PERIOD_BREATING_MS);
      }
      VTi_mL= read_Vol_mL();
      valve_exhale();
      while(Exhalation == _state) { 
          if ( T <= (get_time_ms() - _cycle_start_ms )) { 
              uint32_t t_ms = get_time_ms();

	      PEP_cmH2O = get_PEP_avg_cmH2O();
              EoI_ratio =  (float)(t_ms-_cycle_start_ms)/(_state_start_ms-_cycle_start_ms);
              FR_pm     = 1./(((float)(t_ms-_cycle_start_ms))/1000/60);
              VTe_mL = VTe_start_mL - read_Vol_mL();
              VMe_Lpm   = (VTe_mL/1000) * FR_pm;

              xEventGroupSetBits(brthCycleState, BRTH_RESULT_UPDATED);

              // TODO regulation_pep();
              enter_state(Finished);
          }
	  sample_PEP_cmH2O(read_Paw_cmH2O());
	  wait_ms(PERIOD_BREATING_MS);
      }





      events= xEventGroupGetBits(ctrlEventFlags);
    } while ( ( events & BREATHING_RUN_FLAG ) != 0 );

    brth_printf("BRTH: Stopping\n");      

    wait_ms(200);
    xEventGroupSetBits(ctrlEventFlags, BREATHING_STOPPED_FLAG);

  }
}

static void enter_state(BreathingState newState) {
  _state= newState;
  _state_start_ms = get_time_ms();
  EventBits_t brthState =0;
  switch(_state) {
    case Insufflation:
      _cycle_start_ms = get_time_ms();
      brthState =  BRTH_CYCLE_INSUFLATION;
      brth_printf("BRTH: Insuflation\n");
      break;
    case Plateau:
      brthState =  BRTH_CYCLE_PLATEAU;
      brth_printf("BRTH: Plateau\n");
      break;
    case Exhalation:
      VTe_start_mL = read_Vol_mL();
      brthState =  BRTH_CYCLE_EXHALATION;
      brth_printf("BRTH: Exhalation\n");
      break;
    case Finished:
      brthState =  BRTH_CYCLE_FINISHED;
      brth_printf("BRTH: Finished\n");
      break;
  }
  // Inform system about current state
  xEventGroupClearBits(brthCycleState, (BRTH_CYCLE_INSUFLATION | BRTH_CYCLE_PLATEAU | BRTH_CYCLE_EXHALATION | BRTH_CYCLE_FINISHED) );
  xEventGroupSetBits(brthCycleState, brthState);
}



// static float compte_motor_step_time(uint32_t step_number, float desired_flow_Ls, float A, float B, float speed) {
// 	float res = (0.8*A*speed*speed*step_number) + B * speed;
// 	res = res / desired_flow_Ls;
// 	if (res * 1000000 < 200) {return 200;}
// 	else {return res * 1000000.;}
// }


// static void adaptation(float target_flow_Lm, float* flow_samples, uint32_t nb_samples, float time_step_sec, float* A, float* B) {
//   if(nb_samples==0) return;
// //************************************************* PID ZONE ********************************************//
// 		// Compute average flow and slope to adjust A_calibrated and B_calibrated
// 		float P_plateau_slope = 0.1;
// 		float P_plateau_mean = 0.2;
// 		uint32_t low;
// 		uint32_t high;
// 		if(get_plateau(flow_samples, nb_samples, time_step_sec, 10, &low, &high) == 0) {
// //			brth_printf("plateau found from sample %lu to %lu\n", low, high);
// 		} else {
// //			brth_printf("plateau NOT found, considering from sample %lu to %lu\n", low, high);
// 		}
// 		float plateau_slope = linear_fit(flow_samples+low, high-low-1, time_step_sec, &plateau_slope);
// 		float plateau_mean = 0;
// 		for(uint32_t i=low; i<high; i++) {
// 			plateau_mean += flow_samples[i];
// 		}
// 		plateau_mean = plateau_mean/(high-low);
// //		brth_printf("plateau slope : %ld\n",(int32_t)(1000*plateau_slope));
// //		brth_printf("plateau mean : %ld\n",(int32_t)(1000*plateau_mean));

// 		float error_mean = plateau_mean - (target_flow_Lm/60.);

// 		*A += plateau_slope * P_plateau_slope;
// 		*B += error_mean * P_plateau_mean;
// //		brth_printf("A = %ld\n", (int32_t)(1000*(*A)));
// //		brth_printf("B = %ld\n", (int32_t)(1000*(*B)));

// }

// // Compute slope of samples fetched with specified time_step
// // Returns 	R  if fit is ok
// // 			-1 if fit is not possible
// static float linear_fit(float* samples, uint32_t samples_len, float time_step_sec, float* slope){
// 	float sumx=0,sumy=0,sumxy=0,sumx2=0, sumy2=0;
// 	for(uint32_t i=0;i<samples_len;i++) {
// 		sumx  = sumx + (float)i * time_step_sec;
// 		sumx2 = sumx2 + (float)i*time_step_sec*(float)i*time_step_sec;
// 		sumy  = sumy + *(samples+i);
// 		sumy2 = sumy2 + (*(samples+i)) * (*(samples+i));
// 		sumxy = sumxy + (float)i*(time_step_sec)* (*(samples+i));
// 	}
// 	float denom = (samples_len * sumx2 - (sumx * sumx));
// 	if(denom == 0.) {
// //		brth_printf("Calibration of A is not possible\n");
// 		return 1;
// 	}
// 	// compute slope a
// 	*slope = (samples_len * sumxy  -  sumx * sumy) / denom;

// 	// compute correlation coefficient
// 	return (sumxy - sumx * sumy / samples_len) / sqrtf((sumx2 - (sumx*sumx)/samples_len) * (sumy2 - (sumy*sumy)/samples_len));
// }

// static int32_t get_plateau(float* samples, uint32_t samples_len, float time_step_sec, uint8_t windows_number, uint32_t* low_bound, uint32_t* high_bound){
// 	if(windows_number < 2 || windows_number > 30) {return -1;}
// 	float slopes[30];
// 	*high_bound = samples_len-1;
// 	// Compute slope for time windows to detect when signal start increasing/decreasing
// 	for(uint32_t window=0; window<windows_number; window++) {
// 		float r = linear_fit(samples+window*(samples_len/windows_number), samples_len/windows_number, time_step_sec, slopes+window);
// //		brth_printf("%ld    ", (int32_t)(*(slopes+window) * 1000));
// 	}
// //	brth_printf("\n");
// 	for(uint32_t window=1; window<windows_number; window++) {
// 		float delta_slope = slopes[window-1] - slopes[window];
// 		if(delta_slope > 1.) {
// 			*low_bound = (uint32_t)((samples_len/windows_number)*(window+1));
// //			brth_printf("plateau begin at %lu over %lu points\n", *low_bound, (uint32_t)samples_len);
// 			return 0;
// 		}
// 	}
// 	*low_bound = (uint32_t)(samples_len/2);
// //	brth_printf("No plateau found\n");
// 	return 1;
// }





float get_breathing_EoI_ratio()     { return EoI_ratio; }
float get_breathing_FR_pm()         { return FR_pm; }
float get_breathing_VTe_mL()        { return VTe_mL; }
float get_breathing_VTi_mL()        { return VTi_mL; }
float get_breathing_VMe_Lpm()       { return VMe_Lpm; }
float get_breathing_Pcrete_cmH2O()  { return Pcrete_cmH2O; }
float get_Pplat_cmH20()             { return Pplat_cmH2O; }
float get_PEP_cmH2O()               { return PEP_cmH2O; }


