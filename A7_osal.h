#ifndef __A7_OSAL_H
#define __A7_OSAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../tools/snack_wrapper.h"
#include "../tools/log_level.h"
#include "io_exp/demo.h"

#define	MS_US(T)						((T)*1000)						//ms转us
#define	IO_ENABLE						(1)								//使能电平
#define IO_DISABLE						(!IO_ENABLE)					//失能电平
#define MOVE_POS_ERR					(7)								//移动到固定位置时的允许误差
#define MOVE_FOREVER					(0xFFFF)						//无目标位置限制
#define DIFF_BOARD_IO_VALUE				(100)							//不同线路板IO的区分差值
#define BOARD_ID_RESOLUTION(id)     	(id / DIFF_BOARD_IO_VALUE)      //解析线路板id
#define PIN_ID_RESOLUTION(id)       	(id % DIFF_BOARD_IO_VALUE)      //解析线路板引脚id

/***********************************************************************************/
// 机器配置使能
typedef enum{
	//提示信号类
    CONFIG_ENABLE_SIGN_VOICE = 0,
    CONFIG_ENABLE_SIGN_DISPLAY,
    CONFIG_ENABLE_SIGN_PARK,
    CONFIG_ENABLE_SIGN_GATE_1,
	//水系统类
    CONFIG_ENABLE_WATER_PRERINSE,
    CONFIG_ENABLE_WATER_SHAMPOO_1,
    CONFIG_ENABLE_WATER_SHAMPOO_2,
    CONFIG_ENABLE_WATER_CHASSIS,
    CONFIG_ENABLE_WATER_FOAM_RINSE,
    CONFIG_ENABLE_WATER_WAXWATER,
    CONFIG_ENABLE_WATER_DRYIND_AGENT,
    CONFIG_ENABLE_WATER_RO,
	//毛刷类
    CONFIG_ENABLE_BRUSH_TOP,
    CONFIG_ENABLE_BRUSH_FRONT_SKIRT,
    CONFIG_ENABLE_BRUSH_BACK_SKIRT,
    CONFIG_ENABLE_BRUSH_SIDE_FL,
    CONFIG_ENABLE_BRUSH_SIDE_FR,
    CONFIG_ENABLE_BRUSH_SIDE_BL,
    CONFIG_ENABLE_BRUSH_SIDE_BR,
	//风干类
    CONFIG_ENABLE_DRYER_1,
    CONFIG_ENABLE_DRYER_2,
    CONFIG_ENABLE_DRYER_3,
    CONFIG_ENABLE_DRYER_4,
    CONFIG_ENABLE_DRYER_5,
	CONFIG_ENABLE_DRYER_6,
	//传感器监测类
    CONFIG_ENABLE_SENSOR_LOW_WATER_PRESS,
    CONFIG_ENABLE_SENSOR_HIGH_WATER_PRESS,
    CONFIG_ENABLE_SENSOR_RO_WATER_PRESS,
    CONFIG_ENABLE_SENSOR_PARKING_TO_LEFT,
    CONFIG_ENABLE_SENSOR_PARKING_TO_RIGHT,
    CONFIG_ENABLE_SENSOR_GROUND,
    CONFIG_ENABLE_SENSOR_COLLISION_FL,
    CONFIG_ENABLE_SENSOR_COLLISION_FR,
    CONFIG_ENABLE_SENSOR_LIFTER_LEFT_LOOSE,
    CONFIG_ENABLE_SENSOR_LIFTER_RIGHT_LOOSE,
	CONFIG_ENABLE_SENSOR_FRONT_BRUSH_CROOKED,
    CONFIG_ENABLE_SENSOR_FL_BRUSH_DOWN,
    CONFIG_ENABLE_SENSOR_FR_BRUSH_DOWN,
    CONFIG_ENABLE_SENSOR_BL_BRUSH_DOWN,
    CONFIG_ENABLE_SENSOR_BR_BRUSH_DOWN,
    CONFIG_ENABLE_SENSOR_SHAMPOO_1_LESS,
    CONFIG_ENABLE_SENSOR_SHAMPOO_2_LESS,
    CONFIG_ENABLE_SENSOR_WAXWATER_LESS,
    CONFIG_ENABLE_SENSOR_DRYIND_AGENT_LESS,
    CONFIG_ENABLE_SENSOR_LONG_LIMIT,
    CONFIG_ENABLE_SENSOR_HIGH_LIMIT,
	//其它
    CONFIG_ENABLE_CONVEYOR_1,
    CONFIG_ENABLE_CONVEYOR_2,
    CONFIG_ENABLE_CONVEYOR_3,
	HARDWARE_CONFIG_NUM,
} Type_ConfigHardwareType_Enum;
/***********************************************************************************/

