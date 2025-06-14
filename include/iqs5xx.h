//*****************************************************************************
//
//! The memory map registers, and bit definitions are defined in this header
//! file.  The access writes of the memory map are also indicated as follows:
//! (READ)          : Read only
//! (READ/WRITE)    : Read & Write
//! (READ/WRITE/E2) : Read, Write & default loaded from non-volatile memory
//
//*****************************************************************************

#pragma once
#define	END_WINDOW				(uint16_t)0xEEEE
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

// Single finger data
struct iqs5xx_finger {
    // Absolute X position
    uint16_t ax;
    // Absolute Y position
    uint16_t ay;
    // Touch strength
    uint16_t strength;
    // Touch area
    uint16_t area;
};

// Data read from the device
struct iqs5xx_rawdata {
    // Gesture events 0: Single tap, press and hold, swipe -x, swipe +x, swipe -y, swipe +y
    uint8_t gestures0;
    // Gesture events 1: 2 finger tap, scroll, zoom
    uint8_t gestures1;
    // System info 0
    uint8_t system_info0;
    // System info 1
    uint8_t system_info1;
    // Number of fingers
    uint8_t finger_count;
    // Relative X position
    int16_t rx;
    // Relative Y position
    int16_t ry;
    // Fingers
    struct iqs5xx_finger fingers[5];
};

// Callback
typedef void (*iqs5xx_trigger_handler_t)(const struct device *dev, const struct iqs5xx_rawdata *data);

struct iqs5xx_data {
    // I2C device
	const struct device *i2c;
    const struct device *dev;
    // Data ready callback
	struct gpio_callback dr_cb;
    // Data ready callback
    iqs5xx_trigger_handler_t data_ready_handler;
    // Device data
    struct iqs5xx_rawdata raw_data;
    // i2c mutex
    struct k_mutex i2c_mutex;
    // Work queue item for handling interrupts
    struct k_work work;
};

struct iqs5xx_config {
    // Data ready GPIO spec from devicetree
    const struct gpio_dt_spec dr;
    // Note: invert_x, invert_y removed for simplicity
    // Can be added back later when needed
};

// Register configuration structure
struct __attribute__((packed)) iqs5xx_reg_config {
    // Refresh rate when the device is active (ms interval)
    uint16_t    activeRefreshRate;
    // Refresh rate when the device is idling (ms interval)
    uint16_t    idleRefreshRate;
    // Which single finger gestures will be enabled
    uint8_t     singleFingerGestureMask;
    // Which multi finger gestures will be enabled
    uint8_t     multiFingerGestureMask;
    // Tap time in ms
    uint16_t    tapTime;
    // Tap distance in pixels
    uint16_t    tapDistance;
    // Touch multiplier
    uint8_t     touchMultiplier;
    // Prox debounce value
    uint8_t     debounce;
    // i2c timeout in ms
    uint8_t     i2cTimeout;
    // Filter settings
    uint8_t     filterSettings;
    uint8_t     filterDynBottomBeta;
    uint8_t     filterDynLowerSpeed;
    uint16_t    filterDynUpperSpeed;

    // Initial scroll distance (px)
    uint16_t    initScrollDistance;
};

// Returns the default register configuration
struct iqs5xx_reg_config iqs5xx_reg_config_default ();

/**
 * @brief Initializes registers
 *
 * @param dev
 * @param config
 * @return int
 */
int iqs5xx_registers_init (const struct device *dev, const struct iqs5xx_reg_config *config);

int iqs5xx_trigger_set(const struct device *dev, iqs5xx_trigger_handler_t handler);

// Byte swap macros
#define SWPEND16(n) ((n >> 8) | (n << 8))
#define SWPEND32(n) ( ((n >> 24) & 0x000000FF) | ((n >>  8) & 0x0000FF00) | \
                        ((n << 8) & 0x00FF0000) | ((n << 24) & 0xFF000000) )


#define AZOTEQ_IQS5XX_ADDR      0x74

//*****************************************************************************
//
//! ----------------------    IQS5xx-B000 BIT DEFINITIONS     -----------------
//
//*****************************************************************************

