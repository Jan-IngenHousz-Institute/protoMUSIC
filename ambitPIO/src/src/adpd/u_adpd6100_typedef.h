
struct light_gain_setting{
    uint8_t led_driver;
    uint8_t led_side;
    uint8_t gain;
    uint8_t current;
    uint8_t PD;
};


enum driverside{
    LED_A,
    LED_B
};

struct ts_led{    
	uint8_t driver2_current;        /*LED 2 output and current control 0 - 127 (200mA) */
	uint8_t driver1_current;        /* LED 1 output and current control 0 - 127 (200mA) */
    enum driverside led2_channel;   /* LED 2 Channel LED_A or LED_B  */
    enum driverside led1_channel;   /* LED 1 Channel LED_A or LED_B  */
    uint8_t led2_mode;      /* Choose the operation mode of LED2x. 0: high SNR mode. 1: low compliance mode. */
    uint8_t led1_mode;          /* Choose the operation mode of LED1x. 0: high SNR mode. 1: low compliance mode. */
};

struct ts_SNR{

    /**
     *  Channel 1 ambient cancellation DAC code from 0 μA to 300 μA
     * with 0.6 μA/LSB step. 
    */
   	uint16_t ch1_dac; 

	/**
	 * Channel 1 LED dc offset cancellation DAC code from 0 μA to 190 μA with 1.5 μA/LSB step.
     * Set to 0 to disable. 
	*/
    uint8_t ch1_led_offset;

    /**
     * Channel 2 ambient cancellation DAC code from 0 μA to 300 μA with 0.6 μA/LSB step. 
     */
	uint16_t ch2_dac;

    /**
     * Channel 2 LED dc offset cancellation DAC code from 0 μA to 190 μA with 1.5 μA/LSB step. Set to 0 to disable. 
    */
	uint8_t ch2_led_offset;
    
    /** 
     * Enable TIA saturation detection. Set to 1 to enable TIA saturation detection circuitry. 
     * Enables Channel 1 and also Channel 2 if Channel 2 is enabled.
     * 
     */
    uint8_t TIA_ceiling_det;

    /* 
     * Channel 2 integrator resistor or buffer gain.
     * 00: RIN = 400 kΩ or buffer gain = 1 (RF/RIN = 200 kΩ/200 kΩ). <<
     * 01: RIN = 200 kΩ or buffer gain = 2 (RF/RIN = 200 kΩ/100 kΩ).
     * 10: RIN = 100 kΩ or buffer gain = 1 (RF/RIN = 100 kΩ/100 kΩ).
     * 11: RIN = 50 kΩ or buffer gain = 2 (RF/RIN = 100 kΩ/50 kΩ).
     */
    uint8_t Ch2_R_int;

    /**Channel 1 integrator resistor or buffer gain.
     * 00: RIN = 400 kΩ or buffer gain = 1 (RF/RIN = 200 kΩ/200 kΩ). <<
     * 01: RIN = 200 kΩ or buffer gain = 2 (RF/RIN = 200 kΩ/100 kΩ).
     * 10: RIN = 100 kΩ or buffer gain = 1 (RF/RIN = 100 kΩ/100 kΩ).
     * 11: RIN = 50 kΩ or buffer gain = 2 (RF/RIN = 100 kΩ/50 kΩ).
    */
    uint8_t Ch1_R_int;
    
    /**TIA_VREF pulse control.
     * 0: no pulsing.
     * 1: pulse TIA_VREF based on modulate pulse. 
    */
    uint8_t Vref_pulse;
    /** Voltage trim for ref buffer.
     * 00: TIA_VREF = 0.8855 V, PD reverse bias = 600 mV.
     * 01: TIA_VREF = 0.8855 V, PD reverse bias = 400 mV.
     * 10: TIA_VREF = 0.8855 V, PD reverse bias = 200 mV. <<
     * 11: TIA_VREF = 1.265 V, PD reverse bias = 200 mV.
    */
    uint8_t Vref_VPD;
    /**TIA_VREF pulse alternate value.
     * 00: TIA_VREF = 0.8855 V, PD reverse bias = 600 mV.
     * 01: TIA_VREF = 0.8855 V, PD reverse bias = 400 mV.
     * 10: TIA_VREF = 0.8855 V, PD reverse bias = 200 mV. 
     * 11: TIA_VREF = 1.265 V, PD reverse bias = 200 mV. <<
    */
    uint8_t Vref_VPD_pulse;