/***********************************************************************************/
// 电机状态机枚举
typedef enum {
    MOTOR_STA_IDLE = 0,
    MOTOR_STA_HOLD_ON,
    MOTOR_STA_MOVE,
    MOTOR_STA_MOVE_FORE,
    MOTOR_STA_MOVE_POS,
    MOTOR_STA_MOVE_TIME,
    MOTOR_STA_PAUSE,
    MOTOR_STA_RESUME,
    MOTOR_STA_STOP,
    MOTOR_STA_FAULT,
	MOTOR_STA_ERROR,
} Type_MoveState_Enum;

// 控制类型枚举
typedef enum {
	CTL_NULL = 0,
	//对称机构（如左右侧刷）
	CTL_ONLY_LEFT,
	CTL_ONLY_RIGHT,
	CTL_BOTH,
	//非对称机构，部件序号（如传送带1，2，3）
	CTL_SECTION_1,
	CTL_SECTION_2,
	CTL_SECTION_3,
	CTL_SECTION_4,
	CTL_SECTION_5,
	CTL_SECTION_6,
	CTL_ALL_SECTION,
} Type_CtlType_Enum;

//限位模式枚举
typedef enum {
    MODE_SIGNAL_LIMIT = 0,
    MODE_LIMIT_PULSE_MIN,			//脉冲限位模式在触碰到正/反向限位传感器后同样会停止
	MODE_LIMIT_PULSE_MAX,
	MODE_LIMIT_PULSE_MIN_MAX,
} Type_LimitMode_Enum;

// 抽象模块枚举
typedef enum {
	OSAL_MODULE_DISPLAY = 0,
	OSAL_MODULE_VOICE,
	OSAL_MODULE_IO_BOARD,
	OSAL_MODULE_WATER,
	OSAL_MODULE_MOTOR,
} Type_OsalModule_Enum;
/***********************************************************************************/

/***********************************************************************************/
// 设备驱动索引
typedef enum {
    //变频器
	VFD_TOP_BRUSH = 0,
	VFD_SIDE_BRUSH,
	VFD_GANTRY,
	VFD_DRYER,
	DRIVER_VFD_NUM,

    //线圈（接触器/继电器）
	KM_SWITCH_TOP_BRUSH = DRIVER_VFD_NUM,
	KM_SWITCH_SIDE_BRUSH,
	KM_PUTTER,
	DRIVER_ALL_NUM,
} Type_DriverIndex_Enum;
/***********************************************************************************/

/***********************************************************************************/
// 输入口枚举
typedef enum {
	INPUT_IO_NULL = 0,
	// 业务主板

	// 1号IO扩展板
	BOARD1_INPUT_FRONT_WHEEL_LIMIT = 1*DIFF_BOARD_IO_VALUE + 1,	//前轮到位
	BOARD1_INPUT_REAR_WHEEL_LOCK1,		//后轮锁止1
	BOARD1_INPUT_REAR_WHEEL_LOCK2,		//后轮锁止2
	BOARD1_INPUT_OVERHEIGHT_DETECT,		//超高检测
	BOARD1_INPUT_RESERVE_1,
	BOARD1_INPUT_RESERVE_2,
	BOARD1_INPUT_RESERVE_3,
	BOARD1_INPUT_RESERVE_4,
	BOARD1_INPUT_REAR_LOCK_HOME,		//后轮锁原点
	BOARD1_INPUT_BUMPER_LEFT,			//左防撞胶条
	BOARD1_INPUT_BUMPER_RIGHT,			//右防撞胶条
	BOARD1_INPUT_GANTRY_ENCODER_PULSE,	//行走码盘
	BOARD1_INPUT_GANTRY_REV_LIMIT,		//龙门后限位
	BOARD1_INPUT_GANTRY_FWD_LIMIT,		//龙门前限位
	BOARD1_INPUT_LIFT_UP_LIMIT,			//升降上限位
	BOARD1_INPUT_LIFT_DOWN_LIMIT,		//升降下限位
	BOARD1_INPUT_TOP_BRUSH_COLLISION,	//顶刷防撞
	BOARD1_INPUT_BUMPER_ROD_LEFT,		//左防撞杆
	BOARD1_INPUT_BUMPER_ROD_RIGHT,		//右防撞杆
	BOARD1_INPUT_HEIGHT_CONTROL,		//高度控制
	BOARD1_INPUT_ESTOP,					//急停按钮
	BOARD1_INPUT_HALL_FEEDBACK1,		//霍尔反馈1
	BOARD1_INPUT_HALL_FEEDBACK2,		//霍尔反馈2
	BOARD1_INPUT_LIFT_ALARM_FEEDBACK,	//顶刷升降报警反馈
	BOARD1_INPUT_SIDE_BRUSH_OVERLOAD,	//侧刷过载
	BOARD1_INPUT_TOP_BRUSH_OVERLOAD,	//顶刷过载
	BOARD1_INPUT_WATER_PUMP_OVERLOAD,	//水泵过载
	BOARD1_INPUT_GANTRY_ALARM,			//行走报警反馈
	BOARD1_INPUT_FAN_ALARM,				//风机报警反馈
	BOARD1_INPUT_SIDE_BRUSH_ALARM,		//侧刷报警反馈
	BOARD1_INPUT_PIN_NUM,
} Type_InputIo_Enum;
/***********************************************************************************/

