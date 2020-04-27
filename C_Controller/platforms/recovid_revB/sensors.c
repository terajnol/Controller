#include "recovid_revB.h"
#include "platform.h"
#include "recovid.h"

typedef enum {
	STOPPED,
	STOPPING,
	REQ_SDP_MEASUREMENT,
	READ_SDP_MEASUREMENT,
	READ_NPA_MEASUREMENT
} sensors_state_t;


static I2C_HandleTypeDef* _i2c= NULL;

static const uint8_t _sdp_reset_req[1]  = { 0xFE };
static const uint8_t _sdp_readAUR_req[1]  = { 0xE5 };
static uint8_t _sdp_writeAUR_req[3]  = { 0xE4, 0x00, 0x00};

static const uint8_t _sdp_measurement_req[1] 	= { 0xF1 };
static uint8_t       _sdp_measurement_buffer[3] = { 0 };
static uint8_t       _sdp_AUR_buffer[3] 		= { 0 };

static uint8_t       _npa_measurement_buffer[2]	= { 0 };

static volatile float _current_flow_slm;
static volatile float _current_Paw_cmH2O;
static volatile float _current_vol_mL;

volatile sensors_state_t _sensor_state;

static volatile uint16_t _hyperfrish_sdp_time;



static void process_i2c_callback(I2C_HandleTypeDef *hi2c);
static inline uint16_t get_time_us() { return timer_us.Instance->CNT; }

bool init_sensors() {
  if(_i2c!=NULL) return true;

	// Scan I2C bus
	// TODO: Check if all sensors are responding.
	for (int t = 1; t < 127; ++t) {
			if(HAL_I2C_IsDeviceReady(&sensors_i2c, (uint16_t)(t<<1), 2, 2) == HAL_OK) {
				dbg_printf("Found device at address: %02X\n", t);
			}

		}



	// First try to complete pending sdp rad request if any !!!
	if(HAL_I2C_Master_Receive(&sensors_i2c, ADDR_SPD610, (uint8_t*) _sdp_measurement_buffer, sizeof(_sdp_measurement_buffer), 1000)!= HAL_I2C_ERROR_NONE) {
		// No pending read :-). keep going
	}

	// Reset SDP
	if(HAL_I2C_Master_Transmit(&sensors_i2c, ADDR_SPD610 , (uint8_t*) _sdp_reset_req, sizeof(_sdp_reset_req), 1000 )!= HAL_I2C_ERROR_NONE) {
		return false;
	}


	HAL_Delay(100);
	// Now read sdp advanced user register
	if(HAL_I2C_Master_Transmit(&sensors_i2c, ADDR_SPD610 , (uint8_t*) _sdp_readAUR_req, sizeof(_sdp_readAUR_req), 1000 )!= HAL_I2C_ERROR_NONE) {
		return false;
	}
	HAL_Delay(100);
	if(HAL_I2C_Master_Receive(&sensors_i2c, ADDR_SPD610 , (uint8_t*) _sdp_AUR_buffer, sizeof(_sdp_AUR_buffer), 1000 )!= HAL_I2C_ERROR_NONE) {
		return false;
	}
	// print AUR (Advances User Register)
	uint16_t sdp_aur = (uint16_t)((_sdp_AUR_buffer[0] << 8) | _sdp_AUR_buffer[1]);
//	printf("sdp AUR = %d\n", (uint16_t)(sdp_aur));
	uint16_t sdp_aur_no_i2c_hold = sdp_aur & 0xFFFD;
	_sdp_writeAUR_req[1] = (uint16_t)(sdp_aur_no_i2c_hold >> 8);
	_sdp_writeAUR_req[2] = (uint16_t)(sdp_aur_no_i2c_hold & 0xFF);
	// Now disable i2c hold master mode
	if(HAL_I2C_Master_Transmit(&sensors_i2c, ADDR_SPD610 , (uint8_t*) _sdp_writeAUR_req, sizeof(_sdp_writeAUR_req), 1000 )!= HAL_I2C_ERROR_NONE) {
		return false;
	}
	// Sensors settle time
	HAL_Delay(100);

	_i2c = &sensors_i2c;


	_i2c->MasterTxCpltCallback=process_i2c_callback;
	_i2c->MasterRxCpltCallback=process_i2c_callback;
	_i2c->ErrorCallback=process_i2c_callback;

	return true;
}


//! \returns false in case of hardware failure
bool is_Pdiff_ok() {
  return _i2c!=NULL;
}
bool is_Paw_ok() {
  return _i2c!=NULL;
}
bool is_Patmo_ok() {
  return _i2c!=NULL;
}

//! \returns the airflow corresponding to a pressure difference in Liters / minute
float read_Pdiff_Lpm() {
	// TODO: correct with ambiant pressure and T°
  return _current_flow_slm;
}

//! \returns the sensed pressure in cmH2O (1,019mbar in standard conditions)
float read_Paw_cmH2O() {
  return _current_Paw_cmH2O;
}

//! \returns the atmospheric pressure in mbar
float read_Patmo_mbar() {
  return 0;
}

 //! \returns the current integrated volume