    /** TIA resistor gain setting for Channel 2.
     * 000: 400 kΩ.
     * 001: 200 kΩ.  <<
     * 010: 100 kΩ.
     * 011: 50 kΩ.
     * 100: 25 kΩ.
     * 101: 12.5 kΩ. 
    */
   uint8_t TIA_gain_CH2;

    /** TIA resistor gain setting for Channel 1.
     * 000: 400 kΩ.
     * 001: 200 kΩ.  <<
     * 010: 100 kΩ.
     * 011: 50 kΩ.
     * 100: 25 kΩ.
     * 101: 12.5 kΩ. 
    */
   uint8_t TIA_gain_CH1;


    /** Channel2 integrator capacitor.
     * 0: 6.3 pF. <<
     * 1: 12.6 pF.
    */
   uint8_t C_int_CH2;

    /** Channel1 integrator capacitor.
     * 0: 6.3 pF. <<
     * 1: 12.6 pF.
    */
   uint8_t C_int_CH1;
};


struct ts_signal{
    // 0x0121 REG_TS_PATH_A_ADDR

    /** Precondition duration for this time slot. This value is in 2 μs
     * increments. A value of 0 skips the precondition state. 4 = 8us
    */
    uint8_t pre_condition_width;

    /**Select the control type for the ambient cancellation DAC.
     * 00: disable the ambient cancellation loop. <--
     * 01: enable coarse and fine loop.
     * 10: enable coarse loop only.
     * 11: enable MCU control.
    */
    uint8_t ac_type;
    /**Timeslot specific GPIO value for this time slot.*/
    uint8_t gpio_out;
    /**Convert integrator to buffer. = 0*/
    uint8_t INT2BUT;
    /**Bypass and input mux select. Integrator is either an integrator or
     * buffer based on mode and AFE_INT_C_BUF for the active time
     * slot.
     * 0x20: TIA, integrator/buffer, and ADC (2× TIA configuration). <--
     * 0x28: TIA, buffer, and ADC (1× TIA configuration).
     * 0x31: TIA, integrator, and ADC (1× TIA configuration).
     * 0x35: integrator and ADC.
     * 0x41: ADC.*/
    uint8_t AFE_PATh;

    //0x0122 INPUTS_A
    /**IN3 and IN4 input pair enable.
     * 0000: input pair disabled. IN3 and IN4 disconnected.
     * 0001: IN3 connected to Channel 1. IN4 disconnected.
     * 0010: IN3 connected to Channel 2. IN4 disconnected.
     * 0011: IN4 connected to Channel 1. IN3 disconnected.
     * 0100: IN4 connected to Channel 2. IN3 disconnected.
     * 0101: IN3 connected to Channel 1. IN4 connected to Channel 2.
     * 0110: IN4 connected to Channel 1. IN3 connected to Channel 2.
     * (Not implemented)
     * 0111: IN3 and IN4 connected to Channel 1. Single-ended or differential, based on PAIR34. None to channel 2.
     * 1000: IN3 and IN4 connected to Channel 2. Single-ended or differential, based on PAIR34.*/
    uint8_t IN34;
    /**IN1 and IN2 input pair enable.
     * 0000: input pair disabled. IN1 and IN2 disconnected.
     * 0001: IN1 connected to Channel 1. IN2 disconnected.
     * 0010: IN1 connected to Channel 2. IN2 disconnected.
     * 0011: IN2 connected to Channel 1. IN1 disconnected.
     * 0100: IN2 connected to Channel 2. IN1 disconnected.
     * 0101: IN1 connected to Channel 1. IN2 connected to Channel 2.
     * 0110: IN2 connected to Channel 1. IN1 connected to Channel 2.
     * * (Not implemented)
     * 0111: IN1 and IN2 connected to Channel 1. Single-ended or differential, based on PAIR12. None to channel 2.
     * 1000: IN1 and IN2 connected to Channel 2. Single-ended or differential, based on PAIR12.*/
    uint8_t IN12;