/***********************************************************************************/
// 输出口枚举
typedef enum {
	OUTPUT_IO_NULL = 0,
	// 业务主板

	// 1号IO扩展板
	BOARD1_OUTPUT_ENTRY_GREEN1 = 1*DIFF_BOARD_IO_VALUE + 1,	//入口绿灯 1
	BOARD1_OUTPUT_ENTRY_RED,			//入口红灯
	BOARD1_OUTPUT_ENTRY_GREEN2,			//入口绿灯 2
	BOARD1_OUTPUT_ENTRY_YELLOW,			//入口黄灯
	BOARD1_OUTPUT_ROD_EXTEND,			//电动推杆伸出
	BOARD1_OUTPUT_ROD_RETRACT,			//电动推杆缩回
	BOARD1_OUTPUT_TOP_LIFT_ENA,			//顶刷升降步进—使能
	BOARD1_OUTPUT_TOP_LIFT_DIR,			//顶刷升降步进—方向
	BOARD1_OUTPUT_RESERVE_1,
	BOARD1_OUTPUT_WATER_PUMP,			//水泵启动
	BOARD1_OUTPUT_BRUSH_FWD,			//刷子正转
	BOARD1_OUTPUT_BRUSH_REV,			//刷子反转
	BOARD1_OUTPUT_BRUSH_RST,			//刷子复位
	BOARD1_OUTPUT_GANTRY_FWD,			//龙门前进
	BOARD1_OUTPUT_GANTRY_REV,			//龙门后退
	BOARD1_OUTPUT_GANTRY_RST,			//龙门复位
	BOARD1_OUTPUT_WATER_CURTAIN,		//清水水帘阀
	BOARD1_OUTPUT_WATER_FOAM,			//泡沫+预洗液阀
	BOARD1_OUTPUT_WATER_BRUSH,			//侧刷冲水阀
	BOARD1_OUTPUT_WATER_HIGHPRES,		//高压冲洗阀
	BOARD1_OUTPUT_WATER_SPARE1,			//备用水阀 1
	BOARD1_OUTPUT_WATER_SPARE2,			//备用水阀 2
	BOARD1_OUTPUT_FAN_START,			//风机启动
	BOARD1_OUTPUT_FAN_RESET,			//风机复位
	BOARD1_OUTPUT_GANTRY_HIGH_SPEED,	//龙门行走高速
	BOARD1_OUTPUT_TOP_LIFT_PUL,			//顶刷升降步进—脉冲
	BOARD1_OUTPUT_PARAM_SEL,			//参数选择
	BOARD1_OUTPUT_SIDE_BRUSH_ACT,		//接触器 1—侧刷接
	BOARD1_OUTPUT_TOP_BRUSH_ACT,		//接触器 2—顶刷接
	BOARD1_OUTPUT_PIN_NUM,		
} Type_OutputIo_Enum;
/***********************************************************************************/

/***********************************************************************************/
// 子版id枚举
typedef enum {
	BOARD_NONE = 0,
	IO_BOARD1,						//子板调用的接口id号从1开始
	BOARD_NUMS,
} Type_Ioboards_Enum;
/***********************************************************************************/