float read_Vol_mL() {
	return _current_vol_mL;
}

 //! reset current integrated volume to 0
void reset_Vol_mL() {
	_current_vol_mL = 0;
}

bool sensors_start() {
    // Start the sensor state machine.
    // This state machine is managed in the I2C interupt routine.
		HAL_TIM_Base_Start(&timer_us);
	_sensor_state= REQ_SDP_MEASUREMENT;
	HAL_I2C_Master_Transmit_IT(_i2c, ADDR_SPD610 , (uint8_t*) _sdp_measurement_req, sizeof(_sdp_measurement_req) );
	return true;
}

bool sensors_stop() {
    // Stop the sensor state machine.
	__disable_irq();
	_sensor_state= STOPPED;
	__enable_irq();
	return true;
}

static void process_i2c_callback(I2C_HandleTypeDef *hi2c) {
	// TODO: First flow integration will not be correct !!! Find a way to indicate that this is the first sampling !!
	static	uint32_t hyperfrish_sdp = 0;

	switch (_sensor_state) {
	case STOPPING:
		_sensor_state=STOPPED;
		return;
	case STOPPED:
		// TODO: remove
		light_red(On);
		return;
	case READ_NPA_MEASUREMENT:
		if (HAL_I2C_GetError(hi2c) != HAL_I2C_ERROR_NONE) {
			// Retry
			HAL_I2C_Master_Receive_DMA(hi2c, ADDR_NPA700B , (uint8_t*) _npa_measurement_buffer, sizeof(_npa_measurement_buffer) );
			break;
		}
		if (HAL_I2C_GetError(hi2c) != HAL_I2C_ERROR_NONE) {
			// TODO: Manage error
			_sensor_state= STOPPED;
			break;
		}
		if (HAL_I2C_GetError(hi2c) == HAL_I2C_ERROR_NONE) {
			if( (_npa_measurement_buffer[0]>>6)==0) {
				uint16_t praw =  (((uint16_t)_npa_measurement_buffer[0]) << 8 | _npa_measurement_buffer[1]) & 0x3FFF;
				_current_Paw_cmH2O = 1.01972 * ((float) 160*( praw - 1638.)/13107.);
			} else if((_npa_measurement_buffer[0]>>6)==3) {
				// TODO: Manage error status !!
			}
		}
		_sensor_state= READ_SDP_MEASUREMENT;
		HAL_I2C_Master_Receive_DMA(hi2c, ADDR_SPD610, (uint8_t*) _sdp_measurement_buffer, sizeof(_sdp_measurement_buffer) );

		break;

	case REQ_SDP_MEASUREMENT:
		if (HAL_I2C_GetError(hi2c) != HAL_I2C_ERROR_NONE) {
			// retry
			HAL_I2C_Master_Transmit_IT(hi2c, ADDR_SPD610, (uint8_t*) _sdp_measurement_req, sizeof(_sdp_measurement_req) );
			break;
		}
		if (HAL_I2C_GetError(hi2c) != HAL_I2C_ERROR_NONE) {
			// TODO: Manage error
			_sensor_state= STOPPED;
			break;
		}
		_sensor_state= READ_SDP_MEASUREMENT;
		HAL_I2C_Master_Receive_DMA(hi2c, ADDR_SPD610 , (uint8_t*) _sdp_measurement_buffer, sizeof(_sdp_measurement_buffer) );
		break;

	case READ_SDP_MEASUREMENT:
		if (HAL_I2C_GetError(hi2c) != HAL_I2C_ERROR_NONE) {
			_sensor_state= READ_NPA_MEASUREMENT;
			HAL_I2C_Master_Receive_DMA(hi2c, ADDR_NPA700B , (uint8_t*) _npa_measurement_buffer, sizeof(_npa_measurement_buffer) );
			break;
		}
		if (HAL_I2C_GetError(hi2c) != HAL_I2C_ERROR_NONE) {
			// TODO: Manage error
			_sensor_state= STOPPED;
			break;
		}
		if(_sdp_measurement_buffer[0] != 0xFF || _sdp_measurement_buffer[1] != 0xFF || _sdp_measurement_buffer[2] != 0xFF){
			_hyperfrish_sdp_time= get_time_us() - hyperfrish_sdp;
			hyperfrish_sdp = get_time_us();
			int16_t dp_raw   = (int16_t)((((uint16_t)_sdp_measurement_buffer[0]) << 8) | (uint8_t)_sdp_measurement_buffer[1]);
			_current_flow_slm = -((float)dp_raw)/105.0;
      _current_vol_mL += (_current_flow_slm/60.) * ((float)_hyperfrish_sdp_time/1000);

			_sensor_state= REQ_SDP_MEASUREMENT;
			HAL_I2C_Master_Transmit_IT(hi2c, ADDR_SPD610, (uint8_t*) _sdp_measurement_req, sizeof(_sdp_measurement_req) );
		} else {
			_sensor_state= READ_NPA_MEASUREMENT;
			HAL_I2C_Master_Receive_DMA(hi2c, ADDR_NPA700B , (uint8_t*) _npa_measurement_buffer, sizeof(_npa_measurement_buffer) );
		}
		break;
	}
}