    //0x0123 CATHODE_A
    /**Precondition value for enabled inputs during this time slot.
     * 000: float inputs. <---
     * 001: precondition to VC1.
     * 010: precondition to VC2.
     * 011: precondition to VICM.
     * 100: precondition with TIA input.
     * 101: precondition with TIA_VREF.
     * 110: Precondition by shorting differential pair.
    */
    uint8_t pre_condition_type;

    /**VC2 pulse control.
     * 00: no pulsing.  <---
     * 01: alternate odd/even time slots.
     * 10: pulse to alternate value using modulate pulse.
     * 11: leave VC2 floating.*/
    uint8_t VC2_pulse_type;
    /**VC2 alternate pulsed state for this time slot.
     * 00: AVDD.  <---
     * 01: TIA_VREF.
     * 10: TIA_VREF + 215 mV (V_DELTA).
     * 11: GND.
    */
    uint8_t VC2_pulse_to;
    /**VC2 active state for this time slot.
     * 00: AVDD.  <---
     * 01: TIA_VREF.
     * 10: TIA_VREF + 215 mV (V_DELTA).
     * 11: GND.
    */
   uint8_t VC2_active;

   /**VC2 pulse control.
     * 00: no pulsing.  <---
     * 01: alternate odd/even time slots.
     * 10: pulse to alternate value using modulate pulse.
     * 11: leave VC2 floating.*/
    uint8_t VC1_pulse_type;
    /**VC2 alternate pulsed state for this time slot.
     * 00: AVDD.  <---
     * 01: TIA_VREF.
     * 10: TIA_VREF + 215 mV (V_DELTA).
     * 11: GND.
    */
    uint8_t VC1_pulse_to;
    /**VC2 active state for this time slot.
     * 00: AVDD.  <---
     * 01: TIA_VREF.
     * 10: TIA_VREF + 215 mV (V_DELTA).
     * 11: GND.
    */
   uint8_t VC1_active;

};

struct ts_DI_timing{
    /**Number of ADC cycles or acquisition width. Number of analog
     * integration cycles per ADC conversion or the acquisition width
     * for digital integration. A setting of 0 is not allowed.
     * = 1*/
    uint16_t num_integration;
    /**Number of sequence repeats. Total number of pulses =
     * NUM_INT_x × NUM_REPEAT_x. A setting of 0 is not allowed.
    = 1*/
    uint8_t num_repeats;

    /**Modulation connection type.
     * 00: TIA is continuously connected to input after precondition. No connection modulation. <<
     * 01: float type operation. Pulse connection from input to TIA with modulate pulse, floating between pulses.
     * 10: nonfloat type connection modulation. Pulse connection from input to TIA. Connect to precondition value between pulses.
    */
    uint8_t Modulation_type;

    /**Minimum period for pulse repetition. 
     * Override for the automatically calculated period.
     *  Used in float type operations to set the floating time of second and subsequent floats using the following formula: 
     * Float Time = MIN_PERIOD_x − MOD_WIDTH_x.
     * =  0
     */
    uint16_t period_min;
    /**LED pulse width. = 2*/
    uint16_t LED_pulse_width;
    /**LED pulse offset. = 0x10*/
    uint8_t LED_pulse_offset;

    /**LED pulse offset for the second LED phase. = 0x13*/
    uint8_t LED_pulse2_offset;
    /*Acquisition window lit offset for Time Slot x.
    = 0x26 */
    uint16_t LIT_OFFSET;
    /*Acquisition window Dark Offset 2 for Time Slot x.
    0x1*/
    uint16_t DARK_OFFSET2;
    /*Acquisition window Dark Offset 1 for Time Slot x.
    0x6*/
    uint8_t DARK_OFFSET1;
    //0x0120 TS_CTRL_A
    /**Subsample using DECIMATE_FACTOR_x. When this bit is set,
     * operate the time slot only once per (DECIMATE_FACTOR_X + 1) time slot sequences. 
     * This subsampling aligns to other time slots using the same decimate factor.
     * Subsampling skips DECMATE_FACTOR_x times and then executes the time slot. << 0*/
    uint8_t subsample_en;
    /**Channel 2 enable.
     * 0: Channel 2 disabled. <<
     * 1: Channel 2 enabled.
    */
    uint8_t ch2_en;
    /**Time slot sampling type.
     * 000: normal sampling mode.  <<
     * 001: two phase normal sampling mode.
     * 010: on-region digital integrate mode.
     * 011: two-region digital integrate mode.
    */
    uint8_t sample_type;
    /**Time Slot X offset in 64 × 960 kHz or 64 × (external 960 kHz)
     * cycles. = 0*/
    uint16_t ts_offset;