/***********************************************************************************/
//voice module
typedef enum {
	A7_VOICE_POS_ENTRY = 0,				//入口处语音
	A7_VOICE_POS_EXIT,					//出口处语音
} Type_VoicePos_Enum;

typedef enum {
	A7_VOICE_SILENCE = 0,               // 静音
	A7_VOICE_CAR_FORWARD,               // 继续向前				（预备区）
	A7_VOICE_SLOW_BACKOFF,              // 缓慢后退				（预备区）
	A7_VOICE_CAR_LEFT,                  // 停车偏左				（预备区）
	A7_VOICE_CAR_RIGHT,                 // 停车偏右				（预备区）
	A7_VOICE_CAR_STOP,                  // 请停车				（预备区）
	A7_VOICE_WASH_START,            	// 开始洗车				（预备区）
	A7_VOICE_CAR_TOO_LONG,            	// 车辆超长				（预备区）
	// A7_VOICE_LEAVE_AT_ONCE,            	// 后方来车，请立即驶离	 （完成区）
	A7_VOICE_RESERVE,
	A7_VOICE_OVER_HEIGHT,               // 车辆超高				（预备区）
	A7_VOICE_COMPLETED,                 // 洗车已完成			（完成区）
	A7_VOICE_EXIT_CONGESTION,			// 出口拥堵				（完成区）
	A7_VOICE_WASH_EXCEPTION,            // 洗车异常				（预备区）
	A7_VOICE_HOMING,                    // 归位中				（预备区）
	A7_VOICE_PLEASE_PAY,				// 请扫码付费			（预备区）
	A7_VOICE_GO_READY_AREA,				// 请驶入洗车预备区	 	 （预备区）
} Type_A7Voice_Enum;

typedef struct {
	bool isInit;
	int (*init)();                      //voice module init
	int (*play)(Type_VoicePos_Enum pos, Type_A7Voice_Enum item);     //voice module play set code number
} Type_Voice_Def;

//dispaly module
typedef enum {
	// A7_PAUSE_SERVICE = 0,               // 暂停服务
	// A7_CAR_FORWARD,                     // 请前进
	// A7_CAR_BACKOFF,                     // 缓慢后退
	// A7_CAR_STOP,                        // 请停车
	// A7_CAR_TRANSFER,                    // 车辆传送
	// A7_CAR_READY_TRANSFER,              // 即将传送
	// A7_CAR_WAIT,                    	// 请稍等
	// A7_CAR_TOO_LONG,                    // 车辆超长
	// A7_SERVICE_EXCEPTION,               // 服务异常
	// A7_CAR_LEFT_SKEW,                   // 左侧停偏
	// A7_CAR_RIGHT_SKEW,                  // 右侧停偏
	// A7_DEV_RESET,                  		// 复位中
	// A7_CAR_MOVEE_ON,                  	// 继续前进
	// A7_SCAN_CODE,                     	// 请扫码
	// A7_OVER_HEIGHT,                     // 车辆超高
	// A7_SOON_TO_OPEN,					// 即将开业
	A7_PAUSE_SERVICE = 0,               // Closed
	A7_CAR_FORWARD,                     // Forward
	A7_CAR_MOVEE_ON,                  	// Continue Forwar
	A7_CAR_BACKOFF,                     // Back Up
	A7_CAR_STOP,                        // Stop
	A7_CAR_PARK_BRAKE,                  // Parking Brake
	A7_SCAN_CODE,                     	// Scan Code
	A7_CAR_READY_TRANSFER,              // Conveyor Starting
	A7_CAR_WASH_STARTING,               // Wash Starting
	A7_DEV_RESET,                  		// Resetting
	A7_CAR_LEFT_SKEW,                   // Move Right
	A7_CAR_RIGHT_SKEW,                  // Move Left
	A7_OVER_HEIGHT,                     // Overheight
	A7_CAR_TOO_LONG,                    // Overlength
	A7_CAR_WAIT,                    	// Please Wait
	A7_SERVICE_EXCEPTION,               // Out of Order
	A7_DISP_TEST,               		// Out of Order
} Type_A7Display_Enum;

typedef struct {
	bool isInit;
	int (*init)();                      //display module init
	int (*display)(Type_A7Display_Enum);//display module set code number
} Type_Display_Def;

//freq module
typedef enum {
	GET_VFD_STATE = 0,
	GET_VFD_CURRENT,
	GET_VFD_ERR_CODE,
} Type_GetVFDInfo_Enum;