//
//! GestureEvents0 bit definitions
//
#define GESTURE_SWIPE_Y_NEG	    		0x20
#define GESTURE_SWIPE_Y_POS	    		0x10
#define GESTURE_SWIPE_X_POS      		0x08
#define GESTURE_SWIPE_X_NEG        		0x04
#define GESTURE_TAP_AND_HOLD     		0x02
#define GESTURE_SINGLE_TAP        		0x01
//
//! GesturesEvents1 bit definitions
//
#define GESTURE_ZOOM	          		0x04
#define GESTURE_SCROLLG		     		0x02
#define GESTURE_TWO_FINGER_TAP       	0x01
//
//! SystemInfo0 bit definitions
//
#define	SHOW_RESET				0x80
#define	ALP_REATI_OCCURRED		0x40
#define	ALP_ATI_ERROR			0x20
#define	REATI_OCCURRED			0x10
#define	ATI_ERROR				0x08
#define	CHARGING_MODE_2			0x04
#define CHARGING_MODE_1			0x02
#define	CHARGING_MODE_0			0x01
//
//! SystemInfo1 bit definitions
//
#define	SNAP_TOGGLE				0x10
#define	RR_MISSED				0x08
#define	TOO_MANY_FINGERS		0x04
#define PALM_DETECT				0x02
#define	TP_MOVEMENT				0x01
//
//! SystemControl0 bit definitions
//
#define ACK_RESET         		0x80
#define AUTO_ATI          		0x20
#define ALP_RESEED        		0x10
#define RESEED            		0x08
#define MODE_SELECT_2			0x04
#define MODE_SELECT_1			0x02
#define MODE_SELECT_0			0x01
//
//! SystemControl1 bit definitions
//
#define RESET_TP             	0x02
#define SUSPEND           		0x01
//
//! SystemConfig0 bit definitions
//
#define MANUAL_CONTROL     		0x80
#define SETUP_COMPLETE     		0x40
#define WDT_ENABLE        		0x20
#define ALP_REATI        		0x08
#define REATI            		0x04
#define IO_WAKEUP_SELECT   		0x02
#define IO_WAKE         		0x01
//
//! SystemConfig1 bit definitions
//
#define PROX_EVENT        		0x80
#define TOUCH_EVENT       		0x40
#define SNAP_EVENT        		0x20
#define ALP_PROX_EVENT    		0x10
#define REATI_EVENT      		0x08
#define TP_EVENT          		0x04
#define GESTURE_EVENT     		0x02
#define EVENT_MODE        		0x01
//
//! FilterSettings0 bit definitions
//
#define ALP_COUNT_FILTER    	0x08
#define IIR_SELECT			    0x04
#define MAV_FILTER     			0x02
#define IIR_FILTER     			0x01
//
//! ALPChannelSetup0 bit definitions
//
#define CHARGE_TYPE  			0x80
#define RX_GROUP        		0x40
#define PROX_REV       			0x20
#define ALP_ENABLE        		0x10
//
//! IQS525RxToTx bit definitions
//
#define RX7_TX2     			0x80
#define RX6_TX3     			0x40
#define RX5_TX4     			0x20
#define RX4_TX5    				0x10
#define RX3_TX6    				0x08
#define RX2_TX7     			0x04
#define RX1_TX8     			0x02
#define RX0_TX9     			0x01
//
//! HardwareSettingsA bit definitions
//
#define ND_ENABLE         		0x20
#define RX_FLOAT          		0x04
//
//! HardwareSettingsB bit definitions
//
#define CK_FREQ_2      			0x40
#define CK_FREQ_1     			0x20
#define CK_FREQ_0    			0x10
#define ANA_DEAD_TIME     		0x02
#define INCR_PHASE        		0x01
//
//! HardwareSettingsC bit definitions
//
#define STAB_TIME_1     		0x80
#define STAB_TIME_0     		0x40
#define OPAMP_BIAS_1   			0x20
#define OPAMP_BIAS_0     		0x10
#define VTRIP_3					0x08
#define VTRIP_2                 0x04
#define VTRIP_1                 0x02
#define VTRIP_0                 0x01
//
//! HardwareSettingsD bit definitions
//
#define UPLEN_2    				0x40
#define UPLEN_1   				0x20
#define UPLEN_0     			0x10
#define PASSLEN_2               0x04
#define PASSLEN_1               0x02
#define PASSLEN_0               0x01
//
//! XYConfig0 bit definitions
//
#define PALM_REJECT       		0x08
#define SWITCH_XY_AXIS    		0x04
#define FLIP_Y            		0x02
#define FLIP_X            		0x01
//
//! SFGestureEnable bit definitions
//
#define SWIPE_Y_MINUS_EN  		0x20
#define SWIPE_Y_PLUS_EN   		0x10
#define SWIPE_X_PLUS_EN   		0x08
#define SWIPE_X_MINUS_EN  		0x04
#define TAP_AND_HOLD_EN   		0x02
#define SINGLE_TAP_EN     		0x01
//
//! MFGestureEnable bit definitions
//
#define ZOOM_EN           		0x04
#define SCROLL_EN         		0x02
#define TWO_FINGER_TAP_EN 		0x01