    /**
     * Dark data shift = 0
    */
    uint8_t dark_shift;
    /**
     * Dark data size = 0
    */
    uint8_t dark_size;
    /**
     * Signal data shift = 0
    */
    uint8_t signal_shift;
    /**
     * Signal data size = 3
    */
    uint8_t signal_size;
    /**
     * Lit data shift = 0
    */
    uint8_t lit_shift;
    /**
     * Signal data size = 0
    */
    uint8_t lit_size;


};


struct ts_AI_timing{
    /**Number of ADC cycles or acquisition width. Number of analog
     * integration cycles per ADC conversion or the acquisition width
     * for digital integration. A setting of 0 is not allowed.
     * = 1*/
    uint16_t num_integration;
    /**Number of sequence repeats. Total number of pulses =
     * NUM_INT_x × NUM_REPEAT_x. A setting of 0 is not allowed.
    = 1*/
    uint8_t num_repeats;

    /**Modulation connection type.
     * 00: TIA is continuously connected to input after precondition. No connection modulation. <<
     * 01: float type operation. Pulse connection from input to TIA with modulate pulse, floating between pulses.
     * 10: nonfloat type connection modulation. Pulse connection from input to TIA. Connect to precondition value between pulses.
    */
    uint8_t Modulation_type;

    /**Minimum period for pulse repetition. 
     * Override for the automatically calculated period.
     *  Used in float type operations to set the floating time of second and subsequent floats using the following formula: 
     * Float Time = MIN_PERIOD_x − MOD_WIDTH_x.
     * =  0
     */
    uint16_t period_min;
    /**LED pulse width. = 2*/
    uint16_t LED_pulse_width;
    /**LED pulse offset. = 0x10*/
    uint8_t LED_pulse_offset;

    /**LED pulse offset for the second LED phase. = 0x13*/
    uint8_t LED_pulse2_offset;

    //0x012E INTEG_WIDTH_A
    /**Use single integrator pulse.
     * 0: use both generated integrator clocks.    <<
     * 1: skip the second integrator clock.
    */
    uint8_t SINGLE_INTEG;
    /**Amplifier disables for power control. Set the appropriate bit to disable the Channel 2 amplifier in Time Slot x.
     * 0: TIA.   <<
     * 1: integrator.*/
    uint8_t CH2_DIS_AMP;
    /**Amplifier disables for power control. Set the appropriate bit to disable the Channel 1 amplifier in Time Slot x.
     * 0: TIA.   <<
     * 1: integrator.*/
    uint8_t CH1_DIS_AMP;
    /**ADC conversions per pulse. Number of conversions = ADC_COUNT + 1.*/
    uint8_t ADC_count;
    /**Integrator clock width. = 3us*/
    uint16_t integration_width;

    //0x012F INTEG_OFFSET_A
    /**Integrator clock coarse offset. 
     * 0xD
    */
    uint8_t offset_us;
    /**Integrator clock fine offset. << 0*/
    uint8_t offset_ns;

    uint16_t modulation_width;
    uint8_t modulation_offset;

    //0x0131 PATTERN1_A
    /**Four-pulse LED disable pattern. Set to 1 to disable the LED
     * pulse in the matching position in a group of four pulses. The LSB
     * maps to the first pulse.*/
    uint8_t pattern_LED_disable;
    /**Four-pulse modulation disable pattern. Set to 1 to disable the
     * modulation pulse in the matching position in a group of four
     * pulses. The LSB maps to the first pulse.*/
    uint8_t pattern_MOD_disable;
    /**Four-pulse subtract pattern. Set to 1 to negate the math
     * operation in the matching position in a group of four pulses.
     * The LSB maps to the first pulse.*/
    uint8_t pattern_subtract;
    /**Four-pulse integration reverse pattern. Set to 1 to reverse the
     * integrator pos/neg pulse order in the matching position in a
     * group of four pulses. The LSB maps to the first pulse.*/
    uint8_t pattern_reverse_int;