typedef enum {
	SET_CLEAR_ERR = 0,
} Type_SetVFDInfo_Enum;

typedef struct {
	bool isInit;
	int (*init)();                      	//VFD module init
	int (*run)(Type_DriverIndex_Enum, int);	//VFD set run speed
	int (*get_info)(Type_DriverIndex_Enum, Type_GetVFDInfo_Enum);	//VFD get info
	int (*set_info)(Type_DriverIndex_Enum, Type_SetVFDInfo_Enum);	//VFD set info
} Type_VFD_Def;

// 水系统控制枚举
typedef enum {
	/* 单泵控制，用于调试 */
	WATER_LOW_PUMP = 0,
	/* 组合动作，自动化流程使用 */
	WATER_CURTAIN,
	WATER_FOAM,
	WATER_BRUSH,
	WATER_HIGHPRES,
	WATER_DRAIN,

	WATER_ALL,
	WATER_CTL_NUM,
} Type_WaterSystem_Enum;

// 水系统事件item
typedef enum {
	WATER_EVENT_LOW_PUMP = 0,
	WATER_EVENT_LOW_WATER_PRESS,
	WATER_EVENT_NUM,
} Type_WaterEventItemType_Enum;

typedef struct {
	bool isInit;
	int (*init)();                      //water module init
	int (*set_cmd)(Type_WaterSystem_Enum, bool);
	bool (*get_sta)(Type_WaterSystem_Enum);
} Type_WaterSystem_Def;

// 驱动定义
typedef struct {
	bool					isInit;
	Type_Voice_Def      	voice;		//语音系模块
	Type_WaterSystem_Def	water;		//水系统模块
	Type_Display_Def    	screen;		//显示模块
	Type_VFD_Def       		vfd;     	//变频器模块
} Type_Driver_Def;
/***********************************************************************************/

/***********************************************************************************/
//信号枚举
typedef enum{
	// 对射光电类
    SIGNAL_FRONT_WHEEL = 0,
    SIGNAL_REAR_WHEEL_LOCK1,
    SIGNAL_REAR_WHEEL_LOCK2,
	// 接近开关类
	SIGNAL_OVERHEIGHT_DETECT,
    SIGNAL_REAR_LOCK_HOME,
	SIGNAL_BUMPER_LEFT,
	SIGNAL_BUMPER_RIGHT,
	SIGNAL_GANTRY_FWD_LIMIT,
    SIGNAL_GANTRY_REV_LIMIT,
	SIGNAL_LIFT_UP_LIMIT,
	SIGNAL_LIFT_DOWN_LIMIT,
    SIGNAL_TOP_BRUSH_COLLISION,
    SIGNAL_BUMPER_ROD_LEFT,
    SIGNAL_BUMPER_ROD_RIGHT,
    SIGNAL_HEIGHT_CONTROL,
	// 按键类
	// 其他
    SIGNAL_LIFT_ALARM_FEEDBACK,
	SIGNAL_SIDE_BRUSH_OVERLOAD,
	SIGNAL_TOP_BRUSH_OVERLOAD,
	SIGNAL_WATER_PUMP_OVERLOAD,
	SIGNAL_GANTRY_ALARM,
	SIGNAL_FAN_ALARM,
	SIGNAL_SIDE_BRUSH_ALARM,
    SIGNAL_NUM,
} Type_SignalType_Enum;

//信号信息定义
typedef struct{
	Type_SignalType_Enum	signalType;
    Type_InputIo_Enum   	matchIo;
	int                 	trigDir;
	uint8_t					trigDownCnt;
	uint8_t					trigUpCnt;
    uint32_t            	closePos;
    uint32_t            	leavePos;
} Type_SignalStaInfo_Def;
/***********************************************************************************/

/***********************************************************************************/
// 异常事件枚举
typedef enum {
	ERR_EVENT_DRIVER_FAULT = 0x01,			//驱动器异常
	ERR_EVENT_DRIVE_FAILED = 0x02,			//驱动失败
	ERR_EVENT_COMMUMICATION_FAILED = 0x04,	//通讯失败
	ERR_EVENT_ACTION_TIMEOUT_CW = 0x08,		//动作超时（正向）
	ERR_EVENT_ACTION_TIMEOUT_CCW = 0x10,	//动作超时（反向）
	ERR_EVENT_INCORRECT_POSITION = 0x20,	//位置异常
	ERR_EVENT_CURRENT_ANOMALY = 0x40,		//电流异常
	ERR_EVENT_LOW_PRESS = 0x80,				//压力不足
} Type_ErrorEvent_Enum;
/***********************************************************************************/