//*****************************************************************************
//
//! ------------------    IQS5xx-B00 MEMORY MAP REGISTERS    ------------------
//
//*****************************************************************************

/******************** DEVICE INFO REGISTERS ***************************/
#define ProductNumber_adr		0x0000	//(READ)			//2 BYTES;
#define ProjectNumber_adr		0x0002	//(READ)			//2 BYTES;
#define MajorVersion_adr		0x0004	//(READ)
#define MinorVersion_adr		0x0005	//(READ)
#define BLStatus_adr			0x0006	//(READ)
/******************** ************************* ***************************/
#define MaxTouch_adr			0x000B	//(READ)
#define PrevCycleTime_adr		0x000C	//(READ)
/******************** GESTURES AND EVENT STATUS REGISTERS ***************************/
#define GestureEvents0_adr		0x000D	//(READ)
#define GestureEvents1_adr		0x000E	//(READ)
#define SystemInfo0_adr			0x000F	//(READ)
#define SystemInfo1_adr			0x0010	//(READ)
/******************** XY DATA REGISTERS ***************************/
#define NoOfFingers_adr			0x0011	//(READ)
#define RelativeX_adr			0x0012	//(READ)			//2 BYTES;
#define RelativeY_adr			0x0014	//(READ)		   	//2 BYTES;
/******************** INDIVIDUAL FINGER DATA ***************************/
#define AbsoluteX_adr			0x0016	//(READ) 2 BYTES	//ADD 0x0007 FOR FINGER 2; 0x000E FOR FINGER 3; 0x0015 FOR FINGER 4 AND 0x001C FOR FINGER 5
#define AbsoluteY_adr			0x0018	//(READ) 2 BYTES	//ADD 0x0007 FOR FINGER 2; 0x000E FOR FINGER 3; 0x0015 FOR FINGER 4 AND 0x001C FOR FINGER 5
#define TouchStrength_adr		0x001A	//(READ) 2 BYTES	//ADD 0x0007 FOR FINGER 2; 0x000E FOR FINGER 3; 0x0015 FOR FINGER 4 AND 0x001C FOR FINGER 5
#define Area_adr				0x001C	//(READ)			//ADD 0x0007 FOR FINGER 2; 0x000E FOR FINGER 3; 0x0015 FOR FINGER 4 AND 0x001C FOR FINGER 5
/******************** CHANNEL STATUS REGISTERS ***************************/
#define ProxStatus_adr			0x0039	//(READ)	  		//32 BYTES;
#define TouchStatus_adr			0x0059	//(READ)	 	    //30 BYTES;
#define SnapStatus_adr			0x0077	//(READ)		    //30 BYTES;
/******************** DATA STREAMING REGISTERS ***************************/
#define Counts_adr				0x0095	//(READ)	  		//300 BYTES;
#define Delta_adr				0x01C1	//(READ)	 		//300 BYTES;
#define ALPCount_adr			0x02ED	//(READ)	 		//2 BYTES;
#define ALPIndivCounts_adr		0x02EF	//(READ)	 		//20 BYTES;
#define References_adr			0x0303	//(READ/WRITE)		//300 BYTES;
#define ALPLTA_adr 				0x042F	//(READ/WRITE)		//2 BYTES;
/******************** SYSTEM CONTROL REGISTERS ***************************/
#define SystemControl0_adr 		0x0431	//(READ/WRITE)
#define SystemControl1_adr 		0x0432	//(READ/WRITE)
/******************** ATI SETTINGS REGISTERS ***************************/
#define ALPATIComp_adr 			0x0435	//(READ/WRITE)  	//10 BYTES;
#define	ATICompensation_adr		0x043F	//(READ/WRITE)  	//150 BYTES;
#define ATICAdjust_adr         	0x04D5	//(READ/WRITE/E2)	//150 BYTES;
#define GlobalATIC_adr         	0x056B	//(READ/WRITE/E2)
#define ALPATIC_adr				0x056C	//(READ/WRITE/E2)
#define ATITarget_adr			0x056D	//(READ/WRITE/E2)	//2 BYTES;
#define ALPATITarget_adr		0x056F	//(READ/WRITE/E2)	//2 BYTES;
#define RefDriftLimit_adr		0x0571	//(READ/WRITE/E2)
#define ALPLTADriftLimit_adr	0x0572	//(READ/WRITE/E2)
#define ReATILowerLimit_adr		0x0573	//(READ/WRITE/E2)
#define ReATIUpperLimit_adr		0x0574	//(READ/WRITE/E2)
#define MaxCountLimit_adr		0x0575	//(READ/WRITE/E2)	//2 BYTES;
#define ReATIRetryTime_adr		0x0577	//(READ/WRITE/E2)
/******************** TIMING SETTINGS REGISTERS ***************************/
#define ActiveRR_adr		0x057A	//(READ/WRITE/E2)   //2 BYTES;
#define	IdleTouchRR_adr			0x057C	//(READ/WRITE/E2)	//2 BYTES;
#define	IdleRR_adr				0x057E	//(READ/WRITE/E2)	//2 BYTES;
#define	LP1RR_adr				0x0580	//(READ/WRITE/E2)	//2 BYTES;
#define	LP2RR_adr				0x0582	//(READ/WRITE/E2)	//2 BYTES;
#define	ActiveTimeout_adr		0x0584	//(READ/WRITE/E2)
#define IdleTouchTimeout_adr	0x0585	//(READ/WRITE/E2)
#define	IdleTimeout_adr			0x0586	//(READ/WRITE/E2)
#define	LP1Timeout_adr			0x0587	//(READ/WRITE/E2)
#define	RefUpdateTime_adr		0x0588	//(READ/WRITE/E2)
#define	SnapTimeout_adr			0x0589	//(READ/WRITE/E2)
#define	I2CTimeout_adr			0x058A	//(READ/WRITE/E2)
/******************** SYSTEM CONFIG REGISTERS ***************************/
#define SystemConfig0_adr  		0x058E	//(READ/WRITE/E2)
#define SystemConfig1_adr  		0x058F	//(READ/WRITE/E2)
/******************** THRESHOLD SETTINGS REGISTERS ***************************/
#define SnapThreshold_adr      	0x0592	//(READ/WRITE/E2)   //2 BYTES;
#define	ProxThreshold_adr		0x0594	//(READ/WRITE/E2)
#define	ALPProxThreshold_adr	0x0595	//(READ/WRITE/E2)
#define	GlobalTouchSet_adr		0x0596	//(READ/WRITE/E2)
#define	GlobalTouchClear_adr	0x0597	//(READ/WRITE/E2)
#define	IndivTouchAdjust_adr	0x0598	//(READ/WRITE/E2)	//150 BYTES;
/******************** FILTER SETTINGS REGISTERS ***************************/
#define	FilterSettings0_adr		0x0632	//(READ/WRITE/E2)
#define	XYStaticBeta_adr		0x0633	//(READ/WRITE/E2)
#define	ALPCountBeta_adr		0x0634	//(READ/WRITE/E2)
#define	ALP1LTABeta_adr			0x0635	//(READ/WRITE/E2)
#define	ALP2LTABeta_adr			0x0636	//(READ/WRITE/E2)
#define	DynamicBottomBeta_adr	0x0637	//(READ/WRITE/E2)
#define	DynamicLowerSpeed_adr	0x0638	//(READ/WRITE/E2)
#define	DynamicUpperSpeed_adr	0x0639	//(READ/WRITE/E2)   //2 BYTES;
/******************** CHANNEL SET UP (RX-TX MAPPING) REGISTERS ***************************/
#define	TotalRx_adr				0x063D	//(READ/WRITE/E2)
#define	TotalTx_adr				0x063E	//(READ/WRITE/E2)
#define	RxMapping_adr			0x063F	//(READ/WRITE/E2)	//10 BYTES;
#define	TxMapping_adr			0x0649	//(READ/WRITE/E2)	//15 BYTES;
#define	ALPChannelSetup0_adr	0x0658	//(READ/WRITE/E2)
#define	ALPRxSelect_adr			0x0659	//(READ/WRITE/E2)	//2 BYTES;
#define	ALPTxSelect_adr			0x065B	//(READ/WRITE/E2)	//2 BYTES;
#define IQS525RxToTx_adr		0x065D  //(READ/WRITE/E2)
/******************** HARDWARE SETTINGS REGISTERS ***************************/
#define	HardwareSettingsA_adr	0x065F	//(READ/WRITE/E2)
#define	HardwareSettingsB1_adr	0x0660	//(READ/WRITE/E2)
#define	HardwareSettingsB2_adr	0x0661	//(READ/WRITE/E2)
#define	HardwareSettingsC1_adr	0x0662	//(READ/WRITE/E2)
#define	HardwareSettingsC2_adr	0x0663	//(READ/WRITE/E2)
#define	HardwareSettingsD1_adr	0x0664	//(READ/WRITE/E2)
#define	HardwareSettingsD2_adr	0x0665	//(READ/WRITE/E2)
/******************** XY CONFIG REGISTERS ***************************/
#define	XYConfig0_adr			0x0669	//(READ/WRITE/E2)
#define	MaxMultitouches_adr		0x066A	//(READ/WRITE/E2)
#define	FingerSplitFactor_adr	0x066B	//(READ/WRITE/E2)
#define	PalmRejectThreshold_adr	0x066C	//(READ/WRITE/E2)
#define	PalmRejectTimeout_adr	0x066D	//(READ/WRITE/E2)
#define	XResolution_adr			0x066E	//(READ/WRITE/E2)	//2 BYTES;
#define	YResolution_adr			0x0670	//(READ/WRITE/E2)	//2 BYTES;
#define	StationaryTouchThr_adr	0x0672	//(READ/WRITE/E2)
/*********************************************************************/
#define	DefaultReadAdr_adr		0x0675	//(READ/WRITE/E2)
/******************** DEBOUNCE SETTING REGISTERS ***************************/
#define	ProxDb_adr				0x0679	//(READ/WRITE/E2)
#define	TouchSnapDb_adr			0x067A	//(READ/WRITE/E2)
/******************** CHANNEL CONFIG REGISTERS ***************************/
#define	ActiveChannels_adr		0x067B	//(READ/WRITE/E2)	//30 BYTES;
#define	SnapChannels_adr		0x0699	//(READ/WRITE/E2)   //30 BYTES;
/******************** GESTURE SETTING REGISTERS ***************************/
#define	SFGestureEnable_adr		0x06B7	//(READ/WRITE/E2)
#define	MFGestureEnable_adr		0x06B8	//(READ/WRITE/E2)
#define	TapTime_adr				0x06B9	//(READ/WRITE/E2)	//2 BYTES;
#define	TapDistance_adr			0x06BB	//(READ/WRITE/E2)	//2 BYTES;
#define HoldTime_adr           	0x06BD	//(READ/WRITE/E2)   //2 BYTES;
#define	SwipeInitTime_adr		0x06BF	//(READ/WRITE/E2)	//2 BYTES;
#define	SwipeInitDistance_adr	0x06C1	//(READ/WRITE/E2)	//2 BYTES;
#define	SwipeConsTime_adr		0x06C2	//(READ/WRITE/E2)	//2 BYTES;
#define	SwipeConsDistance_adr	0x06C5	//(READ/WRITE/E2)	//2 BYTES;
#define	SwipeAngle_adr			0x06C7	//(READ/WRITE/E2)
#define	ScrollInitDistance_adr	0x06C8	//(READ/WRITE/E2)	//2 BYTES;
#define	ScrollAngle_adr			0x06CA	//(READ/WRITE/E2)
#define	ZoomInitDistance_adr	0x06CB	//(READ/WRITE/E2)	//2 BYTES;
#define	ZoomConsDistance_adr	0x06CD	//(READ/WRITE/E2)	//2 BYTES;


// Register dumping

// Start dump from 04D5
#define IQS5XX_REG_DUMP_START_ADDRESS   0x04D5
// Write 504 bytes
#define IQS5XX_REG_DUMP_SIZE            504
// Dump data
extern const unsigned char _iqs5xx_regdump[IQS5XX_REG_DUMP_SIZE];