    //0x0120 TS_CTRL_A
    /**Subsample using DECIMATE_FACTOR_x. When this bit is set,
     * operate the time slot only once per (DECIMATE_FACTOR_X + 1) time slot sequences. 
     * This subsampling aligns to other time slots using the same decimate factor.
     * Subsampling skips DECMATE_FACTOR_x times and then executes the time slot. << 0*/
    uint8_t subsample_en;
    /**Channel 2 enable.
     * 0: Channel 2 disabled. <<
     * 1: Channel 2 enabled.
    */
    uint8_t ch2_en;
    /**Time slot sampling type.
     * 000: normal sampling mode.  <<
     * 001: two phase normal sampling mode.
     * 010: on-region digital integrate mode.
     * 011: two-region digital integrate mode.
    */
    uint8_t sample_type;
    /**Time Slot X offset in 64 × 960 kHz or 64 × (external 960 kHz)
     * cycles. = 0*/
    uint16_t ts_offset;
};

struct system_config{
    //0x0020 INPUT_SLEEP
    /**Input pair sleep state for IN3 and IN4 inputs.
     * 0x0: both inputs float.              <<
     * 0x1: floating short of IN3 to IN4. Only if PAIR34 is set to 1.
     * 0x2: IN3 and IN4 connected to VC1. Shorted together if PAIR34 is set to 1.
     * 0x3: IN3 and IN4 connected to VC2. Shorted together if PAIR34 is set to 1.
     * 0x4: IN3 connected to VC1. IN4 floating.
     * 0x5: IN3 connected to VC1. IN4 connected to VC2.
     * 0x6: IN3 connected to VC1. IN4 floating.
     * 0x7: IN3 connected to VC2. IN4 connected to VC1.
     * 0x8: IN3 floating. IN4 connected to VC1.
     * 0x9: IN3 floating. IN4 connected to VC2.
    */
    uint8_t IN34_sleep;
    /**Input pair sleep state for IN1 and IN2 inputs.
     * 0x0: both inputs float.              <<
     * 0x1: floating short of IN1 to IN2. Only if PAIR12 is set to 1.
     * 0x2: IN1 and IN2 connected to VC1. Shorted together if PAIR12 is set to 1.
     * 0x3: IN1 and IN2 connected to VC2. Shorted together if PAIR12 is set to 1.
     * 0x4: IN1 connected to VC1. IN2 floating.
     * 0x5: IN1 connected to VC1. IN2 connected to VC2.
     * 0x6: IN1 connected to VC1. IN2 floating.
     * 0x7: IN1 connected to VC2. IN2 connected to VC1.
     * 0x8: IN1 floating. IN2 connected to VC1.
     * 0x9: IN1 floating. IN2 connected to VC2.
    */
    uint8_t IN12_sleep;
    //0x0021 INPUT_CFG
    /**VC2 sleep state.
     * 00: VC2 set to AVDD during sleep.   <<
     * 01: VC2 set to GND during sleep.
     * 10: VC2 floating during sleep.
     * */
    uint8_t VC2_sleep;
    /**VC1 sleep state.
     * 00: VC1 set to AVDD during sleep.   <<
     * 01: VC1 set to GND during sleep.
     * 10: VC1 floating during sleep.
     * */
    uint8_t VC1_sleep;
    /**Input pair configuration.
     * 0: use as two single-ended inputs.   <<
     * 1: use as a differential pair.
     * */
    uint8_t PAIR34;
    /**Input pair configuration.
     * 0: use as two single-ended inputs.   <<
     * 1: use as a differential pair.
     * */
    uint8_t PAIR12;
    //0x0022 GPIO_CFG
    /**Slew control for GPIOx pins.
     * 00: slowest.   <<
     * 01: slow.
     * 10: fastest.
     * 11: fast.*/
    uint8_t GPIO_slew;
    /**Drive control for GPIOx pins.
     * 00: medium.  <<
     * 01: weak.
     * 10: strong.
     * 11: strong.*/
    uint8_t GPIO_drv;
    /**GPIO2 pin configuration.
     * 000: disabled (tristate, input buffer off).      <<
     * 001: enabled input.
     * 010: output–normal.
     * 011: output–inverted.
     * 100: pull-down only–normal.
     * 101: pull-down only–inverted.
     * 110: pull-up only–normal.
     * 111: pull-up only–inverted.
    */
    uint8_t GPIO2_cfg;
    /**GPIO1 pin configuration.
     * 000: disabled (tristate, input buffer off).      <<
     * 001: enabled input.
     * 010: output–normal.
     * 011: output–inverted.
     * 100: pull-down only–normal.
     * 101: pull-down only–inverted.
     * 110: pull-up only–normal.
     * 111: pull-up only–inverted.
    */
    uint8_t GPIO1_cfg;
    /**GPIO0 pin configuration.
     * 000: disabled (tristate, input buffer off).      <<
     * 001: enabled input.
     * 010: output–normal.
     * 011: output–inverted.
     * 100: pull-down only–normal.
     * 101: pull-down only–inverted.
     * 110: pull-up only–normal.
     * 111: pull-up only–inverted.
    */
    uint8_t GPIO0_cfg;
    /**GPIO1 output signal select.*/
    uint8_t GPIO1_output;
    /**GPIO0 output signal select.*/
    uint8_t GPIO0_output;
    /**GPIO2 output signal select.*/
    uint8_t GPIO2_output;
    /**Set to 0x0 if IOVDD of 3 V or higher is used. Default value of 0x1 is used for IOVDD lower than 3 V, because the typical value of IOVDD is 1.8 V.*/
    uint8_t IOVDD;
    /**Slew control for SPI pins.
     * 00: slowest.     <<
     * 01: slow.
     * 10: fastest.
     * 11: fast.*/
    uint8_t SPI_slew;
    /**Drive control for SPI pins.
     * 00: medium.      <<
     * 01: weak.
     * 10: strong.
     * 11: strong.*/
    uint8_t SPI_drv;
    /*External clock select.
    000: use internal clocks.
    001: use GPIO for low frequency oscillator (960 kHz). Timer clock also uses this as the source.
    010: use GPIO for high frequency oscillator (32 MHz).
    011: use GPIO for high frequency oscillator (32 MHz), and generate low frequency oscillator (1 MHz) from high frequency oscillator. This feature must be disabled when the ECG is enabled.
    100: use GPIO for timer clock, 32 kHz or 960 kHz.
     = 0*/
    uint8_t alt_clock;
    /*Alternate clock GPIO select.
    00: use GPIO0 for alternate clock.
    01: use GPIO1 for alternate clock.
    10: use GPIO2 for alternate clock.
    11: reserved.
     = 0
    */
    uint8_t alt_clock_gpio;
    /*Select low frequency clock between 960 kHz and 32 kHz. This bit must be used when ALT_CLOCKS is 3'b100.
    0: use the 32 kHz external source from GPIO as the timer clock.
    1: use the 960 kHz external source from GPIO as the low frequency clock.
     = 0*/
    uint8_t ext_clock_freq;
    /*Enable low frequency oscillator. This bit turns on the 960 kHz low frequency oscillator, which must be left running during all operations using this oscillator.
    = 0*/
    uint8_t internal_clock_en;


};



struct GPIO0_config{
   
     /**GPIO0 pin configuration.
     * 000: disabled (tristate, input buffer off).      <<
     * 001: enabled input.
     * 010: output–normal.
     * 011: output–inverted.
     * 100: pull-down only–normal.
     * 101: pull-down only–inverted.
     * 110: pull-up only–normal.
     * 111: pull-up only–inverted.
    */
    uint8_t GPIO0_cfg;

    /*GPIO0 output signal select.*/
    uint8_t GPIO0_output;

    /* External sync enable. When enabled, use the GPIO selected
    by EXT_SYNC_GPIO to trigger samples rather than the period
    counter. = 0(1)
    */
    uint8_t EXT_SYNC_EN;

    /* External sync GPIO select. = 0 (0, 01, 10)*/
    uint8_t SYNC_GPIO;

};