typedef void (*err_event_callback)(Type_OsalModule_Enum, int, Type_ErrorEvent_Enum, int);

/***********************************************************************************/
//状态获取接口
extern Type_Driver_Def* A7_osal_get(void);											//获取osal的控制对象
extern Type_SignalStaInfo_Def *get_signal_handle(Type_SignalType_Enum type);		//获取传感器信号的句柄
extern bool osal_is_signal_filter_trigger(Type_SignalType_Enum type);				//获取传感器是否处于触发状态（有滤波）
extern int osal_get_dev_pos(Type_DriverIndex_Enum id);								//获取码盘机构的位置
extern Type_MoveState_Enum osal_get_motor_move_state(Type_DriverIndex_Enum id);		//获取电机的运动状态
/***********************************************************************************/
//机构驱动接口
extern int osal_drive_hold(Type_DriverIndex_Enum id, int vel);
extern int osal_move_run(Type_DriverIndex_Enum id, int vel);
extern int osal_move_force_run(Type_DriverIndex_Enum id, int vel, uint16_t time);
extern int osal_move_pos(Type_DriverIndex_Enum id, int vel, int32_t pos);
extern int osal_move_time(Type_DriverIndex_Enum id, int vel, uint16_t time);
extern int osal_move_pause(Type_DriverIndex_Enum id);
extern int osal_move_resume(Type_DriverIndex_Enum id);
extern int osal_move_stop(Type_DriverIndex_Enum id);
extern void osal_io_state_change(Type_OutputIo_Enum index, bool sta);			//输出点位状态控制
/***********************************************************************************/
//其它接口
extern bool osal_get_io_board_online(Type_Ioboards_Enum num);
extern bool osal_is_io_trigger(Type_InputIo_Enum index);
extern bool osal_read_io_input_cache_value(uint8_t board, uint8_t pin);
extern bool osal_read_io_output_cache_value(uint8_t board, uint8_t pin);
extern int osal_clear_dev_encoder(Type_DriverIndex_Enum id);
extern void offline_payment_callback_regist(void (*callback)(uint8_t washMode));
extern void osal_set_vfd_focus_on_read_current(bool value);
extern void osal_set_VFD_load_warning_current(Type_DriverIndex_Enum id, int value);
extern int osal_get_VFD_load_current(Type_DriverIndex_Enum id);
extern void osal_error_callback_regist(err_event_callback callback);
// extern int osal_set_machine_hardware_config(Type_ConfigHardwareType_Enum type, bool value);
extern void set_side_brush_down_signal_limit(bool value);
extern char* read_file(const char *filename);
extern void write_file(const char *filename, const char *content);
extern int osal_set_dev_limit_mode(Type_DriverIndex_Enum id, Type_LimitMode_Enum mode, uint16_t minPos, uint16_t maxPos);
extern void recheck_err_event(Type_OsalModule_Enum module);
// extern bool osal_get_machine_hardware_config(Type_ConfigHardwareType_Enum type);
extern int osal_init(void);
extern int osal_debug_ctl(char* fun, char* param_1, char* param_2);
// extern void refresh_hardware_status(Type_ConfigHardwareType_Enum type, bool value);

/**
 * @brief       设置 IO 脉冲输出频率
 * @param[in]   boardId              板卡 ID
 * @param[in]   pin                  IO 引脚号
 * @param[in]   frequency            输出频率，范围 1~1000，单位 Hz，配置为 0 时失能脉冲输出
 * @param[in]   dutyCycle            占空比，范围 1~100
 * @return      int
 */
extern int io_pluse_frequency_set(int boardId, int pin, int frequency, int dutyCycle);
/**
 * @brief       非阻塞设置步进电机目标位置与脉冲频率
 * @param[in]   position             目标位置，单位为脉冲数
 * @param[in]   frequency            输出频率（Hz）；大于 0 时更新频率；小于等于 0 时保持原频率（无效时置默认）
 * @return      int
 */
extern int io_step_motor_move(int position, int frequency);
/**
 * @brief       获取步进电机当前位置估算值
 * @return      int
 */
extern int io_step_motor_get_pos(void);

#ifdef __cplusplus
}
#endif

#endif
