#include <semaphore.h>
#include <pthread.h>
#include "../tools/timeData.h"
#include "modbus/modbus-rtu.h"
#include "modbus/modbus.h"
#include "A7_osal.h"
#include "io_exp/slave.h"
#include "tools/cJSON.h"
#include <errno.h> // 包含errno的定义和宏

#define SET_TIMEOUT_MS(ts, timeout_ms) do { \
    if (get_time_stamp(&(ts)) == -1) { \
        perror("clock_gettime"); \
        exit(EXIT_FAILURE); \
    } \
    long nanoseconds = (timeout_ms) * 1000 * 1000; /* 转换为纳秒 */ \
    (ts).tv_nsec += nanoseconds; \
    if ((ts).tv_nsec >= 1000000000) { \
        (ts).tv_sec += (ts).tv_nsec / 1000000000; \
        (ts).tv_nsec %= 1000000000; \
    } \
} while(0)

#define NAME_BUFF_MAX (20)              //名称字符最大长度


/*                                                         =======================                                                         */
/* ========================================================      注册异常回调      ======================================================== */
/*                                                         =======================                                                         */

err_event_callback osal_error_callback = NULL;

void osal_error_callback_regist(err_event_callback callback)
{
    osal_error_callback = callback;
}

/**
 * @brief       osal异常事件回调
 * @param[in]	module              异常模块
 * @param[in]	item                检测项
 * @param[in]	event               事件类型
 * @param[in]	param               判断参数
 * @return      int                
 */
int osal_error_event_callback(Type_OsalModule_Enum module, int item, Type_ErrorEvent_Enum event, int param)
{
    if(NULL == osal_error_callback) return -1;
    osal_error_callback(module, item, event, param);
    return 0;
}

pthread_mutex_t MK_mutex = PTHREAD_MUTEX_INITIALIZER;       //MK*设备共用一个互斥锁，避免串数据，可能共用一个数据缓存区

/*                                                         =======================                                                         */
/* ========================================================        IO读写线程      ======================================================== */
/* ===========由于平台在子板配置超过4块时，只能使用sdo，未避免频繁读写IO造成阻塞，通过此处线程定时读写各板子IO，业务读写IO值均为临时存储值=========== */
/*                                                         =======================                                                         */

#define IO_BUFF_UPDATE_FRE_MS           (30)                        //按项目实际情况调整，该值是理想值，非真实更新时间，真实值需要加上sdo读写时间
#define IO_BOARD_CHECK_ONLINE_TIME_MS   (2000)
#define IO_BOARD_CHECK_OFFLINE_TIME_MS  (300)
#define IO_BOARD_CHECK_OFFLINE_CNT      (3)

volatile unsigned int inputIoBuffValue[BOARD_NUMS] = {0};
volatile unsigned int outputIoBuffValue[BOARD_NUMS] = {0};
bool isIoBoardOnline[BOARD_NUMS] = {0};
bool isIoBoardCheckOffline[BOARD_NUMS] = {0};

void* osal_io_data_r_w_thread(void* arg)
{
    // struct timespec timeStamp;
    uint16_t checkCnt = 0;
    uint8_t offlineCnt[BOARD_NUMS] = {0};
    uint16_t checkTime = IO_BOARD_CHECK_OFFLINE_TIME_MS;
    
    memset(isIoBoardOnline, 1, sizeof(isIoBoardOnline));
    while (1)
    {
        // get_time_stamp(&timeStamp);
        checkCnt++;
        bool isAllIoBoardOffline = true;
        bool isAllIoBoardOnline = true;
        for (uint8_t i = 1; i < BOARD_NUMS; i++)            //板Id从1开始
        {
            if(0 == (checkCnt % (checkTime / IO_BUFF_UPDATE_FRE_MS))){      //由于读写需要时间，这里的时间大概是设定时间的2倍
                if(io_online_get(i) > 0){
                    isIoBoardCheckOffline[i] = false;
                    if(offlineCnt[i] >= IO_BOARD_CHECK_OFFLINE_CNT){
                        osal_error_event_callback(OSAL_MODULE_IO_BOARD, i, ERR_EVENT_COMMUMICATION_FAILED, 0);
                        LOG_DEBUG(">>>> Board id %d online", i);
                        isIoBoardOnline[i] = true;
                    }
                    offlineCnt[i] = 0;
                }
                else{
                    isIoBoardOnline[i] = false;
                    if(offlineCnt[i] < IO_BOARD_CHECK_OFFLINE_CNT){
                        if(++offlineCnt[i] == IO_BOARD_CHECK_OFFLINE_CNT){
                            osal_error_event_callback(OSAL_MODULE_IO_BOARD, i, ERR_EVENT_COMMUMICATION_FAILED, 1);
                            LOG_DEBUG(">>>> Board id %d offline", i);
                            isIoBoardCheckOffline[i] = true;
                        }
                    }
                }
            }

            if(isIoBoardOnline[i]){
                inputIoBuffValue[i] = io_read_input_s(i);   //sdo读写一次约1~2ms时间（读取失败时返回值还是之前的值）
                io_write_all_s(i, outputIoBuffValue[i]);
                isAllIoBoardOffline = false;
            }
            if(!isIoBoardCheckOffline[i])   isAllIoBoardOffline = false;
            else if(!isIoBoardOnline[i])    isAllIoBoardOnline = false;
            usleep(MS_US(IO_BUFF_UPDATE_FRE_MS/(BOARD_NUMS - 1)));
        }
        // if(isAllIoBoardOffline){
        //     LOG_ERROR("All io board offline, will be restart soon");
        //     sleep(10);          //等待报警初始化完成
        //     for (uint8_t i = 1; i < BOARD_NUMS; i++){
        //         osal_error_event_callback(OSAL_MODULE_IO_BOARD, i, ERR_EVENT_COMMUMICATION_FAILED, 1);
        //     }
        //     sleep(20);
        //     system("killall ./A7 && sleep 10 && ./A7");
        // }
        checkTime = isAllIoBoardOnline ? IO_BOARD_CHECK_OFFLINE_TIME_MS : IO_BOARD_CHECK_ONLINE_TIME_MS;
        // LOG_DEBUG("io_data_r_w updata time %lld", get_diff_ms(timeStamp));
    }
}

bool osal_get_io_board_online(Type_Ioboards_Enum num)
{
    return isIoBoardOnline[num];
}

/*                                                         =======================                                                         */
/* ========================================================      IO触发监控接口    ======================================================== */
/*                                                         =======================                                                         */

bool isIoTestEnable[BOARD_NUMS][32] = {0};
bool ioTestValue[BOARD_NUMS][32] = {0};

/**
 * @brief       IO触发检测（1为IO的默认状态值，为0时表示触发）
 * @param[in]	index               IO索引
 * @return      bool                
 */
bool osal_is_io_trigger(Type_InputIo_Enum index)
{
    uint8_t boardId = BOARD_ID_RESOLUTION(index);
    uint8_t pinId   = PIN_ID_RESOLUTION(index);

    if(INPUT_IO_NULL == index
    || (1 == boardId && pinId >= PIN_ID_RESOLUTION(BOARD1_INPUT_PIN_NUM))){
        LOG_WARN("Index %d board %d is not registered", index, boardId);
        return false;
    }
    if(isIoTestEnable[boardId][pinId])  return ioTestValue[boardId][pinId];
    //这里只对侧刷低位做特殊处理，需要在动作过程中做限位判断，保证及时停止
    if(BOARD1_INPUT_REAR_WHEEL_LOCK1 == index){
        return (inputIoBuffValue[boardId] & ((unsigned int)1 << (pinId - 1))) ? false : true;     //IO接口引脚从1开始
    }
    else{
        return (inputIoBuffValue[boardId] & ((unsigned int)1 << (pinId - 1))) ? true : false;     //IO接口引脚从1开始
    }
}

bool osal_read_io_input_cache_value(uint8_t board, uint8_t pin)
{
    return (inputIoBuffValue[board] & ((unsigned int)1 << pin)) ? true : false;
}

/*                                                         =======================                                                         */
/* ========================================================      IO输出控制接口    ======================================================== */
/*                                                         =======================                                                         */

/**
 * @brief       设置注册IO的引脚状态
 * @param[in]	index               IO索引
 * @param[in]	sta                 
 */
void osal_io_state_change(Type_OutputIo_Enum index, bool sta)
{
    uint8_t boardId = BOARD_ID_RESOLUTION(index);
    uint8_t pinId   = PIN_ID_RESOLUTION(index);

    if(OUTPUT_IO_NULL == index
    || (1 == boardId && pinId >= PIN_ID_RESOLUTION(BOARD1_OUTPUT_PIN_NUM))){
        LOG_WARN("Index %d board %d is not registered", index, boardId);
        return;
    }
    if(sta) outputIoBuffValue[boardId] |= ((unsigned int)1 << (pinId - 1));     //IO接口引脚从1开始
    else    outputIoBuffValue[boardId] &= ~((unsigned int)1 << (pinId - 1));
}

bool osal_read_io_output_cache_value(uint8_t board, uint8_t pin)
{
    return (outputIoBuffValue[board] & ((unsigned int)1 << pin)) ? false : true;
}


/*                                                         =======================                                                         */
/* ========================================================      驱动注册信息      ======================================================== */
/*                                                         =======================                                                         */

static bool isConfigMotor[DRIVER_ALL_NUM];

/* 设备驱动类型 */
typedef enum {
	DRIVER_TYPE_KM = 0,		                    //变频器驱动
	DRIVER_TYPE_VFD,						    //线圈驱动
	DRIVER_TYPE_PULSE,				            //脉冲驱动
} Type_DriverType_Enum;

/* 设备动作类型 */
typedef enum {
	ACTION_TYPE_HOLD_ON = 0,                    //保持类型（动作过程中不检查限位，超时等信息，需要手动停止）
    ACTION_TYPE_MOVE,				            //移动类型（动作过程中检查限位，超时等信息，触发条件会主动停止）
} Type_DriverActionType_Enum;

/* 驱动通用配置信息定义 */
typedef struct {
    Type_MoveState_Enum     state;
    Type_MoveState_Enum     lastState;
    bool                    isStateChange;      //是否首次切换状态
    
    Type_DriverIndex_Enum   drvIndex;           //驱动器索引
    Type_DriverType_Enum    drvType;            //驱动类型
    char                    drvName[NAME_BUFF_MAX]; //驱动名称

    Type_OutputIo_Enum      ioIndexCW;          //正转IO
    Type_OutputIo_Enum      ioIndexCCW;         //反转IO
    Type_OutputIo_Enum      ioIndexStop;        //停止IO
    Type_OutputIo_Enum      ioIndexVel0;        //速度选择IO
    Type_OutputIo_Enum      ioIndexVel1;        //速度选择IO

    int                     tarVel;             //目标速度
    int                     lastVel;            //上次的速度
} Type_DriverComInfo_Def;

/* 移动类驱动运行信息定义 */
typedef struct {
    Type_OutputIo_Enum      ioIndexEnable;      //驱动使能IO
    int                     tarPos;
    uint16_t                moveTime;
    uint16_t                forceMoveTime;
    uint32_t                actionOverTime;     //单次动作的最长时间
    struct timespec         workTimeStamp;      //开始工作的时间戳
    struct timespec         stopTimeStamp;      //结束工作的时间戳
    int8_t                  storeActDir;        //存储的运动方向（只有正向和反向）
    bool                    isPulseCounting;    //是否进行脉冲计数
} Type_DriverRunParamInfo_Def;

/* 移动类驱动限位信息定义 */
typedef struct {
    Type_LimitMode_Enum     mode;               //限位模式
    uint16_t                minPos;             //限制移动的最小距离
    uint16_t                maxPos;             //限制移动的最长距离
    uint8_t                 touchedCnt;         //限位触发次数
    Type_InputIo_Enum       ioIndexCW;          //正向限位IO
    Type_InputIo_Enum       ioIndexCCW;         //反向限位IO
} Type_DriverLimitInfo_Def;

/* 移动类驱动脉冲触发信息定义 */
typedef struct {
    bool                    isEnable;           //是否使能引脚触发
    uint16_t                time;               //触发时间
    Type_OutputIo_Enum      ioIndex;            //触发引脚
} Type_DriverTriggerInfo_Def;

/* 驱动信息定义 */
typedef struct {
    bool                        isInit;         //是否初始化
    Type_DriverActionType_Enum  action;         //动作类型
    bool                        haveEncode;     //是否有码盘脉冲信号
    bool                        isStartDriver;  //是否开始驱动
    Type_DriverComInfo_Def      com;
    Type_DriverRunParamInfo_Def runParam;       //运行参数配置
    Type_DriverLimitInfo_Def    limitParam;     //限位参数配置
    Type_DriverTriggerInfo_Def  trigger;        //脉冲触发配置
} Type_DriverInfo_Def;

Type_DriverInfo_Def Driver_Table[DRIVER_ALL_NUM];

/**
 * @brief       初始化驱动的所有参数
 * @param[in]	driver              
 */
static void osal_driver_param_init(Type_DriverInfo_Def* const driver)
{
    driver->isInit              = false;
    driver->action              = ACTION_TYPE_HOLD_ON;
    driver->haveEncode          = false;
    driver->isStartDriver       = false;

    driver->com.state           = MOTOR_STA_IDLE;
    driver->com.lastState       = MOTOR_STA_IDLE;
    driver->com.isStateChange   = true;

    driver->com.drvIndex        = 0;
    driver->com.drvType         = 0;
    strncpy(driver->com.drvName, "unReg", NAME_BUFF_MAX);

    driver->com.ioIndexCW       = OUTPUT_IO_NULL;
    driver->com.ioIndexCCW      = OUTPUT_IO_NULL;
    driver->com.ioIndexStop     = OUTPUT_IO_NULL;
    driver->com.ioIndexVel0     = OUTPUT_IO_NULL;
    driver->com.ioIndexVel1     = OUTPUT_IO_NULL;
    driver->com.tarVel          = 0;
    driver->com.lastVel         = 0;

    driver->trigger.isEnable    = false;
    driver->trigger.time        = 0;
    driver->trigger.ioIndex     = OUTPUT_IO_NULL;

    driver->runParam.ioIndexEnable      = OUTPUT_IO_NULL;
    driver->runParam.tarPos             = 0;
    driver->runParam.moveTime           = 0;
    driver->runParam.forceMoveTime      = 0;
    driver->runParam.actionOverTime     = MOVE_FOREVER;
    get_time_stamp(&driver->runParam.workTimeStamp);
    get_time_stamp(&driver->runParam.stopTimeStamp);
    driver->runParam.storeActDir        = 0;
    driver->runParam.isPulseCounting    = false;

    driver->limitParam.mode             = MODE_SIGNAL_LIMIT;
    driver->limitParam.minPos           = 0;
    driver->limitParam.maxPos           = MOVE_FOREVER;
    driver->limitParam.touchedCnt       = 0xFF;                 //首次上电 touchedCnt 为零时，若机构在限位位置，会导致机构即使在限位处也会动一下，所以这里赋一个较大初始值
    driver->limitParam.ioIndexCW        = INPUT_IO_NULL;
    driver->limitParam.ioIndexCCW       = INPUT_IO_NULL;
}

/**
 * @brief       注册保持类型的驱动设备
 * @param[in]	id                  驱动索引
 * @param[in]	type                驱动类型
 * @param[in]	printName           驱动名称
 * @param[in]	ctlIo               输出控制IO
 * @return      bool                注册成功与否
 */
static bool osal_register_hold_on_driver(Type_DriverIndex_Enum id, Type_DriverType_Enum type, char* const printName, Type_DriverComInfo_Def* const ctlIo)
{
    Type_DriverInfo_Def *driver = &Driver_Table[id];

    if(driver){
        driver->action           = ACTION_TYPE_HOLD_ON;
        driver->haveEncode       = false;
        driver->isStartDriver    = false;

        driver->com.drvIndex     = id;
        driver->com.drvType      = type;
        strncpy(driver->com.drvName, printName, NAME_BUFF_MAX);
            
        driver->com.ioIndexCW    = ctlIo->ioIndexCW;
        driver->com.ioIndexCCW   = ctlIo->ioIndexCCW;
        driver->com.ioIndexStop  = ctlIo->ioIndexStop;
        driver->com.ioIndexVel0  = ctlIo->ioIndexVel0;
        driver->com.ioIndexVel1  = ctlIo->ioIndexVel1;

        driver->isInit = true;
        return true;
    }
    LOG_WARN("%s Illegal driver id %d", __func__, id);
    return false;
}

/**
 * @brief       注册移动类型的驱动设备
 * @param[in]	id                  驱动索引
 * @param[in]	type                驱动类型
 * @param[in]	printName           驱动名称
 * @param[in]	haveEncoder         是否需要编码器计数
 * @param[in]	driverInfo          设备注册信息
 * @return      bool                注册成功与否
 */
static bool osal_register_move_driver(Type_DriverIndex_Enum id, Type_DriverType_Enum type, char* const printName, bool haveEncoder, Type_DriverInfo_Def* driverInfo)
{
    Type_DriverInfo_Def *driver = &Driver_Table[id];

    if(driver){
        driver->action           = ACTION_TYPE_MOVE;
        driver->haveEncode       = haveEncoder;
        driver->isStartDriver    = driverInfo->isStartDriver;

        driver->com.state        = driverInfo->com.state;
        driver->com.lastState    = driverInfo->com.lastState;
        driver->com.isStateChange= driverInfo->com.isStateChange;
        
        driver->com.drvIndex     = id;
        driver->com.drvType      = type;
        strncpy(driver->com.drvName, printName, NAME_BUFF_MAX);

        driver->com.ioIndexCW    = driverInfo->com.ioIndexCW;
        driver->com.ioIndexCCW   = driverInfo->com.ioIndexCCW;
        driver->com.ioIndexStop  = driverInfo->com.ioIndexStop;
        driver->com.ioIndexVel0  = driverInfo->com.ioIndexVel0;
        driver->com.ioIndexVel1  = driverInfo->com.ioIndexVel1;
        driver->com.tarVel       = driverInfo->com.tarVel;
        driver->com.lastVel      = driverInfo->com.lastVel;

        driver->trigger.isEnable = driverInfo->trigger.isEnable;
        driver->trigger.time     = driverInfo->trigger.time;
        driver->trigger.ioIndex  = driverInfo->trigger.ioIndex;
        
        driver->runParam.ioIndexEnable   = driverInfo->runParam.ioIndexEnable;
        driver->runParam.tarPos          = driverInfo->runParam.tarPos;
        driver->runParam.moveTime        = driverInfo->runParam.moveTime;
        driver->runParam.forceMoveTime   = driverInfo->runParam.forceMoveTime;
        driver->runParam.actionOverTime  = driverInfo->runParam.actionOverTime;
        driver->runParam.storeActDir     = driverInfo->runParam.storeActDir;
        driver->runParam.isPulseCounting = driverInfo->runParam.isPulseCounting;

        driver->limitParam.mode       = driverInfo->limitParam.mode;
        driver->limitParam.minPos     = driverInfo->limitParam.minPos;
        driver->limitParam.maxPos     = driverInfo->limitParam.maxPos;
        driver->limitParam.touchedCnt = driverInfo->limitParam.touchedCnt;
        driver->limitParam.ioIndexCW  = driverInfo->limitParam.ioIndexCW;
        driver->limitParam.ioIndexCCW = driverInfo->limitParam.ioIndexCCW;

        driver->isInit = true;
        return true;
    }
    LOG_WARN("%s Illegal id %d", __func__, id);
    return false;
}

/**
 * @brief       驱动注册初始化（注册所有需要用到的驱动）
 */
static void osal_driver_register_init(void)
{
    Type_DriverInfo_Def driverInfo;
    /* 注册保持类驱动 */
    //顶刷旋转
    osal_driver_param_init(&driverInfo);
    driverInfo.com.ioIndexCW = BOARD1_OUTPUT_BRUSH_FWD;
    driverInfo.com.ioIndexCCW = BOARD1_OUTPUT_BRUSH_REV;
    osal_register_hold_on_driver(VFD_TOP_BRUSH, DRIVER_TYPE_VFD, "BRUSH_TOP", &driverInfo.com);
    //侧刷旋转
    osal_driver_param_init(&driverInfo);
    driverInfo.com.ioIndexCW = BOARD1_OUTPUT_BRUSH_FWD;
    driverInfo.com.ioIndexCCW = BOARD1_OUTPUT_BRUSH_REV;
    osal_register_hold_on_driver(VFD_SIDE_BRUSH, DRIVER_TYPE_VFD, "BRUSH_SIDE", &driverInfo.com);
    //风机
    osal_driver_param_init(&driverInfo);
    driverInfo.com.ioIndexCW = BOARD1_OUTPUT_FAN_START;
    osal_register_hold_on_driver(VFD_DRYER, DRIVER_TYPE_VFD, "DRYER", &driverInfo.com);
    
    /* 注册移动类驱动（非脉冲控制） */
    //龙门
    osal_driver_param_init(&driverInfo);
    driverInfo.com.ioIndexCW   = BOARD1_OUTPUT_GANTRY_FWD;
    driverInfo.com.ioIndexCCW  = BOARD1_OUTPUT_GANTRY_REV;
    driverInfo.com.ioIndexVel0 = BOARD1_OUTPUT_GANTRY_HIGH_SPEED;
    driverInfo.com.ioIndexVel1 = OUTPUT_IO_NULL;
    driverInfo.runParam.actionOverTime = 180000;
    driverInfo.limitParam.mode       = MODE_SIGNAL_LIMIT;
    driverInfo.limitParam.maxPos     = 700;
    driverInfo.limitParam.ioIndexCW  = BOARD1_INPUT_GANTRY_FWD_LIMIT;
    driverInfo.limitParam.ioIndexCCW = BOARD1_INPUT_GANTRY_REV_LIMIT;
    osal_register_move_driver(VFD_GANTRY, DRIVER_TYPE_VFD, "GANTRY", true, &driverInfo);
    //电动推杆
    osal_driver_param_init(&driverInfo);
    driverInfo.com.ioIndexCW   = BOARD1_OUTPUT_ROD_EXTEND;
    driverInfo.com.ioIndexCCW  = BOARD1_OUTPUT_ROD_RETRACT;
    driverInfo.com.ioIndexVel0 = OUTPUT_IO_NULL;
    driverInfo.com.ioIndexVel1 = OUTPUT_IO_NULL;
    driverInfo.runParam.actionOverTime = 30000;
    driverInfo.limitParam.mode       = MODE_SIGNAL_LIMIT;
    driverInfo.limitParam.maxPos     = MOVE_FOREVER;
    driverInfo.limitParam.ioIndexCW  = BOARD1_INPUT_REAR_WHEEL_LOCK2;
    driverInfo.limitParam.ioIndexCCW = BOARD1_INPUT_REAR_LOCK_HOME;
    osal_register_move_driver(KM_PUTTER, DRIVER_TYPE_KM, "PUTTER", true, &driverInfo);
    /* 注册移动类驱动（脉冲控制） */
    //1#道闸
    // osal_driver_param_init(&driverInfo);
    // driverInfo.com.ioIndexCW    = BOARD1_OUTPUT_GATE_1_CLOSE;
    // driverInfo.com.ioIndexCCW   = BOARD1_OUTPUT_GATE_1_OPEN;
    // driverInfo.com.ioIndexStop  = BOARD1_OUTPUT_GATE_1_STOP;
    // driverInfo.trigger.time     = 1000,
    // driverInfo.runParam.actionOverTime = 10000;
    // driverInfo.limitParam.ioIndexCW  = BOARD5_INPUT_GEATE_1_STOP_SIGNAL;
    // driverInfo.limitParam.ioIndexCCW = INPUT_IO_NULL;
    // osal_register_move_driver(KM_GATE_1, DRIVER_TYPE_PULSE, "GATE_1#", false, &driverInfo);
}

/*                                                         =======================                                                         */
/* ========================================================       码盘注册信息     ======================================================== */
/*                                                         =======================                                                         */

#define PULSE_IO_INPUT_FILTER_TIME      (60)
#define NULL_ENCODER                    (0xFFFFFFF)    //未获取码盘脉冲值时的返回值

/**
 * @brief       配置子板输入IO的滤波时间
 * @param[in]	boardId             子板ID
 * @param[in]	pin                 子板IO引脚
 * @param[in]	time                滤波窗口时间
 * @return      int                 
 */
static int osal_config_input_filter_time(int boardId, int pin, int time)
{
    return sdo_write(boardId, 0x2006, pin, 2, &time, 0x06);
}

/* 码盘状态 */
typedef struct
{
    bool isErr;                                 //码盘计数值是否异常状态
    bool isErrRecord;
    int errCnt;
    struct timespec checkTimeStamp;
    int touchZeroCnt;                           //零点触发次数
    int recoderEncValue;                        //记录的码盘脉冲值
} Type_EncoderState_Def;

/* 码盘配置信息 */
typedef struct
{
    bool                    isInit;             //是否初始化
    int                     encoderValue;       //码盘计数基于识别零点的相对值（未校零时可能为负数）
    int                     lastEncoderValue;   //上次的码盘计数值

    uint32_t                readPulseValue;     //读取的脉冲值
    uint32_t                flashPulseValue;    //刷新的脉冲值

    Type_InputIo_Enum       ioIndexPulse;       //脉冲采集IO
    Type_OutputIo_Enum      ioIndexCW;          //控制正向移动IO
    Type_OutputIo_Enum      ioIndexCCW;         //控制反向移动IO
    Type_InputIo_Enum       ioIndexZero;        //初始零位IO
    bool                    isManualClearEncoder;  //是否手动清除脉冲值

    Type_EncoderState_Def   status;             //码盘状态
} Type_EncoderInfo_Def;

Type_EncoderInfo_Def Encoder_Table[DRIVER_ALL_NUM];

/**
 * @brief       初始化码盘的所有参数
 * @param[in]	driver              
 */
static void osal_encoder_param_init(Type_EncoderInfo_Def* const encoder)
{
    encoder->isInit             = false;
    encoder->encoderValue       = 0;
    encoder->lastEncoderValue   = 0;

    encoder->readPulseValue     = 0;
    encoder->flashPulseValue    = 0;

    encoder->ioIndexPulse       = INPUT_IO_NULL;
    encoder->ioIndexCW          = INPUT_IO_NULL;
    encoder->ioIndexCCW         = INPUT_IO_NULL;
    encoder->ioIndexZero        = INPUT_IO_NULL;
    encoder->isManualClearEncoder = false;

    encoder->status.isErr       = false;
    encoder->status.errCnt      = 0;
    get_time_stamp(&encoder->status.checkTimeStamp);
    encoder->status.touchZeroCnt = 0;
    encoder->status.recoderEncValue = 0;
}

/**
 * @brief       注册码盘
 * @param[in]	id                  驱动索引
 * @param[in]	pulseIo             码盘的信号IO
 * @return      bool                注册成功与否
 */
static bool osal_register_encoder(Type_DriverIndex_Enum id, Type_InputIo_Enum pulseIo)
{
    Type_EncoderInfo_Def *encoder = &Encoder_Table[id];

    if(encoder){
        if(!Driver_Table[id].isInit || !Driver_Table[id].haveEncode){
            LOG_WARN("Enc register failed, driver %d is not init or not euquip encoder", id);
            return false;
        }

        osal_encoder_param_init(encoder);
        encoder->ioIndexPulse = pulseIo;
        //正向、反向、零位输入IO从驱动表中获取
        encoder->ioIndexCW   = Driver_Table[id].com.ioIndexCW;
        encoder->ioIndexCCW  = Driver_Table[id].com.ioIndexCCW;
        encoder->ioIndexZero = Driver_Table[id].limitParam.ioIndexCCW;
        //设置滤波脉冲的滤波时间
        osal_config_input_filter_time(BOARD_ID_RESOLUTION(pulseIo), PIN_ID_RESOLUTION(pulseIo), PULSE_IO_INPUT_FILTER_TIME);

        encoder->isInit = true;
        return true;
    }
    LOG_WARN("%s Illegal id %d", __func__, id);
    return false;
}

/**
 * @brief       码盘注册初始化（注册所有需要用到的码盘）
 */
static void osal_encoder_register_init(void)
{   
    osal_register_encoder(VFD_GANTRY,       BOARD1_INPUT_GANTRY_ENCODER_PULSE);
}

/*                                                         =======================                                                         */
/* ========================================================      语音相关接口      ======================================================== */
/*                                                         =======================                                                         */

#define ADDR_TEST               0x0000              //测试
#define ADDR_PLAY_MODE          0x0001              //工作模式
#define ADDR_PLAY_VOLUME        0x0002              //模块音量
#define ADDR_EQ_MODE            0x0003              //EQ模式
#define ADDR_VOICE_PLAY         0x0004              //语音播放
#define ADDR_VOICE_UP           0x0005              //音量增加
#define ADDR_VOICE_DOWN         0x0006              //音量减小
#define ADDR_PAUSE_PLAY         0x0009              //暂停
#define ADDR_CLEAR_PLAY         0x000A              //停止播放，情况列表
#define DL485_NUM               (2)                 //DL485模块个数

static bool isVoiceEnable = true;

typedef struct
{
    bool        isInit;
	uint8_t 	slave_id;           //modbus slave id
	uint8_t 	port;               //modbus uart port
    uint32_t 	speed;              //modbus uart speed
    uint32_t 	timeOut;            //modbus uart time out
}Type_DL485Info_Def;

Type_DL485Info_Def DL485_Table[DL485_NUM];
static modbus_t *mbVoice[DL485_NUM] = {0};
static uint8_t voiceModuleRegNum = 0;

/**
 * @brief       初始化DL485语音模块的所有参数
 * @param[in]	dl485              
 */
static void osal_dl485_param_init(Type_DL485Info_Def* const dl485)
{
    dl485->isInit   = false;
    dl485->slave_id = 0;
    dl485->port     = 1;
    dl485->speed    = 9600;
    dl485->timeOut  = 100;
}

/**
 * @brief       注册语音模块
 * @param[in]	id                  站号
 * @param[in]	port                端口号
 * @param[in]	speed               波特率
 * @param[in]	timeOut             通讯超时时间（ms）
 * @return      bool                注册成功与否
 */
static bool osal_register_voice_module(uint8_t id, uint8_t port, uint32_t speed, uint32_t timeOut)
{
    Type_DL485Info_Def *voice = &DL485_Table[id];

    if(voice){
        osal_dl485_param_init(voice);
        voice->slave_id = id;
        voice->port     = port;
        voice->speed    = speed;
        voice->timeOut  = timeOut;

        char *voiceDriver = (char*)malloc(32 * sizeof(char));
        if(2 == voice->port)        voiceDriver = "/dev/ttysWK0";
        else if(3 == voice->port)   voiceDriver = "/dev/ttysWK2";
        else                        voiceDriver = "/dev/ttysWK1";
        mbVoice[voiceModuleRegNum] = modbus_new_rtu(voiceDriver, voice->speed, 'N', 8, 1);
        if(NULL == mbVoice[voiceModuleRegNum]){
            LOG_ERROR("mbVoice %d init failed", voiceModuleRegNum);
            return false;
        }
        modbus_set_slave(mbVoice[voiceModuleRegNum], voice->slave_id);
        modbus_rtu_set_serial_mode(mbVoice[voiceModuleRegNum], MODBUS_RTU_RS485);
        modbus_set_response_timeout(mbVoice[voiceModuleRegNum], voice->timeOut/1000, voice->timeOut%1000*1000);

        // 设置错误恢复模式为链路层恢复和协议层恢复
        if (-1 == modbus_set_error_recovery(mbVoice[voiceModuleRegNum], MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL)) {
            fprintf(stderr, "Failed to set error recovery mode\n");
            return false;
        }

        if(-1 == modbus_connect(mbVoice[voiceModuleRegNum])){
            LOG_ERROR("mbVoice %d connect failed !", voiceModuleRegNum);
            return false;
        }
        // modbus_set_debug(mbVoice[voiceModuleRegNum], 1);

        voice->isInit = true;
        voiceModuleRegNum++;
        return true;
    }
    LOG_WARN("%s Illegal id %d", __func__, id);
    return false;
}

/**
 * @brief       
 * @return      bool                
 */
static int osal_voice_init(void)
{
    int ret = 0;

    ret |= osal_register_voice_module(1, 2, 9600, 100) ? 0 : -1;
    ret |= osal_register_voice_module(2, 2, 9600, 100) ? 0 : -1;

    return ret;
}

/**
 * @brief       语音播报
 * @param[in]	pos                语音位置
 * @param[in]	item               播报的节目
 * @return      int                 
 */
static int osal_voice_play(Type_VoicePos_Enum pos, Type_A7Voice_Enum item)
{
    if(!isVoiceEnable){
        LOG_DEBUG("Voice config disable, exit");
        return 0;
    }

    int ret = -1;
    uint16_t writeData = item;
    uint8_t retryCnt = 1;

    struct timespec MK_ts;
    SET_TIMEOUT_MS(MK_ts, 200);
    if(0 == pthread_mutex_timedlock(&MK_mutex, &MK_ts)){
        modbus_t *voiceObj = (A7_VOICE_POS_ENTRY == pos) ? mbVoice[0] : mbVoice[1];
        while (ret != 0 && retryCnt--)
        {
            modbus_flush(voiceObj);
            ret = (1 == modbus_write_registers(voiceObj, ADDR_VOICE_PLAY, 1, (uint16_t*)&writeData)) ? 0 : -1;
            if(ret != 0){
                LOG_WARN("Failed to write: %s", modbus_strerror(errno));
            }
            else break;
            usleep(MS_US(50));
        }
        osal_error_event_callback(OSAL_MODULE_VOICE, 0, ERR_EVENT_COMMUMICATION_FAILED, ret);
        pthread_mutex_unlock(&MK_mutex);
    }
    return ret;
}


/*                                                         =======================                                                         */
/* ========================================================      显示相关接口      ======================================================== */
/*                                                         =======================                                                         */

static bool isDisplayEnable = true;
static Type_A7Display_Enum displayCode;
static uint8_t dispTestCnt = 0;
static struct timespec dispStartTimeStamp;

void* osal_display_thread(void* arg)
{
    while (1)
    {
        if(A7_DISP_TEST == displayCode){
            if(dispTestCnt < 4 && get_diff_ms(dispStartTimeStamp) > 1500){
                if(0 == dispTestCnt)        osal_io_state_change(BOARD1_OUTPUT_ENTRY_GREEN1,   IO_ENABLE);
                else if(1 == dispTestCnt)   osal_io_state_change(BOARD1_OUTPUT_ENTRY_RED,       IO_ENABLE);
                else if(2 == dispTestCnt)   osal_io_state_change(BOARD1_OUTPUT_ENTRY_GREEN2,   IO_ENABLE);
                else if(3 == dispTestCnt)   osal_io_state_change(BOARD1_OUTPUT_ENTRY_YELLOW,    IO_ENABLE);
                dispTestCnt++;
                get_time_stamp(&dispStartTimeStamp);
            }
        }
        else{
            Type_OutputIo_Enum dispIo = OUTPUT_IO_NULL;
            if(A7_CAR_FORWARD == displayCode
            || A7_CAR_LEFT_SKEW == displayCode
            || A7_CAR_LEFT_SKEW == displayCode)             dispIo = BOARD1_OUTPUT_ENTRY_GREEN1;
            else if(A7_CAR_BACKOFF == displayCode)          dispIo = BOARD1_OUTPUT_ENTRY_GREEN2;
            else if(A7_CAR_READY_TRANSFER == displayCode
                || A7_CAR_WASH_STARTING == displayCode
                || (A7_CAR_STOP == displayCode && get_diff_ms(dispStartTimeStamp) > 1000))  dispIo = BOARD1_OUTPUT_ENTRY_YELLOW;

            if(dispIo != OUTPUT_IO_NULL){
                osal_io_state_change(dispIo, IO_ENABLE);
                usleep(MS_US(500));
                osal_io_state_change(dispIo, IO_DISABLE);
                usleep(MS_US(500));
                continue;
            }
        }
        sleep(1);
    }
}

/**
 * @brief       display_init
 * @return      int
 */
static int osal_display_init(void)
{
    pthread_t osalDisplay_thread;
    pthread_create(&osalDisplay_thread, NULL, osal_display_thread, NULL);
    return 0;
}

/**
 * @brief       display_set
 * @param[in]	code
 * @return      int
 */
static int osal_display_set(Type_A7Display_Enum code)
{
    if(!isDisplayEnable){
        LOG_DEBUG("Display config disable, exit");
        return 0;
    }

    static Type_A7Display_Enum lastCode = 0;

    displayCode = code;
    get_time_stamp(&dispStartTimeStamp);
    osal_io_state_change(BOARD1_OUTPUT_ENTRY_GREEN1,   IO_DISABLE);
    osal_io_state_change(BOARD1_OUTPUT_ENTRY_RED,       IO_DISABLE);
    osal_io_state_change(BOARD1_OUTPUT_ENTRY_GREEN2,   IO_DISABLE);
    osal_io_state_change(BOARD1_OUTPUT_ENTRY_YELLOW,    IO_DISABLE);
    if(A7_CAR_FORWARD == displayCode || A7_CAR_BACKOFF == displayCode
         || A7_CAR_LEFT_SKEW == displayCode || A7_CAR_LEFT_SKEW == displayCode
         || A7_CAR_READY_TRANSFER == displayCode || A7_CAR_WASH_STARTING == displayCode)  {}  //闪烁灯由线程控制
    else if(A7_DISP_TEST == displayCode)    dispTestCnt = 0;
    else if(A7_CAR_WAIT == displayCode)     osal_io_state_change(BOARD1_OUTPUT_ENTRY_GREEN1, IO_ENABLE);
    else                                    osal_io_state_change(BOARD1_OUTPUT_ENTRY_RED, IO_ENABLE);

    if(lastCode != code){
        LOG_INFO("Display change to %d", code);
    }
    lastCode = code;
    return 0;
}

/*                                                         =======================                                                         */
/* ========================================================     变频器信息配置     ======================================================== */
/*                                                         =======================                                                         */

/* //伟创变频器
#define MB_ADDR_VFD_STATE               (0x1001)        //变频器状态
#define MB_ADDR_VFD_CURRENT             (0x1004)        //变频器电流
#define MB_ADDR_VFD_ERR_CODE            (0x1007)        //变频器错误码
#define MB_ADDR_VFD_CLEAR_ERR           (0x1101)        //变频器清报警

#define MB_DATA_VFD_RESET               (0x9696)        //变频器重置
#define MB_DATA_VFD_CLEAR_ERR           (0xA5A5)        //清除故障信息 */

/* //台达变频器
#define MB_ADDR_VFD_ERR_CODE            (0x2100)        //变频器故障状态
#define MB_ADDR_VFD_FREQUENCE           (0x2103)        //变频器输出频率
#define MB_ADDR_VFD_CURRENT             (0x2104)        //变频器输出电流
#define MB_ADDR_VFD_VOLTAGE             (0x2106)        //变频器输出电压
#define MB_ADDR_VFD_STATE               (0x2226)        //变频器状态
#define MB_ADDR_VFD_CTL                 (0x2002)        //故障/控制命令来源
#define MB_DATA_VFD_CLEAR_ERR           (0x0002)        //清除错误状态 */

//士林变频器
#define MB_ADDR_VFD_CTL       	        (0x1000)        //操作模式
#define MB_ADDR_VFD_STATE               (0x1001)        //变频器状态
#define MB_ADDR_VFD_CURRENT             (0x1004)        //变频器输出电流
#define MB_ADDR_VFD_ERR_CODE            (0x1007)        //变频器错误码
#define MB_DATA_VFD_CLEAR_ERR           (0x1101)        //清除错误

typedef struct{
	uint8_t 	slave_id;           //modbus slave id
	uint8_t 	port;               //modbus uart port
    uint32_t 	speed;              //modbus uart speed
    uint32_t 	timeOut;            //modbus uart time out
} Type_ModbusCom_Def;

typedef struct
{
    int                 loadCurrent;
    int                 vfdState;
    uint8_t             commFailedCnt;
} Type_CommData_Def;

typedef struct
{
    bool                isInit;
	Type_ModbusCom_Def 	com;
	
    Type_OutputIo_Enum  gpio_STF;
    Type_OutputIo_Enum  gpio_STR;
    Type_OutputIo_Enum  gpio_M0;
    Type_OutputIo_Enum  gpio_M1;
    Type_OutputIo_Enum  gpio_RESET;
    uint8_t             mbMatchId;
    Type_CommData_Def   commData;
    int                 warningCurrent;
    int                 VfdLoadCurrentSta;
    struct timespec		currentTooLowTimeStamp;     //触压电流过小时间戳
    struct timespec		currentTooHighTimeStamp;    //触压电流过大时间戳
} Type_VFDInfo_Def;

Type_VFDInfo_Def VFD_Table[DRIVER_VFD_NUM];
static modbus_t *mbVFD[DRIVER_VFD_NUM] = {0};
static bool isFoucsOnReadCurrentData = false;

/**
 * @brief       初始化VFD的所有参数
 * @param[in]	dl485              
 */
static void osal_VFD_param_init(Type_VFDInfo_Def* const vfd)
{
    vfd->isInit         = false;
    vfd->com.slave_id   = 0;
    vfd->com.port       = 1;
    vfd->com.speed      = 9600;
    vfd->com.timeOut    = 10;
    vfd->gpio_STF       = OUTPUT_IO_NULL;
    vfd->gpio_STR       = OUTPUT_IO_NULL;
    vfd->gpio_M0        = OUTPUT_IO_NULL;
    vfd->gpio_M1        = OUTPUT_IO_NULL;
    vfd->gpio_RESET     = OUTPUT_IO_NULL;
    vfd->mbMatchId      = 0;
    vfd->commData.loadCurrent = 0;
    vfd->commData.commFailedCnt = 0;
    vfd->warningCurrent = 200;
    vfd->VfdLoadCurrentSta = 0;
}

/**
 * @brief       注册VFD
 * @param[in]	drvId               驱动器索引
 * @param[in]	mbId                通讯站号
 * @param[in]	mbPort              通讯端口号
 * @param[in]	mbSpeed             通讯波特率
 * @param[in]	timeOut             通讯超时时间（ms）
 * @param[in]	resetIo             复位IO
 * @return      bool                注册成功与否
 */
static bool osal_register_vfd_module(Type_DriverIndex_Enum drvId, uint8_t mbId, uint8_t mbPort, uint32_t mbSpeed, uint32_t timeOut, Type_OutputIo_Enum resetIo)
{
    static uint8_t vfdModuleRegNum = 0;
    Type_VFDInfo_Def *vfd = &VFD_Table[drvId];

    if(vfd){
        if(!Driver_Table[drvId].isInit){
            LOG_WARN("VFD id %d driver not init, register failed, ", drvId);
            return false;
        }
        osal_VFD_param_init(vfd);
        vfd->com.slave_id   = mbId;
        vfd->com.port       = mbPort;
        vfd->com.speed      = mbSpeed;
        vfd->com.timeOut    = timeOut;
        vfd->gpio_RESET     = resetIo;

        //以下IO信息从驱动表中获取
        vfd->gpio_STF       = Driver_Table[drvId].com.ioIndexCCW;
        vfd->gpio_STR       = Driver_Table[drvId].com.ioIndexCW;
        vfd->gpio_M0        = Driver_Table[drvId].com.ioIndexVel0;
        vfd->gpio_M1        = Driver_Table[drvId].com.ioIndexVel1;

        vfd->mbMatchId = vfdModuleRegNum;
        char *vfdDriver = (char*)malloc(32 * sizeof(char));
        if(vfdDriver){
            if(2 == vfd->com.port)      strncpy(vfdDriver, "/dev/ttysWK0", 32);
            else if(3 == vfd->com.port) strncpy(vfdDriver, "/dev/ttysWK2", 32);
            else                        strncpy(vfdDriver, "/dev/ttysWK1", 32);
            mbVFD[vfdModuleRegNum] = modbus_new_rtu(vfdDriver, vfd->com.speed, 'N', 8, 1);
            if(NULL == mbVFD[vfdModuleRegNum]){
                LOG_ERROR("mbVFD %d init failed", vfdModuleRegNum);
                return false;
            }
            modbus_set_slave(mbVFD[vfdModuleRegNum], vfd->com.slave_id);
            modbus_rtu_set_serial_mode(mbVFD[vfdModuleRegNum], MODBUS_RTU_RS485);
            modbus_set_response_timeout(mbVFD[vfdModuleRegNum],  vfd->com.timeOut/1000, vfd->com.timeOut%1000*1000);

            // 设置错误恢复模式为链路层恢复和协议层恢复
            if (-1 == modbus_set_error_recovery(mbVFD[vfdModuleRegNum], MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL)) {
                fprintf(stderr, "Failed to set error recovery mode\n");
                return false;
            }

            if(-1 == modbus_connect(mbVFD[vfdModuleRegNum])){
                LOG_ERROR("mbVFD %d connect failed !", vfdModuleRegNum);
                return false;
            }
            // modbus_set_debug(mbVFD[vfdModuleRegNum], 1);
            free(vfdDriver);
        }

        vfd->isInit = true;
        vfdModuleRegNum++;
        return true;
    }
    LOG_WARN("%s Illegal id %d", __func__, drvId);
    return false;
}

/**
 * @brief       变频器的驱动控制
 * @param[in]	index               变频器的列表索引
 * @param[in]	vel                 变频器设定速度
 * @return      int                 
 */
static int osal_VFD_run(Type_DriverIndex_Enum index, int vel)
{
    if(VFD_Table[index].gpio_STR != OUTPUT_IO_NULL) osal_io_state_change(VFD_Table[index].gpio_STR, (vel > 0) ? IO_ENABLE : IO_DISABLE);
    if(VFD_Table[index].gpio_STF != OUTPUT_IO_NULL) osal_io_state_change(VFD_Table[index].gpio_STF, (vel < 0) ? IO_ENABLE : IO_DISABLE);
    if(VFD_Table[index].gpio_M0 != OUTPUT_IO_NULL)  osal_io_state_change(VFD_Table[index].gpio_M0, (2 == vel || 4 == vel) ? IO_ENABLE : IO_DISABLE);
    if(VFD_Table[index].gpio_M1 != OUTPUT_IO_NULL)  osal_io_state_change(VFD_Table[index].gpio_M1, (3 == vel || 4 == vel) ? IO_ENABLE : IO_DISABLE);
    return 0;
}

/**
 * @brief       获取变频器信息
 * @param[in]	index               变频器的列表索引
 * @param[in]	info                需要获取的信息类型
 * @return      int                 
 */
static int osal_get_VFD_info(Type_DriverIndex_Enum index, Type_GetVFDInfo_Enum info)
{
    if(0 == VFD_Table[index].com.slave_id) return -2;

    uint16_t infoData = 0;
    uint16_t regAddr = 0;
    static uint8_t errCnt[DRIVER_VFD_NUM] = {0};
    switch (info)
    {
    case GET_VFD_STATE:     regAddr = MB_ADDR_VFD_STATE;    break;
    case GET_VFD_CURRENT:   regAddr = MB_ADDR_VFD_CURRENT;  break;
    case GET_VFD_ERR_CODE:  regAddr = MB_ADDR_VFD_ERR_CODE; break;
    default:
        return -1;
        break;
    }
    struct timespec MK_ts;
    SET_TIMEOUT_MS(MK_ts, 500);
    if(0 == pthread_mutex_timedlock(&MK_mutex, &MK_ts)){
        if(VFD_Table[index].isInit){
            modbus_flush(mbVFD[VFD_Table[index].mbMatchId]);
            // struct timespec timeStamp;
            // get_time_stamp(&timeStamp);
            if(modbus_read_registers(mbVFD[VFD_Table[index].mbMatchId], regAddr, 1, (uint16_t*)&infoData) != 1){
                infoData = 0xFFFF;
                // LOG_WARN("%s slave id %d read failed: %s", Driver_Table[index].com.drvName, VFD_Table[index].com.slave_id, modbus_strerror(errno));
                // errCnt[VFD_Table[index].mbMatchId]++;
                // if(errCnt[VFD_Table[index].mbMatchId] >= 100){
                //     errCnt[VFD_Table[index].mbMatchId] = 0;
                //     //删除上下文
                //     modbus_close(mbVFD[VFD_Table[index].mbMatchId]);
                //     modbus_free(mbVFD[VFD_Table[index].mbMatchId]);
                //     //创建上下文
                //     mbVFD[VFD_Table[index].mbMatchId] = modbus_new_rtu("/dev/ttysWK1", VFD_Table[index].com.speed, 'N', 8, 1);
                //     if(NULL == mbVFD[VFD_Table[index].mbMatchId]){
                //         LOG_ERROR("mbVFD %d init failed", VFD_Table[index].mbMatchId);
                //         // return false;
                //     }
                //     //恢复上下文
                //     modbus_set_slave(mbVFD[VFD_Table[index].mbMatchId], VFD_Table[index].com.slave_id);
                //     modbus_rtu_set_serial_mode(mbVFD[VFD_Table[index].mbMatchId], MODBUS_RTU_RS485);
                //     modbus_set_response_timeout(mbVFD[VFD_Table[index].mbMatchId], VFD_Table[index].com.timeOut/1000, VFD_Table[index].com.timeOut%1000*1000);

                //     // 设置错误恢复模式为链路层恢复和协议层恢复
                //     if (-1 == modbus_set_error_recovery(mbVFD[VFD_Table[index].mbMatchId], MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL)) {
                //         fprintf(stderr, "Failed to set error recovery mode\n");
                //         // return false;
                //     }

                //     if(-1 == modbus_connect(mbVFD[VFD_Table[index].mbMatchId])){
                //         LOG_ERROR("mbVFD %d connect failed !", VFD_Table[index].mbMatchId);
                //         // return false;
                //     }
                //     LOG_INFO("%d reInit done", VFD_Table[index].mbMatchId);
                // }
            }
            else{
                errCnt[VFD_Table[index].mbMatchId] = 0;
            }
            // LOG_ERROR(">>>>>> %d, %lld",index, get_diff_ms(timeStamp));
        }
        else{
            LOG_WARN("VFD id %d is not init", index);
            infoData = -2;
        }
        pthread_mutex_unlock(&MK_mutex);
    }
    else{
        return -1;
    }
    return infoData;
}

/**
 * @brief       设置变频器信息
 * @param[in]	index               变频器的列表索引
 * @param[in]	info                需要设置的信息类型
 * @return      int                 
 */
static int osal_set_VFD_info(Type_DriverIndex_Enum index, Type_SetVFDInfo_Enum info)
{
    if(0 == VFD_Table[index].com.slave_id) return -2;

    int ret = -1;
    uint16_t regAddr = 0;
    uint16_t writeData = 0;
    switch (info)
    {
    case SET_CLEAR_ERR:
        regAddr = MB_ADDR_VFD_CTL;
        writeData = MB_DATA_VFD_CLEAR_ERR;
        break;
    default:
        return -2;
        break;
    }
    struct timespec MK_ts;
    SET_TIMEOUT_MS(MK_ts, 500);
    if(0 == pthread_mutex_timedlock(&MK_mutex, &MK_ts)){
        if(VFD_Table[index].isInit){
            ret = (1 == modbus_write_registers(mbVFD[VFD_Table[index].mbMatchId], regAddr, 1, (uint16_t*)&writeData)) ? 0 : -1;
        }
        else{
            LOG_WARN("VFD id %d is not init", index);
        }
        pthread_mutex_unlock(&MK_mutex);
    }
    return ret;
}

/**
 * @brief       检查变频器的通讯状态
 * @param[in]	id                  变频器的列表索引
 * @param[in]	info                
 * @return      int                 
 */
static int VFD_commumicate_state_check(Type_DriverIndex_Enum id, int info)
{
    int ret = -1;
    if(0xFFFF == info){
        if(VFD_Table[id].commData.commFailedCnt < 3){
            if(3 == ++VFD_Table[id].commData.commFailedCnt) osal_error_event_callback(OSAL_MODULE_MOTOR, id, ERR_EVENT_COMMUMICATION_FAILED, 1);
        }
        ret = -1;
    }
    else{
        if(VFD_Table[id].commData.commFailedCnt >= 3) osal_error_event_callback(OSAL_MODULE_MOTOR, id, ERR_EVENT_COMMUMICATION_FAILED, 0);
        VFD_Table[id].commData.commFailedCnt = 0;
        ret = 0;
    }
    return ret;
}

/**
 * @brief       检查变频器的负载电流
 * @param[in]	id                  变频器的列表索引
 * @return      void                 
 */
static void VFD_load_current_check(Type_DriverIndex_Enum id)
{
    if(0 == VFD_commumicate_state_check(id, VFD_Table[id].commData.loadCurrent)){
        bool isLimitTrigger = false;
        // if(VFD_TOP_BRUSH == id)                 isLimitTrigger = osal_is_signal_filter_trigger(SIGNAL_LIFTER_UP);
        // else if(VFD_FRONT_LEFT_BRUSH == id)     isLimitTrigger = osal_is_signal_filter_trigger(SIGNAL_FL_MOVE_ZERO);
        // else if(VFD_FRONT_RIGHT_BRUSH == id)    isLimitTrigger = osal_is_signal_filter_trigger(SIGNAL_FR_MOVE_ZERO);
        // else if(VFD_BACK_LEFT_BRUSH == id)      isLimitTrigger = osal_is_signal_filter_trigger(SIGNAL_BL_MOVE_ZERO);
        // else if(VFD_BACK_RIGHT_BRUSH == id)     isLimitTrigger = osal_is_signal_filter_trigger(SIGNAL_BR_MOVE_ZERO);

        if(VFD_Table[id].commData.loadCurrent > VFD_Table[id].warningCurrent && get_diff_ms(Driver_Table[id].runParam.workTimeStamp) > 1800){
            get_time_stamp(&VFD_Table[id].currentTooLowTimeStamp);
            if((get_diff_ms(VFD_Table[id].currentTooHighTimeStamp) > 1500 && isLimitTrigger)
            || (get_diff_ms(VFD_Table[id].currentTooHighTimeStamp) > 3000)){
                if(VFD_Table[id].VfdLoadCurrentSta != 1){
                    osal_error_event_callback(OSAL_MODULE_MOTOR, id, ERR_EVENT_CURRENT_ANOMALY, 1);
                    VFD_Table[id].VfdLoadCurrentSta = 1;
                }
                LOG_WARN("%s currnet too high, current %d warningCurrent %d", 
                Driver_Table[id].com.drvName, VFD_Table[id].commData.loadCurrent, VFD_Table[id].warningCurrent);
            }
        }
        else{
            get_time_stamp(&VFD_Table[id].currentTooHighTimeStamp);
            if(0 == VFD_Table[id].commData.loadCurrent && get_diff_ms(Driver_Table[id].runParam.workTimeStamp) > 1800){
                if(get_diff_ms(VFD_Table[id].currentTooLowTimeStamp) > 2000){
                    if(VFD_Table[id].VfdLoadCurrentSta != -1){
                        osal_error_event_callback(OSAL_MODULE_MOTOR, id, ERR_EVENT_CURRENT_ANOMALY, -1);
                        VFD_Table[id].VfdLoadCurrentSta = -1;
                    }
                    LOG_WARN("%s currnet too low, current %d warningCurrent %d", 
                    Driver_Table[id].com.drvName, VFD_Table[id].commData.loadCurrent, VFD_Table[id].warningCurrent);
                }
            }
            else{
                get_time_stamp(&VFD_Table[id].currentTooLowTimeStamp);
                if(VFD_Table[id].VfdLoadCurrentSta != 0){
                    osal_error_event_callback(OSAL_MODULE_MOTOR, id, ERR_EVENT_CURRENT_ANOMALY, 0);
                    VFD_Table[id].VfdLoadCurrentSta = 0;
                }
            }
        }
    }
}

void* osal_commumicate_VFD_thread(void* arg)
{
    uint8_t logCnt = 0;
    while (1)
    {
        for (uint8_t i = 0; i < DRIVER_ALL_NUM; i++)
        {
            if(isFoucsOnReadCurrentData){   // 专注采样驱动负载电流，保障电流数据的及时性
                if(VFD_TOP_BRUSH == i || VFD_SIDE_BRUSH == i ){
                    if(MOTOR_STA_HOLD_ON == Driver_Table[i].com.state || MOTOR_STA_MOVE == Driver_Table[i].com.state || MOTOR_STA_MOVE_FORE == Driver_Table[i].com.state 
                    || MOTOR_STA_MOVE_POS == Driver_Table[i].com.state || MOTOR_STA_MOVE_TIME == Driver_Table[i].com.state){
                        int current = osal_get_VFD_info(i, GET_VFD_CURRENT);
                        VFD_Table[i].commData.loadCurrent = (current > 0) ? current : 0;
                        VFD_load_current_check(i);
                    }
                    else{
                        VFD_Table[i].commData.loadCurrent = 0;
                    }
                    if(VFD_TOP_BRUSH == i) logCnt++;
                    // if(logCnt % 10 == 0) LOG_DEBUG(">>>%d current %d",i ,VFD_Table[i].commData.loadCurrent);
                    usleep(MS_US(5));
                }
            }
            else if(DRIVER_TYPE_VFD == Driver_Table[i].com.drvType){
                if(MOTOR_STA_HOLD_ON == Driver_Table[i].com.state || MOTOR_STA_MOVE == Driver_Table[i].com.state || MOTOR_STA_MOVE_FORE == Driver_Table[i].com.state 
                || MOTOR_STA_MOVE_POS == Driver_Table[i].com.state || MOTOR_STA_MOVE_TIME == Driver_Table[i].com.state){
                    int current = osal_get_VFD_info(i, GET_VFD_CURRENT);
                    VFD_Table[i].commData.loadCurrent = (current > 0) ? current : 0;
                    VFD_load_current_check(i);
                }
                else{
                    VFD_Table[i].commData.loadCurrent = 0;
                    // if(osal_is_io_trigger(BOARD5_INPUT_POWER_ON)){
                        int state = osal_get_VFD_info(i, GET_VFD_STATE);
                        VFD_commumicate_state_check(i, state);
                    // }
                }
                usleep(MS_US(200));
                // LOG_DEBUG("%s load current %d", Driver_Table[i].com.drvName, VFD_Table[i].commData.loadCurrent);
            }
        }
    }
}

/**
 * @brief       变频器初始化
 * @return      int                 
 */
static int osal_VFD_init(void)
{
    int ret = 0;

    ret |= osal_register_vfd_module(VFD_TOP_BRUSH,      9,  1, 9600, 100, BOARD1_INPUT_SIDE_BRUSH_ALARM) ? 0 : -1;
    ret |= osal_register_vfd_module(VFD_GANTRY,         10, 1, 9600, 100, BOARD1_INPUT_GANTRY_ALARM) ? 0 : -1;
    ret |= osal_register_vfd_module(VFD_DRYER,          11, 1, 9600, 100, BOARD1_INPUT_FAN_ALARM) ? 0 : -1;

    pthread_t osalModbusComm_thread;
    // pthread_create(&osalModbusComm_thread, NULL, osal_commumicate_VFD_thread, NULL);

    return ret;
}

/**
 * @brief       设置变频器是否专注于检测负载电流（不读取设备状态，减少通讯时间，保障电流采样及时性）
 * @param[in]	value               
 * @return      void                 
 */
void osal_set_vfd_focus_on_read_current(bool value)
{
    isFoucsOnReadCurrentData = value;
    LOG_INFO("Set VFD focus on read current value %d", value);
}

/**
 * @brief       获取VFD负载电流（缓存值）
 * @param[in]	id                
 * @return      int                 
 */
int osal_get_VFD_load_current(Type_DriverIndex_Enum id)
{
    if(Driver_Table[id].com.drvType != DRIVER_TYPE_VFD){
        LOG_WARN("Driver id %d drive type not VFD");
        return 0xFFFF;
    }

    return VFD_Table[id].commData.loadCurrent;
}

/**
 * @brief       设置变频器的警告电流（软件判断的异常值，非变频器自检值）
 * @param[in]	id                
 * @param[in]	value             
 * @return      int                 
 */
void osal_set_VFD_load_warning_current(Type_DriverIndex_Enum id, int value)
{
    if(Driver_Table[id].com.drvType != DRIVER_TYPE_VFD){
        LOG_WARN("Driver %d not VFD, set Failed");
    }
    else{
        VFD_Table[id].warningCurrent = value;
        LOG_INFO("Set driver %d warning current %d", id, value);
    }
}

/*                                                         =======================                                                         */
/* ========================================================       水系统控制       ======================================================== */
/*                                                         =======================                                                         */

#define WATER_CTL_CMD_STORE_MAX_NUM     (WATER_CTL_NUM) //水系统控制存储的最大指令数

//水路控制的动作IO
typedef struct{
    Type_OutputIo_Enum      waterPump;            //水泵
    Type_OutputIo_Enum      detergentPump;        //药剂泵
    Type_OutputIo_Enum      waterValue;           //水阀
    Type_OutputIo_Enum      AirValue;             //气阀
} Type_CrlAction_Def;

typedef struct{
    Type_WaterSystem_Enum   type;                   //水类型
    char                    name[NAME_BUFF_MAX];    //水路名称
    bool                    isOpen;                 //是否开启
    bool                    isDrainCrl;             //排水时是否动作
    Type_CrlAction_Def      actionMatchIo;          //动作的匹配IO
} Type_WaterCrlInfo_Def;
Type_WaterCrlInfo_Def ctlCmd[WATER_CTL_CMD_STORE_MAX_NUM];
static uint8_t executeCmdId = 0;

/* 单泵控制表，只在调试的时候使用 */
Type_WaterCrlInfo_Def onlyPumpCrl_Table[] = {
    //水类型                    //水路名称              //是否开启    //排水时是否动作   //水泵的匹配IO
    {WATER_LOW_PUMP,            "LOW_PUMP",             false,      false,  {BOARD1_OUTPUT_WATER_PUMP,       OUTPUT_IO_NULL, OUTPUT_IO_NULL, OUTPUT_IO_NULL}},
};

/* 组合水路控制表 */
Type_WaterCrlInfo_Def waterCrl_Table[] = {
    //水类型                    //水路名称              //是否开启   //排水时是否动作   //水泵的匹配IO（只有高压和低压泵）  //药剂泵的匹配IO      //水阀的匹配IO                       //气阀的匹配IO
    {WATER_CURTAIN,             "CURTAIN",              false,      true,   {BOARD1_OUTPUT_WATER_PUMP,  OUTPUT_IO_NULL,                 BOARD1_OUTPUT_WATER_CURTAIN,        OUTPUT_IO_NULL}},
    {WATER_FOAM,                "FOAM",                 false,      true,   {BOARD1_OUTPUT_WATER_PUMP,  OUTPUT_IO_NULL,                 BOARD1_OUTPUT_WATER_FOAM,           OUTPUT_IO_NULL}},
    {WATER_BRUSH,               "BRUSH",                false,      true,   {BOARD1_OUTPUT_WATER_PUMP,  OUTPUT_IO_NULL,                 BOARD1_OUTPUT_WATER_BRUSH,          OUTPUT_IO_NULL}},
    {WATER_HIGHPRES,            "HIGHPRES",             false,      true,   {BOARD1_OUTPUT_WATER_PUMP,  OUTPUT_IO_NULL,                 BOARD1_OUTPUT_WATER_HIGHPRES,       OUTPUT_IO_NULL}},
    // {WATER_DRAIN,               "DRAIN",                false,      true,   {OUTPUT_IO_NULL,            OUTPUT_IO_NULL,                 OUTPUT_IO_NULL,                     BOARD5_OUTPUT_DRAIN_WATER}},
};

#define ONLY_PUMP_CTL_TABLE_NUM (sizeof(onlyPumpCrl_Table) / sizeof(onlyPumpCrl_Table[0]))
#define WATER_CTL_TABLE_NUM     (sizeof(waterCrl_Table) / sizeof(waterCrl_Table[0]))

static bool isConfigWater[WATER_CTL_NUM];
static bool isWaterEventTrigger[WATER_EVENT_NUM] = {0};   //事件是否触发
static bool isRoSpareEnable = false;

/**
 * @brief       检查水系统状态
 * @param[in]	value               
 * @return      void                 
 */
static void water_system_err_check(void)
{
    // static struct timespec lowPressPumpWorkTimeStamp;
    // static struct timespec highPressPumpWorkTimeStamp;
    // static struct timespec roPumpWorkTimeStamp;

    // //查看各水路开启状态
    // bool isHighPressWaterWork = false;
    // bool isLowPressWaterWork = false;
    // bool isRoWaterWork = false;
    // for (uint8_t i = 0; i < WATER_CTL_TABLE_NUM; i++)
    // {
    //     if(BOARD5_OUTPUT_HIGH_PRESS_PUMP == waterCrl_Table[i].actionMatchIo.waterPump){
    //         if(waterCrl_Table[i].isOpen) isHighPressWaterWork = true;
    //     }
    //     else if(BOARD5_OUTPUT_LOW_PRESS_PUMP == waterCrl_Table[i].actionMatchIo.waterPump){
    //         if(waterCrl_Table[i].isOpen) isLowPressWaterWork = true;
    //     }
    //     else if(BOARD5_OUTPUT_RO_WATER_PUMP == waterCrl_Table[i].actionMatchIo.waterPump){
    //         if(waterCrl_Table[i].isOpen) isRoWaterWork = true;
    //     }
    // }
    // //高压泵水路异常检测
    // if(isHighPressWaterWork){
    //     if(get_diff_ms(highPressPumpWorkTimeStamp) > 2000){
    //         //检查驱动
    //         if(!isWaterEventTrigger[WATER_EVENT_HIGH_PUMP]){
    //             if(osal_is_signal_filter_trigger(SIGNAL_HIGH_PRESS_PUMP_ERR)){
    //                 osal_error_event_callback(OSAL_MODULE_WATER, WATER_EVENT_HIGH_PUMP, ERR_EVENT_DRIVER_FAULT, 1);
    //                 isWaterEventTrigger[WATER_EVENT_HIGH_PUMP] = true;
    //             }
    //             /* else if(!osal_is_signal_filter_trigger(SIGNAL_HIGH_PRESS_PUMP_WORK)){
    //                 osal_error_event_callback(OSAL_MODULE_WATER, WATER_EVENT_HIGH_PUMP, ERR_EVENT_DRIVE_FAILED, 1);
    //                 isWaterEventTrigger[WATER_EVENT_HIGH_PUMP] = true;
    //             } */
    //             //检查水压
    //             if(get_diff_ms(highPressPumpWorkTimeStamp) > 5000){
    //                 if(!isWaterEventTrigger[WATER_EVENT_HIGH_WATER_PRESS] && osal_is_signal_filter_trigger(SIGNAL_HIGH_WATER_PRESS)){
    //                     osal_error_event_callback(OSAL_MODULE_WATER, WATER_EVENT_HIGH_WATER_PRESS, ERR_EVENT_LOW_PRESS, 1);
    //                     isWaterEventTrigger[WATER_EVENT_HIGH_WATER_PRESS] = true;
    //                 }
    //                 else if(isWaterEventTrigger[WATER_EVENT_HIGH_WATER_PRESS] && !osal_is_signal_filter_trigger(SIGNAL_HIGH_WATER_PRESS)){
    //                     osal_error_event_callback(OSAL_MODULE_WATER, WATER_EVENT_HIGH_WATER_PRESS, ERR_EVENT_LOW_PRESS, 0);
    //                     isWaterEventTrigger[WATER_EVENT_HIGH_WATER_PRESS] = false;
    //                 }
    //             }
    //         }
    //         else{
    //             // if(!osal_is_signal_filter_trigger(SIGNAL_HIGH_PRESS_PUMP_ERR) && osal_is_signal_filter_trigger(SIGNAL_HIGH_PRESS_PUMP_WORK)){
    //             if(!osal_is_signal_filter_trigger(SIGNAL_HIGH_PRESS_PUMP_ERR)){
    //                 isWaterEventTrigger[WATER_EVENT_HIGH_PUMP] = false;
    //             }
    //             if(!osal_is_signal_filter_trigger(SIGNAL_HIGH_PRESS_PUMP_ERR)){
    //                 osal_error_event_callback(OSAL_MODULE_WATER, WATER_EVENT_HIGH_PUMP, ERR_EVENT_DRIVE_FAILED, 0);
    //             }
    //             // if(osal_is_signal_filter_trigger(SIGNAL_HIGH_PRESS_PUMP_WORK)){
    //             //     osal_error_event_callback(OSAL_MODULE_WATER, WATER_EVENT_HIGH_PUMP, ERR_EVENT_DRIVE_FAILED, 0);
    //             // }
    //         }
    //     }
    // }
    // else{
    //     get_time_stamp(&highPressPumpWorkTimeStamp);
    // }
    // //低压泵水路检测
    // if(isLowPressWaterWork){
    //     if(get_diff_ms(lowPressPumpWorkTimeStamp) > 2000){
    //         //检查驱动
    //         if(!isWaterEventTrigger[WATER_EVENT_LOW_PUMP]){
    //             if(osal_is_signal_filter_trigger(SIGNAL_LOW_PRESS_PUMP_ERR)){
    //                 osal_error_event_callback(OSAL_MODULE_WATER, WATER_EVENT_LOW_PUMP, ERR_EVENT_DRIVE_FAILED, 1);
    //                 isWaterEventTrigger[WATER_EVENT_LOW_PUMP] = true;
    //             }
    //             //检查水压
    //             if(get_diff_ms(lowPressPumpWorkTimeStamp) > 5000){
    //                 if(!isWaterEventTrigger[WATER_EVENT_LOW_WATER_PRESS] && osal_is_signal_filter_trigger(SIGNAL_LOW_WATER_PRESS)){
    //                     osal_error_event_callback(OSAL_MODULE_WATER, WATER_EVENT_LOW_WATER_PRESS, ERR_EVENT_LOW_PRESS, 1);
    //                     isWaterEventTrigger[WATER_EVENT_LOW_WATER_PRESS] = true;
    //                 }
    //                 else if(isWaterEventTrigger[WATER_EVENT_LOW_WATER_PRESS] && !osal_is_signal_filter_trigger(SIGNAL_LOW_WATER_PRESS)){
    //                     osal_error_event_callback(OSAL_MODULE_WATER, WATER_EVENT_LOW_WATER_PRESS, ERR_EVENT_LOW_PRESS, 0);
    //                     isWaterEventTrigger[WATER_EVENT_LOW_WATER_PRESS] = false;
    //                 }
    //             }
    //         }
    //         else if(!osal_is_signal_filter_trigger(SIGNAL_LOW_PRESS_PUMP_ERR)){
    //             osal_error_event_callback(OSAL_MODULE_WATER, WATER_EVENT_LOW_PUMP, ERR_EVENT_DRIVE_FAILED, 0);
    //             isWaterEventTrigger[WATER_EVENT_LOW_PUMP] = false;
    //         }
    //     }
    // }
    // else{
    //     get_time_stamp(&lowPressPumpWorkTimeStamp);
    // }
    // //RO泵水路驱动异常检测
    // if(isRoWaterWork){
    //     if(get_diff_ms(roPumpWorkTimeStamp) > 2000){
    //         //检查驱动——————开启备用水时，使用的低压水，只检测低压泵的开启情况
    //         if(isRoSpareEnable){
    //             if(!isWaterEventTrigger[WATER_EVENT_LOW_PUMP]){
    //                 if(osal_is_signal_filter_trigger(SIGNAL_LOW_PRESS_PUMP_ERR)){
    //                     osal_error_event_callback(OSAL_MODULE_WATER, WATER_EVENT_LOW_PUMP, ERR_EVENT_DRIVE_FAILED, 1);
    //                     isWaterEventTrigger[WATER_EVENT_LOW_PUMP] = true;
    //                 }
    //             }
    //             else if(!osal_is_signal_filter_trigger(SIGNAL_LOW_PRESS_PUMP_ERR)){
    //                 osal_error_event_callback(OSAL_MODULE_WATER, WATER_EVENT_LOW_PUMP, ERR_EVENT_DRIVE_FAILED, 0);
    //                 isWaterEventTrigger[WATER_EVENT_LOW_PUMP] = false;
    //             }
    //         }
    //         else{
    //             if(!isWaterEventTrigger[WATER_EVENT_RO_PUMP]){
    //                 if(osal_is_signal_filter_trigger(SIGNAL_RO_PRESS_PUMP_ERR)){
    //                     osal_error_event_callback(OSAL_MODULE_WATER, WATER_EVENT_RO_PUMP, ERR_EVENT_DRIVE_FAILED, 1);
    //                     isWaterEventTrigger[WATER_EVENT_RO_PUMP] = true;
    //                 }
    //                 //检查水压
    //                 if(get_diff_ms(roPumpWorkTimeStamp) > 5000){
    //                     if(!isRoSpareEnable && osal_is_signal_filter_trigger(SIGNAL_RO_WATER_PRESS)){
    //                         isRoSpareEnable = true;
    //                     }
    //                     if(!isWaterEventTrigger[WATER_EVENT_RO_WATER_PRESS] && osal_is_signal_filter_trigger(SIGNAL_RO_WATER_PRESS)){
    //                         osal_error_event_callback(OSAL_MODULE_WATER, WATER_EVENT_RO_WATER_PRESS, ERR_EVENT_LOW_PRESS, 1);
    //                         isWaterEventTrigger[WATER_EVENT_RO_WATER_PRESS] = true;
    //                     }
    //                     else if(isWaterEventTrigger[WATER_EVENT_RO_WATER_PRESS] && !osal_is_signal_filter_trigger(SIGNAL_RO_WATER_PRESS)){
    //                         osal_error_event_callback(OSAL_MODULE_WATER, WATER_EVENT_RO_WATER_PRESS, ERR_EVENT_LOW_PRESS, 0);
    //                         isWaterEventTrigger[WATER_EVENT_RO_WATER_PRESS] = false;
    //                     }
    //                 }
    //             }
    //             else if(!osal_is_signal_filter_trigger(SIGNAL_RO_PRESS_PUMP_ERR)){
    //                 osal_error_event_callback(OSAL_MODULE_WATER, WATER_EVENT_RO_PUMP, ERR_EVENT_DRIVE_FAILED, 0);
    //                 isWaterEventTrigger[WATER_EVENT_RO_PUMP] = false;
    //             }
    //         }
    //     }
    // }
    // else{
    //     isRoSpareEnable = false;
    //     get_time_stamp(&roPumpWorkTimeStamp);
    // }
}

/**
 * @brief       水系统控制，遵循先开阀，再开泵；先关泵，再关阀的原则
 * @param[in]	arg                 
 */
void* osal_water_system_ctl_thread(void* arg)
{
    bool isLowPressPumpWorking = false;
    bool isHighPressPumpWorking = false;
    bool isRoPumpWorking = false;
    bool recoderRoSpareSta = false;

    while (1)
    {
        // water_system_err_check();
        // if(recoderRoSpareSta != isRoSpareEnable){
        //     recoderRoSpareSta = isRoSpareEnable;
        //     if(isRoSpareEnable){      //需要开启或关闭RO备用水时，插入一条开启指令
        //         if(executeCmdId < WATER_CTL_CMD_STORE_MAX_NUM){
        //             for (uint8_t i = executeCmdId; i > 0; i--)
        //             {
        //                 ctlCmd[i].type  = ctlCmd[i - 1].type;
        //                 ctlCmd[i].isOpen= ctlCmd[i - 1].isOpen;
        //                 strcpy(ctlCmd[i].name, ctlCmd[i - 1].name);
        //             }
        //             ctlCmd[0].type      = WATER_RO_SPARE;
        //             ctlCmd[0].isOpen    = true;
        //             strcpy(ctlCmd[0].name, "RO_SPARE");
        //             executeCmdId++;
        //             osal_io_state_change(BOARD5_OUTPUT_RO_WATER_PUMP, IO_DISABLE);      //立即关闭RO水泵
        //         }
        //     }
        //     else{
        //         for (uint8_t i = executeCmdId; i > 0; i--)
        //         {
        //             ctlCmd[i].type  = ctlCmd[i - 1].type;
        //             ctlCmd[i].isOpen= ctlCmd[i - 1].isOpen;
        //             strcpy(ctlCmd[i].name, ctlCmd[i - 1].name);
        //         }
        //         ctlCmd[0].type      = WATER_RO_SPARE;
        //         ctlCmd[0].isOpen    = false;
        //         strcpy(ctlCmd[0].name, "RO_SPARE");
        //         executeCmdId++;
        //     }
        // }

        if(0 == executeCmdId){      //无控制指令则只进行事件处理，不改变状态
            usleep(MS_US(500));
            continue;
        }

        //按收到指令的顺序执行
        if(WATER_DRAIN == ctlCmd[0].type){
            if(ctlCmd[0].isOpen){
                isLowPressPumpWorking = false;
                isHighPressPumpWorking = false;
                isRoPumpWorking = false;
                uint8_t drainWaterIndex = 0;
                for (uint8_t i = 0; i < WATER_CTL_TABLE_NUM; i++)           //关闭所有泵和气阀
                {
                    if(waterCrl_Table[i].actionMatchIo.waterPump != OUTPUT_IO_NULL)     osal_io_state_change(waterCrl_Table[i].actionMatchIo.waterPump, IO_DISABLE);
                    if(waterCrl_Table[i].actionMatchIo.detergentPump != OUTPUT_IO_NULL) osal_io_state_change(waterCrl_Table[i].actionMatchIo.detergentPump, IO_DISABLE);
                    if(waterCrl_Table[i].actionMatchIo.AirValue != OUTPUT_IO_NULL)      osal_io_state_change(waterCrl_Table[i].actionMatchIo.AirValue, IO_DISABLE);
                    if(WATER_DRAIN == waterCrl_Table[i].type) drainWaterIndex = i;  //找到排水在表中索引 
                    
                }
                usleep(MS_US(500));
                for (uint8_t i = 0; i < WATER_CTL_TABLE_NUM; i++)           //打开水阀排水
                {
                    if(waterCrl_Table[i].isDrainCrl && waterCrl_Table[i].actionMatchIo.waterValue != OUTPUT_IO_NULL){
                        osal_io_state_change(waterCrl_Table[i].actionMatchIo.waterValue, IO_ENABLE);
                        waterCrl_Table[i].isOpen = true;
                    }
                }
                if(waterCrl_Table[drainWaterIndex].actionMatchIo.AirValue != OUTPUT_IO_NULL){
                    osal_io_state_change(waterCrl_Table[drainWaterIndex].actionMatchIo.AirValue, IO_ENABLE);    //打开排水气阀
                }
            }
            else{
                for (uint8_t i = 0; i < WATER_CTL_TABLE_NUM; i++)           //关闭所有泵和阀
                {
                    if(waterCrl_Table[i].actionMatchIo.waterPump != OUTPUT_IO_NULL)     osal_io_state_change(waterCrl_Table[i].actionMatchIo.waterPump, IO_DISABLE);
                    if(waterCrl_Table[i].actionMatchIo.detergentPump != OUTPUT_IO_NULL) osal_io_state_change(waterCrl_Table[i].actionMatchIo.detergentPump, IO_DISABLE);
                    if(waterCrl_Table[i].actionMatchIo.waterValue != OUTPUT_IO_NULL)    osal_io_state_change(waterCrl_Table[i].actionMatchIo.waterValue, IO_DISABLE);
                    if(waterCrl_Table[i].actionMatchIo.AirValue != OUTPUT_IO_NULL)      osal_io_state_change(waterCrl_Table[i].actionMatchIo.AirValue, IO_DISABLE);
                    waterCrl_Table[i].isOpen = false;
                }
            }
            goto CTL_END;                                       //操作排水排气时不再操作其它
        }
        
        //关闭单路水路的所有输出
        // if(WATER_ALL_LOW_PRESS == ctlCmd[0].type || WATER_ALL_HIGH_PRESS == ctlCmd[0].type || WATER_ALL_RO_PRESS == ctlCmd[0].type){
        //     Type_OutputIo_Enum pumpIo = OUTPUT_IO_NULL;
        //     if(WATER_ALL_LOW_PRESS == ctlCmd[0].type)       pumpIo = BOARD5_OUTPUT_LOW_PRESS_PUMP;
        //     else if(WATER_ALL_HIGH_PRESS == ctlCmd[0].type) pumpIo = BOARD5_OUTPUT_HIGH_PRESS_PUMP;
        //     else if(WATER_ALL_RO_PRESS == ctlCmd[0].type)   pumpIo = BOARD5_OUTPUT_RO_WATER_PUMP;

        //     if(pumpIo != OUTPUT_IO_NULL) osal_io_state_change(pumpIo, IO_DISABLE);
        //     usleep(MS_US(300));
        //     for (uint8_t i = 0; i < WATER_CTL_TABLE_NUM; i++)
        //     {
        //         if(pumpIo == waterCrl_Table[i].actionMatchIo.waterPump){
        //             if(waterCrl_Table[i].actionMatchIo.detergentPump != OUTPUT_IO_NULL) osal_io_state_change(waterCrl_Table[i].actionMatchIo.detergentPump, IO_DISABLE);
        //             if(waterCrl_Table[i].actionMatchIo.waterValue != OUTPUT_IO_NULL)    osal_io_state_change(waterCrl_Table[i].actionMatchIo.waterValue, IO_DISABLE);
        //             if(waterCrl_Table[i].actionMatchIo.AirValue != OUTPUT_IO_NULL)      osal_io_state_change(waterCrl_Table[i].actionMatchIo.AirValue, IO_DISABLE);
        //             waterCrl_Table[i].isOpen = false;
        //         }
        //     }
        //     goto CTL_END;
        // }

        bool isCrlLowPumpOpen = false;
        bool isCrlHighPumpOpen = false;
        bool isCrlRoPumpOpen = false;
        bool isNewLowPressWaterOpen = false;
        bool isNewHighPressWaterOpen = false;
        bool isNewHRoWaterOpen = false;
        //先开启需要开启的水路阀，保证水泵有压力释放口
        if(ctlCmd[0].type != WATER_ALL){                        //所有水路控制只支持全部关闭
            for (uint8_t i = 0; i < WATER_CTL_TABLE_NUM; i++){
                if(waterCrl_Table[i].type == ctlCmd[0].type){
                    waterCrl_Table[i].isOpen = ctlCmd[0].isOpen;
                    if(waterCrl_Table[i].isOpen){
                        if(BOARD1_OUTPUT_WATER_PUMP == waterCrl_Table[i].actionMatchIo.waterPump)   isNewLowPressWaterOpen = true;
                        if(BOARD1_OUTPUT_WATER_PUMP == waterCrl_Table[i].actionMatchIo.waterPump)  isNewHighPressWaterOpen = true;
                        if(BOARD1_OUTPUT_WATER_PUMP == waterCrl_Table[i].actionMatchIo.waterPump){
                            if(isRoSpareEnable) isNewLowPressWaterOpen = true;      //开启RO备用水时，RO水路使用低压泵供水
                            else                isNewHRoWaterOpen = true;
                        }

                        if(waterCrl_Table[i].actionMatchIo.waterValue != OUTPUT_IO_NULL)    osal_io_state_change(waterCrl_Table[i].actionMatchIo.waterValue, IO_ENABLE);
                        if(waterCrl_Table[i].actionMatchIo.detergentPump != OUTPUT_IO_NULL) osal_io_state_change(waterCrl_Table[i].actionMatchIo.detergentPump, IO_ENABLE);
                        if(waterCrl_Table[i].actionMatchIo.AirValue != OUTPUT_IO_NULL)      osal_io_state_change(waterCrl_Table[i].actionMatchIo.AirValue, IO_ENABLE);
                    }
                }
                if(waterCrl_Table[i].isOpen){                   //查看当前是否需要开启高/低压泵
                    if(BOARD1_OUTPUT_WATER_PUMP == waterCrl_Table[i].actionMatchIo.waterPump)   isCrlLowPumpOpen = true;
                    if(BOARD1_OUTPUT_WATER_PUMP == waterCrl_Table[i].actionMatchIo.waterPump)  isCrlHighPumpOpen = true;
                    if(BOARD1_OUTPUT_WATER_PUMP == waterCrl_Table[i].actionMatchIo.waterPump){
                        if(isRoSpareEnable) isCrlLowPumpOpen = true;
                        else                isCrlRoPumpOpen = true;
                    }
                }
            }
        }
        if(isCrlLowPumpOpen || isCrlHighPumpOpen || isCrlRoPumpOpen){
            if(isNewLowPressWaterOpen || isNewHighPressWaterOpen || isNewHRoWaterOpen)  usleep(MS_US(200));  //有新水路开时，延时一段时间再关闭不开的水路，避免水路切换瞬间所有水路阀都关闭
            //没有低高/压泵系统水路开启时，先关泵再关阀
            if(!isCrlLowPumpOpen && isLowPressPumpWorking){
                osal_io_state_change(BOARD1_OUTPUT_WATER_PUMP, IO_DISABLE);
                isLowPressPumpWorking = false;
                usleep(MS_US(300));
            }
            else if(!isCrlHighPumpOpen && isHighPressPumpWorking){
                osal_io_state_change(BOARD1_OUTPUT_WATER_PUMP, IO_DISABLE);
                isHighPressPumpWorking = false;
                usleep(MS_US(300));
            }
            else if(!isCrlRoPumpOpen && isRoPumpWorking){
                osal_io_state_change(BOARD1_OUTPUT_WATER_PUMP, IO_DISABLE);
                isRoPumpWorking = false;
                usleep(MS_US(300));
            }

            for (uint8_t i = 0; i < WATER_CTL_TABLE_NUM; i++)
            {
                if(!waterCrl_Table[i].isOpen){
                    if(waterCrl_Table[i].actionMatchIo.waterValue != OUTPUT_IO_NULL)    osal_io_state_change(waterCrl_Table[i].actionMatchIo.waterValue, IO_DISABLE);
                    if(waterCrl_Table[i].actionMatchIo.detergentPump != OUTPUT_IO_NULL) osal_io_state_change(waterCrl_Table[i].actionMatchIo.detergentPump, IO_DISABLE);
                    if(waterCrl_Table[i].actionMatchIo.AirValue != OUTPUT_IO_NULL)      osal_io_state_change(waterCrl_Table[i].actionMatchIo.AirValue, IO_DISABLE);
                }
            }
            //之前没开启水泵就开启水泵
            if(isCrlLowPumpOpen && !isLowPressPumpWorking){
                usleep(MS_US(300));                             //延时一段时间后开启水泵
                osal_io_state_change(BOARD1_OUTPUT_WATER_PUMP, IO_ENABLE);
                isLowPressPumpWorking = true;
            }
            if(isCrlHighPumpOpen && !isHighPressPumpWorking){
                usleep(MS_US(300));                             //延时一段时间后开启水泵
                osal_io_state_change(BOARD1_OUTPUT_WATER_PUMP, IO_ENABLE);
                isHighPressPumpWorking = true;
            }
            if(isCrlRoPumpOpen && !isRoPumpWorking){
                usleep(MS_US(300));                             //延时一段时间后开启水泵
                osal_io_state_change(BOARD1_OUTPUT_WATER_PUMP, IO_ENABLE);
                isRoPumpWorking = true;
            }
        }
        else{                                                   //高低压水路都关闭
            if(isLowPressPumpWorking || isHighPressPumpWorking || isRoPumpWorking){
                osal_io_state_change(BOARD1_OUTPUT_WATER_PUMP, IO_DISABLE);
                osal_io_state_change(BOARD1_OUTPUT_WATER_PUMP, IO_DISABLE);
                osal_io_state_change(BOARD1_OUTPUT_WATER_PUMP, IO_DISABLE);
                usleep(MS_US(300));
                for (uint8_t i = 0; i < WATER_CTL_TABLE_NUM; i++)
                {
                    if(waterCrl_Table[i].actionMatchIo.detergentPump != OUTPUT_IO_NULL) osal_io_state_change(waterCrl_Table[i].actionMatchIo.detergentPump, IO_DISABLE);
                    if(waterCrl_Table[i].actionMatchIo.waterValue != OUTPUT_IO_NULL)    osal_io_state_change(waterCrl_Table[i].actionMatchIo.waterValue, IO_DISABLE);
                    if(waterCrl_Table[i].actionMatchIo.AirValue != OUTPUT_IO_NULL)      osal_io_state_change(waterCrl_Table[i].actionMatchIo.AirValue, IO_DISABLE);
                    waterCrl_Table[i].isOpen = false;
                }
            }
            isLowPressPumpWorking = false;
            isHighPressPumpWorking = false;
            isRoPumpWorking = false;
        }

CTL_END:                                //指令执行完毕后，储存的指令前移
        LOG_INFO("Water control %s %s, have %d cmd left", ctlCmd[0].name, ctlCmd[0].isOpen ? "open" : "close", executeCmdId - 1);
        if(executeCmdId > 0){
            for (uint8_t i = 0; i < executeCmdId - 1; i++)
            {
                ctlCmd[i].type  = ctlCmd[i + 1].type;
                ctlCmd[i].isOpen= ctlCmd[i + 1].isOpen;
                strcpy(ctlCmd[i].name, ctlCmd[i + 1].name);
            }
            executeCmdId--;
        }
    }
}

/**
 * @brief       水系统相关控制
 * @param[in]	type                控制类型
 * @param[in]	enable              使能
 * @return      int
 */
static int osal_water_system_control(Type_WaterSystem_Enum type, bool enable)
{
    // if(!isConfigWater[type]){
    //     LOG_WARN("Water %d no config, exit", type);
    //     return false;
    // }

    for (uint8_t i = 0; i < ONLY_PUMP_CTL_TABLE_NUM; i++){      //单水泵控制直接在这里执行
        if(type == onlyPumpCrl_Table[i].type && onlyPumpCrl_Table[i].actionMatchIo.waterPump != OUTPUT_IO_NULL){
            osal_io_state_change(onlyPumpCrl_Table[i].actionMatchIo.waterPump, enable ? IO_ENABLE : IO_DISABLE);
            onlyPumpCrl_Table[i].isOpen = enable;
            return 0;
        }
    }
    
    if(executeCmdId < WATER_CTL_CMD_STORE_MAX_NUM){
        ctlCmd[executeCmdId].type = type;           //只赋值两个值，其它值固定不变
        ctlCmd[executeCmdId].isOpen = enable;
        for (uint8_t i = 0; i < WATER_CTL_TABLE_NUM; i++)
        {
            if(waterCrl_Table[i].type == ctlCmd[executeCmdId].type){
                strcpy(ctlCmd[executeCmdId].name, waterCrl_Table[i].name);
                break;
            }
            if(WATER_CTL_TABLE_NUM - 1 == i)    sprintf(ctlCmd[executeCmdId].name, "%d", ctlCmd[executeCmdId].type);
        }
    }
    else{
        LOG_WARN("Water ctl cmd too much");
        return -1;
    }
    executeCmdId++;
    return 0;
}

static bool osal_is_water_working(Type_WaterSystem_Enum type)
{
    for (uint8_t i = 0; i < ONLY_PUMP_CTL_TABLE_NUM; i++){      //单水泵状态返回值仅在单水泵控制时才是准确的
        if(type == onlyPumpCrl_Table[i].type && onlyPumpCrl_Table[i].actionMatchIo.waterPump != OUTPUT_IO_NULL){
            LOG_WARN("This return status is correct only in pump ctl mode");
            return onlyPumpCrl_Table[i].isOpen;
        }
    }
    for (uint8_t i = 0; i < WATER_CTL_TABLE_NUM; i++)
    {
        if(type == waterCrl_Table[i].type){
            return waterCrl_Table[i].isOpen;
        }
    }
    LOG_WARN("Unregister water type %d", type);
    return false;
}

int osal_water_system_init(void)
{
    pthread_t osalWaterCrl_thread;
    pthread_create(&osalWaterCrl_thread, NULL, osal_water_system_ctl_thread, NULL);
    return 0;
}


/**
 * @brief       读取文件并返回文件内容
 * @param[in]	filename                文件路径
 * @return      char                
 */
char* read_file(const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Failed to open file");
        return NULL;
    }

    // 获取文件长度
    fseek(fp, 0, SEEK_END);
    long length = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // 分配内存并读取文件内容
    char *buffer = (char *)malloc(length + 1);  // 多分配一个字节用于存储 '\0'
    if (!buffer) {
        fclose(fp);
        perror("Failed to allocate memory for file content");
        return NULL;
    }

    long bytesRead = fread(buffer, 1, length, fp);
    if (bytesRead != length) {
        fprintf(stderr, "Error: Only %zu bytes read, expected %ld bytes\n", bytesRead, length);
        free(buffer);
        fclose(fp);
        return NULL;
    }
    buffer[bytesRead] = '\0';  // 确保字符串以 '\0' 结尾
    fclose(fp);
    return buffer;
}

/**
 * @brief       将数据内容写入文件
 * @param[in]	filename                文件路径
 * @param[in]	content                 文件内容
 * @return      void                
 */
void write_file(const char *filename, const char *content)
{
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("Failed to open file for writing");
        return;
    }

    fwrite(content, strlen(content), 1, fp);
    fclose(fp);
}

/*                                                         =======================                                                         */
/* ========================================================     信号触发状态监测   ======================================================== */
/*                                                         =======================                                                         */

#define IO_IS_TRIGGER               (-1)            //信号已触发
#define SIGNAL_STABLE               (0)             //信号保持状态
#define IO_NO_TRIGGER               (1)             //信号未触发

//这里的信号触发判定时间较长，对于需要立即响应的信号，不取这里的状态判定值（如报警状态和限位触发）
//信号信息匹配表
Type_SignalStaInfo_Def SignalInfo_Table[] = {
//  信号类型                        //对应的检测引脚                          //信号状态       //信号down/up需确认次数  靠近/离开时的位置
    // 对射光电类
    {SIGNAL_FRONT_WHEEL,            BOARD1_INPUT_FRONT_WHEEL_LIMIT,         SIGNAL_STABLE,  20,     20,             0,  0},
    {SIGNAL_REAR_WHEEL_LOCK1,       BOARD1_INPUT_REAR_WHEEL_LOCK1,          SIGNAL_STABLE,  20,     20,             0,  0},
    {SIGNAL_REAR_WHEEL_LOCK2,       BOARD1_INPUT_REAR_WHEEL_LOCK2,          SIGNAL_STABLE,  20,     20,             0,  0},
    // 接近开关类
    {SIGNAL_OVERHEIGHT_DETECT,      BOARD1_INPUT_OVERHEIGHT_DETECT,         SIGNAL_STABLE,  20,     20,             0,  0},
    {SIGNAL_REAR_LOCK_HOME,         BOARD1_INPUT_REAR_LOCK_HOME,            SIGNAL_STABLE,  20,     20,             0,  0},
    {SIGNAL_BUMPER_LEFT,            BOARD1_INPUT_BUMPER_LEFT,               SIGNAL_STABLE,  20,     20,             0,  0},
    {SIGNAL_BUMPER_RIGHT,           BOARD1_INPUT_BUMPER_RIGHT,              SIGNAL_STABLE,  20,     20,             0,  0},
    {SIGNAL_GANTRY_FWD_LIMIT,       BOARD1_INPUT_GANTRY_FWD_LIMIT,          SIGNAL_STABLE,  20,     20,             0,  0},
    {SIGNAL_GANTRY_REV_LIMIT,       BOARD1_INPUT_GANTRY_REV_LIMIT,          SIGNAL_STABLE,  20,     20,             0,  0},
    {SIGNAL_LIFT_UP_LIMIT,          BOARD1_INPUT_LIFT_UP_LIMIT,             SIGNAL_STABLE,  10,     10,             0,  0},
    {SIGNAL_LIFT_DOWN_LIMIT,        BOARD1_INPUT_LIFT_DOWN_LIMIT,           SIGNAL_STABLE,  10,     10,             0,  0},
    {SIGNAL_TOP_BRUSH_COLLISION,    BOARD1_INPUT_TOP_BRUSH_COLLISION,       SIGNAL_STABLE,  20,     20,             0,  0},
    {SIGNAL_BUMPER_ROD_LEFT,        BOARD1_INPUT_BUMPER_ROD_LEFT,           SIGNAL_STABLE,  20,     20,             0,  0},
    {SIGNAL_BUMPER_ROD_RIGHT,       BOARD1_INPUT_BUMPER_ROD_RIGHT,          SIGNAL_STABLE,  20,     20,             0,  0},
    {SIGNAL_HEIGHT_CONTROL,         BOARD1_INPUT_HEIGHT_CONTROL,            SIGNAL_STABLE,  20,     20,             0,  0},
    // 按键类
    // 其他
    {SIGNAL_LIFT_ALARM_FEEDBACK,    BOARD1_INPUT_LIFT_ALARM_FEEDBACK,       SIGNAL_STABLE,  20,     20,             0,  0},
    {SIGNAL_SIDE_BRUSH_OVERLOAD,    BOARD1_INPUT_SIDE_BRUSH_OVERLOAD,       SIGNAL_STABLE,  20,     20,             0,  0},
    {SIGNAL_TOP_BRUSH_OVERLOAD,     BOARD1_INPUT_TOP_BRUSH_OVERLOAD,        SIGNAL_STABLE,  20,     20,             0,  0},
    {SIGNAL_WATER_PUMP_OVERLOAD,    BOARD1_INPUT_WATER_PUMP_OVERLOAD,       SIGNAL_STABLE,  20,     20,             0,  0},
    {SIGNAL_GANTRY_ALARM,           BOARD1_INPUT_GANTRY_ALARM,              SIGNAL_STABLE,  20,     20,             0,  0},
    {SIGNAL_FAN_ALARM,              BOARD1_INPUT_FAN_ALARM,                 SIGNAL_STABLE,  20,     20,             0,  0},
    {SIGNAL_SIDE_BRUSH_ALARM,       BOARD1_INPUT_SIDE_BRUSH_ALARM,          SIGNAL_STABLE,  20,     20,             0,  0},
};

#define SIGNAL_TABLE_NUM (sizeof(SignalInfo_Table) / sizeof(SignalInfo_Table[0]))    //信号监测列表个数

static bool isConfigSensor[SIGNAL_NUM];

/**
 * @brief       光电信号触发判断（多次滤波判定）
 * @param[in]	signalTableId       光电信号类型
 */
static int signal_flip_dir(Type_SignalType_Enum signalTableId)
{
    static bool isCompleteFirstCheck[SIGNAL_TABLE_NUM] = {0};       //首次所有信号都确认赋值一遍
    static bool lasSignalState[SIGNAL_TABLE_NUM] = {0};
    static bool isStartConfirmSignal[SIGNAL_TABLE_NUM] = {0};
    static uint8_t signalFlipStableCnt[SIGNAL_TABLE_NUM] = {0};
    bool signalState = osal_is_io_trigger(SignalInfo_Table[signalTableId].matchIo);

    if(!isStartConfirmSignal[signalTableId] && isCompleteFirstCheck[signalTableId]){
        if(signalState != lasSignalState[signalTableId]) isStartConfirmSignal[signalTableId] = true;
        signalFlipStableCnt[signalTableId] = 0;
    }else{
        if(signalState != lasSignalState[signalTableId] && isCompleteFirstCheck[signalTableId]){
            isStartConfirmSignal[signalTableId] = false;
        }
        else{
            signalFlipStableCnt[signalTableId]++;
            uint8_t trigCnt = (true == signalState) ? SignalInfo_Table[signalTableId].trigDownCnt : SignalInfo_Table[signalTableId].trigUpCnt;
            if(signalFlipStableCnt[signalTableId] >= trigCnt){
                signalFlipStableCnt[signalTableId] = trigCnt;       //防止溢出
                isCompleteFirstCheck[signalTableId] = true;

                isStartConfirmSignal[signalTableId] = false;        //确认完后退出确认，避免影响下一次检测
                lasSignalState[signalTableId] = signalState;

                return (true == signalState) ? IO_IS_TRIGGER : IO_NO_TRIGGER;
            }
        }
    }
    lasSignalState[signalTableId] = signalState;
    return SIGNAL_STABLE;
}

/**
 * @brief       信号触发监测线程（有信号滤波处理）
 * @param[in]	arg                 
 */
void* osal_signal_trigger_thread(void* arg)
{
    while (1)
    {
        for (uint8_t i = 0; i < SIGNAL_TABLE_NUM; i++)
        {
            int trigDir = signal_flip_dir(i);
            if(IO_IS_TRIGGER == trigDir || IO_NO_TRIGGER == trigDir){
                LOG_DEBUG("Signal %d trig %s", SignalInfo_Table[i].signalType, (IO_IS_TRIGGER == trigDir) ? "Down" : "Up");
                // int trigPos = osal_get_dev_pos(VFD_CONVEYOR_2);
                // switch (SignalInfo_Table[i].signalType)
                // {
                // case SIGNAL_ENTRANCE:
                // case SIGNAL_AVOID_INTRUDE:
                // case SIGNAL_REAR_END_PROTECT:
                // case SIGNAL_EXIT:
                // case SIGNAL_FINISH:
                //     if(0 == trigPos) trigPos = 1;       //值为0用于未检测到的判定，所以最小值限制为1
                //     if(IO_IS_TRIGGER == trigDir)        SignalInfo_Table[i].closePos = trigPos;         //TODO 这里是靠近还是离开确认一下
                //     else if(IO_NO_TRIGGER == trigDir)   SignalInfo_Table[i].leavePos = trigPos;

                //     char *strName = (char*)malloc(50 * sizeof(char));
                //     if(strName){
                //         if(SIGNAL_ENTRANCE == SignalInfo_Table[i].signalType)               strcpy(strName, "SIGNAL_ENTRANCE");
                //         else if(SIGNAL_AVOID_INTRUDE == SignalInfo_Table[i].signalType)     strcpy(strName, "SIGNAL_AVOID_INTRUDE");
                //         else if(SIGNAL_REAR_END_PROTECT == SignalInfo_Table[i].signalType)  strcpy(strName, "SIGNAL_REAR_END_PROTECT");
                //         else if(SIGNAL_EXIT == SignalInfo_Table[i].signalType)              strcpy(strName, "SIGNAL_EXIT");
                //         else if(SIGNAL_FINISH == SignalInfo_Table[i].signalType)            strcpy(strName, "SIGNAL_FINISH");
                //         LOG_INFO("%s trig %s, pos %d", strName, (IO_IS_TRIGGER == trigDir) ? "leave" : "close", trigPos);
                //         free(strName);
                //     }
                //     break;
                // default:
                //     break;
                // }
                SignalInfo_Table[i].trigDir = trigDir;      //赋值当前信号源触发状态
            }
        }
        usleep(MS_US(20));
    }
}

Type_SignalStaInfo_Def *get_signal_handle(Type_SignalType_Enum type)
{
    return &SignalInfo_Table[type];
}

/**
 * @brief       返回信号触发状态（经过滤波）
 * @param[in]	type                
 * @return      bool                
 */
bool osal_is_signal_filter_trigger(Type_SignalType_Enum type)
{
    // if(!isConfigSensor[type]){
        // LOG_WARN("Sensor %d no config, exit", type);
    //     return false;
    // }

    for (uint8_t i = 0; i < SIGNAL_TABLE_NUM; i++)
    {
        if(type == SignalInfo_Table[i].signalType){
            //对射类光电正常电平状态为0，遮挡后为1认为触发了
            if(SIGNAL_FRONT_WHEEL == type){
            // || SIGNAL_REAR_WHEEL_LOCK1 == type
            // || SIGNAL_REAR_WHEEL_LOCK2 == type){
                return (IO_IS_TRIGGER == SignalInfo_Table[i].trigDir) ? false : true;
            }
            else{
                return (IO_NO_TRIGGER == SignalInfo_Table[i].trigDir) ? false : true;
            }
        }
    }
    LOG_WARN("No define signal type %d", type);
    return false;
}

/*                                                         =======================                                                         */
/* ========================================================      码盘位置更新      ======================================================== */
/*                                                         =======================                                                         */

#define ZERO_CHECK_CNT                      (10)

int io_pluse_clear(int boardId, int pin)
{
    int data = 0;
    return io_SDO_write(boardId, 0x2005, pin, &data);
}

/**
 * @brief       获取指定ID驱动的码盘计数值（记录的值）
 * @param[in]	id                  驱动驱动索引id
 * @return      int
 */
int osal_get_dev_pos(Type_DriverIndex_Enum id)
{
    if(Encoder_Table[id].isInit)    return Encoder_Table[id].encoderValue;
    else                            return -1;
}

/**
 * @brief       码盘值清零
 * @param[in]	id                  
 * @return      int                 
 */
int osal_clear_dev_encoder(Type_DriverIndex_Enum id)
{
    if(!Driver_Table[id].haveEncode){
        LOG_WARN("Driver %d not config encoder", id);
        return -1;
    }
    else if(!Encoder_Table[id].isInit){
        LOG_WARN("Driver %d encode not init", id);
        return -2;
    }

    Encoder_Table[id].isManualClearEncoder = true;
    return 0;
}

/**
 * @brief       更新已注册码盘的位置值（仅支持由子板记录脉冲值的码盘）
 * @param[in]	arg
 */
void* osal_encoder_update_thread(void* arg)
{
    uint8_t logCnt = 0;

    sleep(3);
    while (1)
    {
        // struct timespec timeStamp;
        // get_time_stamp(&timeStamp);
        //刷新脉冲计数值
        logCnt++;
        for (uint8_t i = 0; i < DRIVER_ALL_NUM; i++)
        {
            Type_DriverInfo_Def  *pDrv = &Driver_Table[i];
            Type_EncoderInfo_Def *pEnc = &Encoder_Table[i];

            if(!pDrv->haveEncode || !pEnc->isInit) continue;    //没有编码器或者未初始化的跳过

            if(0 == logCnt % 30){
                if(pEnc->lastEncoderValue != pEnc->encoderValue){
                    pEnc->lastEncoderValue = pEnc->encoderValue;
                    LOG_INFO("*****%s encoder upadte to %d", pDrv->com.drvName, pEnc->encoderValue);
                }
            }

            if(!isIoBoardOnline[BOARD_ID_RESOLUTION(pEnc->ioIndexPulse)]) continue;     //子板离线时跳过
            pEnc->readPulseValue = io_pluse_read(BOARD_ID_RESOLUTION(pEnc->ioIndexPulse), PIN_ID_RESOLUTION(pEnc->ioIndexPulse));
            if(0xFFFFFFF == pEnc->readPulseValue || pEnc->readPulseValue < 0){
                // osal_error_event_callback(OSAL_MODULE_MOTOR, i, ERR_EVENT_INCORRECT_POSITION, pDrv->runParam.storeActDir);
                LOG_WARN("Enc %s read err! value = %d", pDrv->com.drvName, pEnc->readPulseValue);
                continue; //读失败则跳过，此次不刷新
            }

            //手动清子板脉冲计数值（动作过程中清除会有少量脉冲漏计）
            if(pEnc->isManualClearEncoder){
                pEnc->isManualClearEncoder = false;
                uint8_t retryCnt = 6;
                while (pEnc->readPulseValue > 0xF && --retryCnt){
                    io_pluse_clear(BOARD_ID_RESOLUTION(pEnc->ioIndexPulse), PIN_ID_RESOLUTION(pEnc->ioIndexPulse));
                    usleep(MS_US(10));
                    pEnc->readPulseValue = io_pluse_read(BOARD_ID_RESOLUTION(pEnc->ioIndexPulse), PIN_ID_RESOLUTION(pEnc->ioIndexPulse));
                }
                if(retryCnt){
                    LOG_DEBUG("Clear encode %s pulse success", pDrv->com.drvName);
                    pEnc->encoderValue = 0;
                    pEnc->status.recoderEncValue = pEnc->encoderValue;
                }
                else{
                    LOG_WARN("Clear encode %s pulse failed", pDrv->com.drvName);
                }
                pEnc->flashPulseValue = pEnc->readPulseValue;
                continue;
            }

            if(!pDrv->runParam.isPulseCounting){
                pEnc->flashPulseValue = pEnc->readPulseValue;   //空闲时一直刷新记录的脉冲值
                pEnc->status.isErr = false;
                pEnc->status.errCnt = 0;
                pEnc->status.touchZeroCnt = 0;
                continue;                                       //不处于脉冲计数阶段或需要手动清零时不刷新脉冲值
            }

            //刷新脉冲值
            int encodeDiffValue = pEnc->readPulseValue - pEnc->flashPulseValue;
            if(encodeDiffValue < 0){                            //子板脉冲值只加不减，这里正常肯定不会小于0，小于零时跳过（每次循环时间很短，即使跳过一次误差也很小）
                // osal_error_event_callback(OSAL_MODULE_MOTOR, i, ERR_EVENT_INCORRECT_POSITION, pDrv->runParam.storeActDir);
                LOG_WARN("Enc %s err! dir %d, read pulse = %d, flash pulse = %d", pDrv->com.drvName, pDrv->runParam.storeActDir, pEnc->readPulseValue, pEnc->flashPulseValue);
                //子板读脉冲值在非清零，非溢出情况下会出现读取值为0的情况，除了溢出情况刷新记录值，其它不刷新
                if(pEnc->flashPulseValue > (UINT32_MAX - 100)){ //只留100个脉冲的窗口，避免非溢出情况下读取值为0
                    pEnc->flashPulseValue = pEnc->readPulseValue;
                }
                continue;
            }
            if(pDrv->runParam.storeActDir > 0)      pEnc->encoderValue += encodeDiffValue;
            else if(pDrv->runParam.storeActDir < 0) pEnc->encoderValue -= encodeDiffValue;
            pEnc->flashPulseValue = pEnc->readPulseValue;

            //检测到零位触发脉冲值清零
            if(pEnc->ioIndexZero != INPUT_IO_NULL && osal_is_io_trigger(pEnc->ioIndexZero))   pEnc->status.touchZeroCnt++;
            else pEnc->status.touchZeroCnt = 0;
            if(pEnc->status.touchZeroCnt >= ZERO_CHECK_CNT){
                if(ZERO_CHECK_CNT == pEnc->status.touchZeroCnt)   pEnc->isManualClearEncoder = true;    //首次到零点时清除子板脉冲数
                pEnc->status.touchZeroCnt = 100;
                pEnc->encoderValue = 0;
            }
            //码盘计数异常检测（驱动在运动过程中多次检测码盘值无变化认为异常）
            if(get_diff_ms(pEnc->status.checkTimeStamp) > 400){
                get_time_stamp(&pEnc->status.checkTimeStamp);
                //FIXME 当机构在限位处时，驱动状态在控制动作时会先设置为移动状态，后由驱动控制线程检测到限位转换为非工作状态，
                //如果在限位处频繁驱动某机构，每次进来这个机构状态都是移动状态，导致误判错误
                if(MOTOR_STA_MOVE == pDrv->com.state || MOTOR_STA_MOVE_FORE == pDrv->com.state 
                || MOTOR_STA_MOVE_POS == pDrv->com.state || MOTOR_STA_MOVE_TIME == pDrv->com.state){    //这里只在控制输出时检测， isPulseCounting 停止后还会存在一段时间，但不需要检测秒冲异常了
                    if(pEnc->status.recoderEncValue == pEnc->encoderValue){
                        pEnc->status.errCnt++;
                    }
                    else if(abs(pEnc->status.recoderEncValue - pEnc->encoderValue) > 50){               //短时间脉冲计数变化过大认为异常
                        osal_error_event_callback(OSAL_MODULE_MOTOR, i, ERR_EVENT_INCORRECT_POSITION, pDrv->runParam.storeActDir);
                        LOG_WARN("Enc %s err! dir %d, record pulse = %d, now pulse = %d", pDrv->com.drvName, pDrv->runParam.storeActDir, pEnc->status.recoderEncValue, pEnc->encoderValue);
                    }
                    else{
                        pEnc->status.isErr = false;
                        pEnc->status.errCnt = 0;
                        if(pEnc->status.isErrRecord){
                            pEnc->status.isErrRecord = false;
                            osal_error_event_callback(OSAL_MODULE_MOTOR, i, ERR_EVENT_INCORRECT_POSITION, 0);
                        }
                    }
                }
                else{
                    pEnc->status.errCnt = 0;
                }
                //脉冲变化异常时报警停止
                if(!pEnc->status.isErr && pEnc->status.errCnt > 5){
                    pEnc->status.isErr = true;
                    pEnc->status.isErrRecord = pEnc->status.isErr;
                    osal_error_event_callback(OSAL_MODULE_MOTOR, i, ERR_EVENT_INCORRECT_POSITION, pDrv->runParam.storeActDir);
                    LOG_WARN("%s encoder err, dev state %d, dev pos %d", pDrv->com.drvName, pDrv->com.state, pEnc->encoderValue);
                }
                pEnc->status.recoderEncValue = pEnc->encoderValue;
            }
        }
        usleep(MS_US(50));
        // LOG_INFO(">>>>%lld", get_diff_ms(timeStamp));
    }
}

/*                                                         =======================                                                         */
/* ========================================================       状态机控制       ======================================================== */
/*                                                         =======================                                                         */

pthread_mutex_t drvRun_mutex = PTHREAD_MUTEX_INITIALIZER;

//状态字符查询表
static const char StateStrList[][10] = { "IDLE", "HOLD_ON", "MOV", "FMOV", "POS", "TIME", "PAUSE", "RESUME", "STOP", "FAULT" };
static const char* get_motor_state_str(Type_MoveState_Enum state)
{
    return StateStrList[state];
}

/**
 * @brief       获取驱动的运动状态
 * @param[in]	id
 * @return      int
 */
Type_MoveState_Enum osal_get_motor_move_state(Type_DriverIndex_Enum id)
{
    return Driver_Table[id].com.state;
}

/**
 * @brief       状态机切换
 * @param[in]	pDriver              
 * @param[in]	state               需要切换的状态
 * @return      int                 
 */
static int osal_motor_state_set(Type_DriverInfo_Def* const pDriver, Type_MoveState_Enum state)
{
    Type_MoveState_Enum CurrState = pDriver->com.state;

    if (pDriver->com.state != state) {
        /*状态迁移图*/
        //空闲->错误/速度模式移动/强制速度移动/位置模式移动/时间模式移动/保持
        if (MOTOR_STA_IDLE == pDriver->com.state
        && (MOTOR_STA_FAULT == state || MOTOR_STA_MOVE == state || MOTOR_STA_MOVE_FORE == state || MOTOR_STA_MOVE_POS == state || MOTOR_STA_MOVE_TIME == state || MOTOR_STA_HOLD_ON == state)) {
            pDriver->com.state = state;
        }
        //速度模式移动->空闲/暂停/错误/位置模式移动/强制速度移动/时间模式移动
        else if (MOTOR_STA_MOVE == pDriver->com.state 
        && (MOTOR_STA_IDLE == state || MOTOR_STA_PAUSE == state || MOTOR_STA_FAULT == state || MOTOR_STA_MOVE_FORE == state || MOTOR_STA_MOVE_POS == state || MOTOR_STA_MOVE_TIME == state)) {
            pDriver->com.state = state;
        }
        //强制速度移动->空闲/暂停/错误
        else if (MOTOR_STA_MOVE_FORE == pDriver->com.state 
        && (MOTOR_STA_IDLE == state || MOTOR_STA_PAUSE == state || MOTOR_STA_FAULT == state)) {
            pDriver->com.state = state;
        }
        //位置模式移动->空闲/暂停/错误/速度模式移动/强制速度移动/时间模式移动
        else if (MOTOR_STA_MOVE_POS == pDriver->com.state 
        && (MOTOR_STA_IDLE == state || MOTOR_STA_PAUSE == state || MOTOR_STA_FAULT == state || MOTOR_STA_MOVE == state || MOTOR_STA_MOVE_FORE == state || MOTOR_STA_MOVE_TIME == state)) {
            pDriver->com.state = state;
        }
        //时间模式移动->空闲/暂停/错误/速度模式移动/强制速度移动/位置模式移动
        else if (MOTOR_STA_MOVE_TIME == pDriver->com.state 
        && (MOTOR_STA_IDLE == state || MOTOR_STA_PAUSE == state || MOTOR_STA_FAULT == state || MOTOR_STA_MOVE == state || MOTOR_STA_MOVE_FORE == state || MOTOR_STA_MOVE_POS == state)) {
            pDriver->com.state = state;
        }
        //暂停->恢复/错误
        else if (MOTOR_STA_PAUSE == pDriver->com.state 
        && (MOTOR_STA_RESUME == state || MOTOR_STA_FAULT == state)) {
            pDriver->com.state = state;
        }
        //恢复->错误/速度模式移动/强制速度移动/位置模式移动/时间模式移动
        else if (MOTOR_STA_RESUME == pDriver->com.state 
        && (MOTOR_STA_FAULT == state || MOTOR_STA_MOVE == state || MOTOR_STA_MOVE_FORE == state || MOTOR_STA_MOVE_POS == state || MOTOR_STA_MOVE_TIME == state)) {
            pDriver->com.state = state;
        }
        //保持->空闲
        else if (MOTOR_STA_HOLD_ON == pDriver->com.state && (MOTOR_STA_IDLE == state)) {
            pDriver->com.state = state;
        }
        //停止->空闲/速度模式移动/强制速度移动/位置模式移动/时间模式移动
        else if (MOTOR_STA_STOP == pDriver->com.state 
        && (MOTOR_STA_IDLE == state || MOTOR_STA_MOVE == state || MOTOR_STA_MOVE_FORE == state || MOTOR_STA_MOVE_POS == state || MOTOR_STA_MOVE_TIME == state || MOTOR_STA_HOLD_ON == state)){
            pDriver->com.state = state;
        }
        else if (MOTOR_STA_STOP == state) {	                                        //任意状态都允许到停止
            pDriver->com.state = state;
        }
        else {
            LOG_WARN("%s state switch not allowed, %s -> %s", pDriver->com.drvName, get_motor_state_str(CurrState), get_motor_state_str(state));
            return -1;				            //不允许的状态切换
        }
        if (MOTOR_STA_PAUSE != CurrState) {
            pDriver->com.lastState = CurrState;
        }
        pDriver->com.isStateChange = true;  //状态切换成功
        LOG_DEBUG("%s state switch OK, %s -> %s", pDriver->com.drvName, get_motor_state_str(CurrState), get_motor_state_str(state));
    }
    else {					                    //已是当前状态
        LOG_DEBUG("%s already in %s state", pDriver->com.drvName, get_motor_state_str(CurrState));
    }
    return 0;
}

/**
 * @brief       设备驱动总接口
 * @param[in]	pDriver              
 * @return      int                 
 */
static int dev_driver_run(Type_DriverInfo_Def* const pDriver)
{
    //正/反/停转配置引脚
    if(pDriver->com.ioIndexCW != OUTPUT_IO_NULL)   osal_io_state_change(pDriver->com.ioIndexCW, (pDriver->com.tarVel > 0) ? IO_ENABLE : IO_DISABLE);
    if(pDriver->com.ioIndexCCW != OUTPUT_IO_NULL)  osal_io_state_change(pDriver->com.ioIndexCCW, (pDriver->com.tarVel < 0) ? IO_ENABLE : IO_DISABLE);
    if(pDriver->com.ioIndexStop != OUTPUT_IO_NULL) osal_io_state_change(pDriver->com.ioIndexStop, (0 == pDriver->com.tarVel) ? IO_ENABLE : IO_DISABLE);
    if(pDriver->com.ioIndexVel0 != OUTPUT_IO_NULL) osal_io_state_change(pDriver->com.ioIndexVel0, (2 == abs(pDriver->com.tarVel) || 4 == abs(pDriver->com.tarVel)) ? IO_ENABLE : IO_DISABLE);
    if(pDriver->com.ioIndexVel1 != OUTPUT_IO_NULL) osal_io_state_change(pDriver->com.ioIndexVel1, (3 == abs(pDriver->com.tarVel) || 4 == abs(pDriver->com.tarVel)) ? IO_ENABLE : IO_DISABLE);
    //脉冲触发驱动配置控制变量
    else if(DRIVER_TYPE_PULSE == pDriver->com.drvType){
        pDriver->trigger.isEnable = true;
        if(0 == pDriver->com.tarVel)     pDriver->trigger.ioIndex = pDriver->com.ioIndexStop;
        else if(pDriver->com.tarVel > 0) pDriver->trigger.ioIndex = pDriver->com.ioIndexCW;
        else if(pDriver->com.tarVel < 0) pDriver->trigger.ioIndex = pDriver->com.ioIndexCCW;
    }
    if(0 == pDriver->com.tarVel){
        get_time_stamp(&pDriver->runParam.stopTimeStamp);
        pDriver->isStartDriver = false;
    }
    return 0;
}

typedef enum {
    LIMITE_INACTIVE = 0,
    LIMIT_SIGNAL_POSTIVE,
    LIMIT_SIGNAL_NEGATIVE,
    LIMIT_SIGNAL_PREOTECT_1,
    LIMIT_SIGNAL_PREOTECT_2,
    LIMIT_SIGNAL_PREOTECT_3,
    LIMIT_PULSE_MIN = 11,
    LIMIT_PULSE_MAX,
    LIMIT_CURRENT_MAX = 21,
} Type_LimitTriggerType_Enum;

Type_LimitTriggerType_Enum special_limit_protection(Type_DriverInfo_Def* const pDriver)
{
    // if (pDriver->com.tarVel > 0) {
    //     if(KM_GATE_1 == pDriver->com.drvIndex){
    //         if(osal_is_signal_filter_trigger(SIGNAL_TOO_LONG))          return LIMIT_SIGNAL_PREOTECT_1;
    //         else if(osal_is_io_trigger(BOARD5_INPUT_GEATE_1_RADAR))     return LIMIT_SIGNAL_PREOTECT_2;
    //         // else if(osal_is_signal_filter_trigger(SIGNAL_GROUND))            return LIMIT_SIGNAL_PREOTECT_3;
    //     }
    //     else if(VFD_LIFTER == pDriver->com.drvIndex){
    //         if(osal_is_signal_filter_trigger(SIGNAL_PICKUP_TRUCK))      return LIMIT_SIGNAL_PREOTECT_1;
    //     }
    //     else if(KM_FRONT_LEFT_MOVE == pDriver->com.drvIndex && get_diff_ms(Driver_Table[VFD_FRONT_LEFT_BRUSH].runParam.workTimeStamp) > 1800){
    //         if(VFD_Table[VFD_FRONT_LEFT_BRUSH].commData.loadCurrent > VFD_Table[VFD_FRONT_LEFT_BRUSH].warningCurrent)   return LIMIT_CURRENT_MAX;
    //     }
    //     else if(KM_FRONT_RIGHT_MOVE == pDriver->com.drvIndex && get_diff_ms(Driver_Table[VFD_FRONT_RIGHT_BRUSH].runParam.workTimeStamp) > 1800){
    //         if(VFD_Table[VFD_FRONT_RIGHT_BRUSH].commData.loadCurrent > VFD_Table[VFD_FRONT_RIGHT_BRUSH].warningCurrent) return LIMIT_CURRENT_MAX;
    //     }
    //     else if(KM_BACK_LEFT_MOVE == pDriver->com.drvIndex && get_diff_ms(Driver_Table[VFD_BACK_LEFT_BRUSH].runParam.workTimeStamp) > 1800){
    //         if(VFD_Table[VFD_BACK_LEFT_BRUSH].commData.loadCurrent > VFD_Table[VFD_BACK_LEFT_BRUSH].warningCurrent)     return LIMIT_CURRENT_MAX;
    //     }
    //     else if(KM_BACK_RIGHT_MOVE == pDriver->com.drvIndex && get_diff_ms(Driver_Table[VFD_BACK_RIGHT_BRUSH].runParam.workTimeStamp) > 1800){
    //         if(VFD_Table[VFD_BACK_RIGHT_BRUSH].commData.loadCurrent > VFD_Table[VFD_BACK_RIGHT_BRUSH].warningCurrent)   return LIMIT_CURRENT_MAX;
    //     }
    // }
    // else{}
    // return LIMITE_INACTIVE;
}
/**
 * @brief       判定移动设备是否触发限位
 * @param[in]	pDriver              
 * @return      Type_LimitTriggerType_Enum
 */
static Type_LimitTriggerType_Enum is_dev_limit_trigger(Type_DriverInfo_Def* const pDriver)
{
    if(ACTION_TYPE_HOLD_ON == pDriver->action) return LIMITE_INACTIVE;      //动作保持的驱动无限位
    
    // Type_LimitTriggerType_Enum result = special_limit_protection(pDriver);
    // if(result != LIMITE_INACTIVE)  return result;                           //触发特殊限位

    if(pDriver->com.tarVel > 0){                                            //正向移动时
        if((MODE_LIMIT_PULSE_MAX == pDriver->limitParam.mode || MODE_LIMIT_PULSE_MIN_MAX == pDriver->limitParam.mode)
        && osal_get_dev_pos(pDriver->com.drvIndex) >= pDriver->limitParam.maxPos){
            return LIMIT_PULSE_MAX;                                         //达到脉冲值触发限位
        }
        if(INPUT_IO_NULL == pDriver->limitParam.ioIndexCW){
            return LIMITE_INACTIVE;                                         //无传感器限位的不触发限位
        }
        else{
            return osal_is_io_trigger(pDriver->limitParam.ioIndexCW) ? LIMIT_SIGNAL_POSTIVE : LIMITE_INACTIVE;
        }
    }
    else if(pDriver->com.tarVel < 0){
        if((MODE_LIMIT_PULSE_MIN == pDriver->limitParam.mode || MODE_LIMIT_PULSE_MIN_MAX == pDriver->limitParam.mode)
        && osal_get_dev_pos(pDriver->com.drvIndex) <= pDriver->limitParam.minPos){
            return LIMIT_PULSE_MIN;                                         //达到脉冲值触发限位
        }
        if(INPUT_IO_NULL == pDriver->limitParam.ioIndexCCW){
            return LIMITE_INACTIVE;
        }
        else{
            return osal_is_io_trigger(pDriver->limitParam.ioIndexCCW) ? LIMIT_SIGNAL_NEGATIVE : LIMITE_INACTIVE;
        }
    }
    else{
        if(pDriver->limitParam.ioIndexCW != INPUT_IO_NULL){
            return osal_is_io_trigger(pDriver->limitParam.ioIndexCW) ? LIMIT_SIGNAL_POSTIVE : LIMITE_INACTIVE;
        }
        if(pDriver->limitParam.ioIndexCCW != INPUT_IO_NULL){
            return osal_is_io_trigger(pDriver->limitParam.ioIndexCCW) ? LIMIT_SIGNAL_NEGATIVE : LIMITE_INACTIVE;
        }
    }
    return LIMITE_INACTIVE;
}

#define LIMIT_TRIGGER_FILTER_CNT    (3)

/**
 * @brief       判定移动设备是否触发限位（有一定滤波次数）
 * @param[in]	pDriver
 * @return      Type_LimitTriggerType_Enum
 */
static Type_LimitTriggerType_Enum is_dev_limit_trigger_filter(Type_DriverInfo_Def* const pDriver)
{
    Type_LimitTriggerType_Enum triggerType = is_dev_limit_trigger(pDriver);
    if(triggerType){
        if(pDriver->limitParam.touchedCnt < LIMIT_TRIGGER_FILTER_CNT) pDriver->limitParam.touchedCnt++;
        else return triggerType;
    }
    else{
        pDriver->limitParam.touchedCnt = 0;
    }
    return LIMITE_INACTIVE;
}

static bool isOsalEventTrigger[DRIVER_ALL_NUM] = {0};   //事件是否触发

/**
 * @brief       检查电机驱动是否成功
 * @param[in]	id                  变频器的列表索引
 * @param[in]	vel                 运动方向
 * @return      void                 
 */
static void motor_drive_failed_check(Type_DriverIndex_Enum id, int vel)
{
    Type_SignalType_Enum signal;

    if(VFD_TOP_BRUSH == id)                 signal = SIGNAL_TOP_BRUSH_OVERLOAD;
    else return;                            //其他不检

    if(!isOsalEventTrigger[id] && osal_is_signal_filter_trigger(signal)){
        if(DRIVER_TYPE_VFD == Driver_Table[id].com.drvType)     osal_error_event_callback(OSAL_MODULE_MOTOR, id, ERR_EVENT_DRIVER_FAULT, 1);
        else if(DRIVER_TYPE_KM == Driver_Table[id].com.drvType) osal_error_event_callback(OSAL_MODULE_MOTOR, id, ERR_EVENT_DRIVE_FAILED, 1);
        isOsalEventTrigger[id] = true;
    }
    else if(isOsalEventTrigger[id] && !osal_is_signal_filter_trigger(signal)){
        if(DRIVER_TYPE_VFD == Driver_Table[id].com.drvType)     osal_error_event_callback(OSAL_MODULE_MOTOR, id, ERR_EVENT_DRIVER_FAULT, 0);
        else if(DRIVER_TYPE_KM == Driver_Table[id].com.drvType) osal_error_event_callback(OSAL_MODULE_MOTOR, id, ERR_EVENT_DRIVE_FAILED, 0);
        isOsalEventTrigger[id] = false;
    }
}


/**
 * @brief       驱动控制线程
 * @param[in]	arg
 */
void* osal_driver_run_thread(void* arg)
{
    int       devPos = 0;
    uint64_t  pauseTime;
    struct timespec  pauseStartStamp;

    pthread_mutex_init(&drvRun_mutex, NULL);

    while (1)
    {
        struct timespec drvRun_ts;
        SET_TIMEOUT_MS(drvRun_ts, 100);
        if(0 == pthread_mutex_timedlock(&drvRun_mutex, &drvRun_ts)){
            for (uint8_t i = 0; i < DRIVER_ALL_NUM; i++)
            {
                Type_DriverInfo_Def  *pDrv = &Driver_Table[i];
            
                //开始驱动一定时间后检测驱动是否正常
                if((MOTOR_STA_HOLD_ON == pDrv->com.state
                || MOTOR_STA_MOVE == pDrv->com.state || MOTOR_STA_MOVE_FORE == pDrv->com.state 
                || MOTOR_STA_MOVE_POS == pDrv->com.state || MOTOR_STA_MOVE_TIME == pDrv->com.state)){
                    motor_drive_failed_check(i, pDrv->com.lastVel);
                }

                //脉冲触发的驱动触发时间到后关闭输出
                if(DRIVER_TYPE_PULSE == pDrv->com.drvType && pDrv->trigger.isEnable
                && get_diff_ms(pDrv->runParam.workTimeStamp) > pDrv->trigger.time){
                    pDrv->trigger.isEnable = false;
                    pDrv->isStartDriver = false;
                    osal_io_state_change(pDrv->trigger.ioIndex, IO_DISABLE);
                    continue;
                }
                
                //动作保持类驱动只做速度变更，不做其它判断
                if(ACTION_TYPE_HOLD_ON == pDrv->action){
                    if(pDrv->com.lastVel != pDrv->com.tarVel){
                        if (0 == dev_driver_run(pDrv)) {
                            pDrv->com.lastVel = pDrv->com.tarVel;
                            LOG_INFO("%s velocity change to %d", pDrv->com.drvName, pDrv->com.tarVel);
                        }
                    }
                    //保持类驱动特殊处理，超时后检测下是否到位
                    // if(KM_SIGN_PARK == pDrv->com.drvIndex){
                    //     if(pDrv->com.isStateChange){
                    //         if(get_diff_ms(pDrv->runParam.workTimeStamp) < 15000){
                    //             if(pDrv->com.lastVel > 0){
                    //                 if(!osal_is_signal_filter_trigger(SIGNAL_SIGN_STOP_LEFT_OPEN) && !osal_is_signal_filter_trigger(SIGNAL_SIGN_STOP_RIGHT_OPEN)){
                    //                     pDrv->com.isStateChange = false;
                    //                     osal_error_event_callback(OSAL_MODULE_MOTOR, i, ERR_EVENT_ACTION_TIMEOUT_CW, 0);
                    //                 }
                    //             }
                    //             else{
                    //                 if(osal_is_signal_filter_trigger(SIGNAL_SIGN_STOP_LEFT_OPEN) && osal_is_signal_filter_trigger(SIGNAL_SIGN_STOP_RIGHT_OPEN)){
                    //                     pDrv->com.isStateChange = false;
                    //                     osal_error_event_callback(OSAL_MODULE_MOTOR, i, ERR_EVENT_ACTION_TIMEOUT_CCW, 0);
                    //                 }
                    //             }
                    //         }
                    //         else{
                    //             pDrv->com.isStateChange = false;
                    //             osal_error_event_callback(OSAL_MODULE_MOTOR, i, (pDrv->com.lastVel > 0) ? ERR_EVENT_ACTION_TIMEOUT_CW : ERR_EVENT_ACTION_TIMEOUT_CCW, 1);
                    //         }
                    //     }
                    // }
                    // if(KM_FRONT_SKIRT_MOVE == pDrv->com.drvIndex){
                    //     if(pDrv->com.isStateChange){
                    //         if(get_diff_ms(pDrv->runParam.workTimeStamp) < 10000){
                    //             if(pDrv->com.lastVel > 0){
                    //                 if(!osal_is_signal_filter_trigger(SIGNAL_FL_SKIRT_OPEN) && !osal_is_signal_filter_trigger(SIGNAL_FR_SKIRT_OPEN)){
                    //                     pDrv->com.isStateChange = false;
                    //                     osal_error_event_callback(OSAL_MODULE_MOTOR, i, ERR_EVENT_ACTION_TIMEOUT_CW, 0);
                    //                 }
                    //             }
                    //             else{
                    //                 if(osal_is_signal_filter_trigger(SIGNAL_FL_SKIRT_OPEN) && osal_is_signal_filter_trigger(SIGNAL_FR_SKIRT_OPEN)){
                    //                     pDrv->com.isStateChange = false;
                    //                     osal_error_event_callback(OSAL_MODULE_MOTOR, i, ERR_EVENT_ACTION_TIMEOUT_CCW, 0);
                    //                 }
                    //             }
                    //         }
                    //         else{
                    //             pDrv->com.isStateChange = false;
                    //             osal_error_event_callback(OSAL_MODULE_MOTOR, i, (pDrv->com.lastVel > 0) ? ERR_EVENT_ACTION_TIMEOUT_CW : ERR_EVENT_ACTION_TIMEOUT_CCW, 1);
                    //         }
                    //     }
                    // }
                    continue;
                }

                //驱动结束移动状态一定时间后（机构减速或惯性移动时间）停止脉冲计数
                if(pDrv->runParam.isPulseCounting 
                && pDrv->com.state != MOTOR_STA_MOVE && pDrv->com.state != MOTOR_STA_MOVE_FORE
                && pDrv->com.state != MOTOR_STA_MOVE_POS && pDrv->com.state != MOTOR_STA_MOVE_TIME
                && get_diff_ms(pDrv->runParam.stopTimeStamp) > 1000){
                    pDrv->runParam.isPulseCounting = false;
                }
                
                Type_LimitTriggerType_Enum isLimitTrigger = LIMITE_INACTIVE;
                if((MOTOR_STA_MOVE == pDrv->com.state || MOTOR_STA_MOVE_FORE == pDrv->com.state 
                || MOTOR_STA_MOVE_POS == pDrv->com.state || MOTOR_STA_MOVE_TIME == pDrv->com.state)){
                    isLimitTrigger = is_dev_limit_trigger_filter(pDrv);
                }
                //状态轮询
                if(MOTOR_STA_IDLE == pDrv->com.state){
                    pauseTime = 0;
                }
                else if(MOTOR_STA_MOVE == pDrv->com.state){
                    if (isLimitTrigger || 0 == pDrv->com.tarVel) {
                        if(0 != pDrv->com.tarVel){
                            LOG_INFO("%s -MOVE- touch limit %d, stop move", pDrv->com.drvName, isLimitTrigger);
                            osal_error_event_callback(OSAL_MODULE_MOTOR, i, pDrv->com.tarVel > 0 ? ERR_EVENT_ACTION_TIMEOUT_CW : ERR_EVENT_ACTION_TIMEOUT_CCW, 0);
                        }
                        osal_motor_state_set(pDrv, MOTOR_STA_STOP);         //在速度方向上的限位触发后或设置速度为0进入停止模式
                    }
                    else if (pDrv->runParam.actionOverTime < MOVE_FOREVER 
                            && get_diff_ms(pDrv->runParam.workTimeStamp) > (pDrv->runParam.actionOverTime + pauseTime)) {
                        LOG_INFO("%s -MOVE- over time, stop move", pDrv->com.drvName);
                        osal_motor_state_set(pDrv, MOTOR_STA_STOP);
                        osal_error_event_callback(OSAL_MODULE_MOTOR, i, pDrv->com.lastVel > 0 ? ERR_EVENT_ACTION_TIMEOUT_CW : ERR_EVENT_ACTION_TIMEOUT_CCW, 1);
                    }
                    else if (pDrv->com.lastVel != pDrv->com.tarVel) {       //仅当在命令发生变化后才对驱动器发出命令
                        if (0 == dev_driver_run(pDrv)) {
                            pDrv->com.lastVel = pDrv->com.tarVel;
                            if(pDrv->com.tarVel != 0) pDrv->runParam.storeActDir = pDrv->com.tarVel;
                            LOG_INFO("%s -MOVE- velocity change to %d", pDrv->com.drvName, pDrv->com.tarVel);
                        }
                    }
                }
                else if(MOTOR_STA_MOVE_FORE == pDrv->com.state){
                    if (get_diff_ms(pDrv->runParam.workTimeStamp) > pDrv->runParam.forceMoveTime || 0 == pDrv->com.tarVel) {
                        if(0 != pDrv->com.tarVel){
                            LOG_INFO("%s -FORCE MOVE- time out, stop move", pDrv->com.drvName);
                        }
                        osal_motor_state_set(pDrv, MOTOR_STA_STOP);
                    }
                    else if (get_diff_ms(pDrv->runParam.workTimeStamp) > (pDrv->runParam.actionOverTime + pauseTime)) {
                        LOG_INFO("%s -FORCE MOVE- over time, stop move", pDrv->com.drvName);
                        osal_motor_state_set(pDrv, MOTOR_STA_STOP);
                        osal_error_event_callback(OSAL_MODULE_MOTOR, i, pDrv->com.lastVel > 0 ? ERR_EVENT_ACTION_TIMEOUT_CW : ERR_EVENT_ACTION_TIMEOUT_CCW, 1);
                    }
                    else if (pDrv->com.lastVel != pDrv->com.tarVel) {       //仅当在命令发生变化后才对驱动器发出命令
                        if (0 == dev_driver_run(pDrv)) {
                            pDrv->com.lastVel = pDrv->com.tarVel;
                            if(pDrv->com.tarVel != 0) pDrv->runParam.storeActDir = pDrv->com.tarVel;
                            LOG_INFO("%s -FORCE MOVE- velocity change to %d", pDrv->com.drvName, pDrv->com.tarVel);
                        }
                    }
                }
                else if(MOTOR_STA_MOVE_POS == pDrv->com.state){
                    devPos = osal_get_dev_pos(pDrv->com.drvIndex);
                    if(0 == pDrv->com.tarVel){
                        LOG_INFO("%s -MOVE POS- vel stop", pDrv->com.drvName);
                        osal_motor_state_set(pDrv, MOTOR_STA_STOP);
                    }
                    else if(isLimitTrigger){
                        if((pDrv->com.tarVel > 0 && devPos >= pDrv->runParam.tarPos - MOVE_POS_ERR)
                        || (pDrv->com.tarVel < 0 && devPos <= pDrv->runParam.tarPos + MOVE_POS_ERR)){
                            osal_motor_state_set(pDrv, MOTOR_STA_STOP);
                            LOG_INFO("%s -MOVE POS- touch limit %d", pDrv->com.drvName, isLimitTrigger);
                            osal_error_event_callback(OSAL_MODULE_MOTOR, i, pDrv->com.tarVel > 0 ? ERR_EVENT_ACTION_TIMEOUT_CW : ERR_EVENT_ACTION_TIMEOUT_CCW, 0);
                        }
                    }
                    else if (get_diff_ms(pDrv->runParam.workTimeStamp) > (pDrv->runParam.actionOverTime + pauseTime)) {
                        LOG_INFO("%s -MOVE POS- over time, stop move", pDrv->com.drvName);
                        osal_motor_state_set(pDrv, MOTOR_STA_STOP);
                        osal_error_event_callback(OSAL_MODULE_MOTOR, i, pDrv->com.lastVel > 0 ? ERR_EVENT_ACTION_TIMEOUT_CW : ERR_EVENT_ACTION_TIMEOUT_CCW, 1);
                    }
                    else if (pDrv->com.lastVel != pDrv->com.tarVel) {       //仅当在命令发生变化后才对驱动器发出命令
                        if (0 == dev_driver_run(pDrv)) {
                            pDrv->com.lastVel = pDrv->com.tarVel;
                            if(pDrv->com.tarVel != 0) pDrv->runParam.storeActDir = pDrv->com.tarVel;
                            LOG_INFO("%s -MOVE POS- velocity change to %d", pDrv->com.drvName, pDrv->com.tarVel);
                        }
                    }
                    else {
                        if(pDrv->com.tarVel > 0 && devPos >= pDrv->runParam.tarPos){
                            LOG_DEBUG("%s -MOVE POS- %d done, now pos %d", pDrv->com.drvName, pDrv->runParam.tarPos, devPos);
                            osal_motor_state_set(pDrv, MOTOR_STA_STOP);
                        }
                        else if(pDrv->com.tarVel < 0 && devPos <= pDrv->runParam.tarPos){
                            LOG_DEBUG("%s -MOVE POS- %d done, now pos %d", pDrv->com.drvName, pDrv->runParam.tarPos, devPos);
                            osal_motor_state_set(pDrv, MOTOR_STA_STOP);
                        }
                    }
                }
                else if(MOTOR_STA_MOVE_TIME == pDrv->com.state){
                    if (isLimitTrigger || 0 == pDrv->com.tarVel) {
                        if(0 != pDrv->com.tarVel){
                            LOG_INFO("%s -MOVE TIME- touch limit %d, stop move", pDrv->com.drvName, isLimitTrigger);
                            osal_error_event_callback(OSAL_MODULE_MOTOR, i, pDrv->com.tarVel > 0 ? ERR_EVENT_ACTION_TIMEOUT_CW : ERR_EVENT_ACTION_TIMEOUT_CCW, 0);
                        }
                        osal_motor_state_set(pDrv, MOTOR_STA_STOP);
                    }
                    else if (get_diff_ms(pDrv->runParam.workTimeStamp) > pDrv->runParam.moveTime) {
                        if(0 != pDrv->com.tarVel){
                            LOG_INFO("%s -MOVE TIME- time out, stop move", pDrv->com.drvName);
                        }
                        osal_motor_state_set(pDrv, MOTOR_STA_STOP);
                    }
                    else if (get_diff_ms(pDrv->runParam.workTimeStamp) > (pDrv->runParam.actionOverTime + pauseTime)) {
                        LOG_INFO("%s -MOVE TIME- over time, stop move", pDrv->com.drvName);
                        osal_motor_state_set(pDrv, MOTOR_STA_STOP);
                        osal_error_event_callback(OSAL_MODULE_MOTOR, i, pDrv->com.lastVel > 0 ? ERR_EVENT_ACTION_TIMEOUT_CW : ERR_EVENT_ACTION_TIMEOUT_CCW, 1);
                    }
                    else if (pDrv->com.lastVel != pDrv->com.tarVel) {       //仅当在命令发生变化后才对驱动器发出命令
                        if (0 == dev_driver_run(pDrv)) {
                            pDrv->com.lastVel = pDrv->com.tarVel;
                            if(pDrv->com.tarVel != 0) pDrv->runParam.storeActDir = pDrv->com.tarVel;
                            LOG_INFO("%s -MOVE TIME- velocity change to %d", pDrv->com.drvName, pDrv->com.tarVel);
                        }
                    }
                }
                else if(MOTOR_STA_PAUSE == pDrv->com.state){
                    if (pDrv->com.isStateChange) {
                        pDrv->com.lastVel = pDrv->com.tarVel;
                        pDrv->com.tarVel = 0;
                        if (0 == dev_driver_run(pDrv)) {
                            pDrv->com.isStateChange = false;
                            get_time_stamp(&pauseStartStamp);
                        }
                        else{
                            LOG_WARN("Motor pause failed");
                        }
                    }
                    else {
                        pauseTime = get_diff_ms(pauseStartStamp);
                    }
                }
                else if(MOTOR_STA_RESUME == pDrv->com.state){
                    pDrv->com.tarVel = pDrv->com.lastVel;
                    osal_motor_state_set(pDrv, pDrv->com.lastState);
                }
                else if(MOTOR_STA_FAULT == pDrv->com.state){
                    //
                }
                //切换到stop状态立即执行
                if(MOTOR_STA_STOP == pDrv->com.state){
                    pDrv->com.tarVel = 0;
                    if (0 == dev_driver_run(pDrv)) {
                        pDrv->com.lastVel = 0;
                        osal_motor_state_set(pDrv, MOTOR_STA_IDLE);
                        LOG_DEBUG("%s stop success", pDrv->com.drvName);
                    }
                    else{
                        LOG_DEBUG("%s change stop err", pDrv->com.drvName);
                    }
                }
            }
            pthread_mutex_unlock(&drvRun_mutex);
        }
        usleep(MS_US(10));
    }
}

/*                                                         =======================                                                         */
/* ========================================================    电机驱动控制接口    ======================================================== */
/*                                                         =======================                                                         */

/**
 * @brief       保持类驱动控制
 * @param[in]	id                  驱动索引id
 * @param[in]	vel                 保持方向
 * @return      int
 */
int osal_drive_hold(Type_DriverIndex_Enum id, int vel)
{
    // if(!isConfigMotor[id]){
    //     LOG_WARN("Driver %d no config, exit", id);
    //     return -1;
    // }

    if(!Driver_Table[id].isInit){
        LOG_WARN("Driver id %d is not init", id);
        return -2;
    }

    int ret = -1;
    struct timespec drvRun_ts;
    SET_TIMEOUT_MS(drvRun_ts, 500);
    if(0 == pthread_mutex_timedlock(&drvRun_mutex, &drvRun_ts)){
        if (ACTION_TYPE_HOLD_ON == Driver_Table[id].action) {
            Driver_Table[id].com.tarVel = vel;
            Driver_Table[id].isStartDriver = true;
            get_time_stamp(&Driver_Table[id].runParam.workTimeStamp);
            LOG_INFO("Drive hold %s speed change to %d", Driver_Table[id].com.drvName, Driver_Table[id].com.tarVel);
            ret = osal_motor_state_set(&Driver_Table[id], (0 == Driver_Table[id].com.tarVel) ? MOTOR_STA_IDLE : MOTOR_STA_HOLD_ON);
        }
        pthread_mutex_unlock(&drvRun_mutex);
    }
    return ret;
}

/**
 * @brief       驱动按指定速度移动（有限位保护）
 * @param[in]	id                  驱动驱动索引id
 * @param[in]	vel                 移动速度
 * @return      int                 
 */
int osal_move_run(Type_DriverIndex_Enum id, int vel)
{
    // if(!isConfigMotor[id]){
    //     LOG_WARN("Driver %d no config, exit", id);
    //     return -1;
    // }

    if(!Driver_Table[id].isInit){
        LOG_WARN("Driver id %d is not init", id);
        return -2;
    }

    int ret = -1;
    struct timespec drvRun_ts;
    SET_TIMEOUT_MS(drvRun_ts, 500);
    if(0 == pthread_mutex_timedlock(&drvRun_mutex, &drvRun_ts)){
        if (ACTION_TYPE_MOVE == Driver_Table[id].action) {
            ret = osal_motor_state_set(&Driver_Table[id], MOTOR_STA_MOVE);
            if(0 == ret){
                Driver_Table[id].com.tarVel = vel;
                Driver_Table[id].runParam.isPulseCounting = true;
                Driver_Table[id].isStartDriver = true;
                get_time_stamp(&Driver_Table[id].runParam.workTimeStamp);
                LOG_INFO("Move %s speed change to %d", Driver_Table[id].com.drvName, Driver_Table[id].com.tarVel);
            }
        }
        pthread_mutex_unlock(&drvRun_mutex);
    }
    return ret;
}

/**
 * @brief       强制驱动按指定速度移动一段时间（无限位保护）
 * @param[in]	id                  驱动驱动索引id
 * @param[in]	vel                 移动速度
 * @param[in]	time                移动时间
 * @return      int
 */
int osal_move_force_run(Type_DriverIndex_Enum id, int vel, uint16_t time)
{
    // if(!isConfigMotor[id]){
    //     LOG_WARN("Driver %d no config, exit", id);
    //     return -1;
    // }

    if(!Driver_Table[id].isInit){
        LOG_WARN("Driver id %d is not init", id);
        return -2;
    }

    int ret = -1;
    struct timespec drvRun_ts;
    SET_TIMEOUT_MS(drvRun_ts, 500);
    if(0 == pthread_mutex_timedlock(&drvRun_mutex, &drvRun_ts)){
        if (ACTION_TYPE_MOVE == Driver_Table[id].action) {
            ret = osal_motor_state_set(&Driver_Table[id], MOTOR_STA_MOVE_FORE);
            if(0 == ret){
                Driver_Table[id].com.tarVel = vel;
                Driver_Table[id].runParam.forceMoveTime = time;
                Driver_Table[id].runParam.isPulseCounting = true;
                Driver_Table[id].isStartDriver = true;
                get_time_stamp(&Driver_Table[id].runParam.workTimeStamp);
                LOG_INFO("Force move %s speed change to %d, move time %d", Driver_Table[id].com.drvName, Driver_Table[id].com.tarVel, Driver_Table[id].runParam.forceMoveTime);
            }
        }
        pthread_mutex_unlock(&drvRun_mutex);
    }
    return ret;
}
/**
 * @brief       驱动按指定速度移动到指定位置
 * @param[in]	id                  驱动驱动索引id
 * @param[in]	vel                 移动速度
 * @param[in]	tarPos              移动目标位置
 * @return      int
 */
int osal_move_pos(Type_DriverIndex_Enum id, int vel, int32_t tarPos)
{
    // if(!isConfigMotor[id]){
    //     LOG_WARN("Driver %d no config, exit", id);
    //     return -1;
    // }

    if(!Driver_Table[id].isInit){
        LOG_WARN("Driver id %d is not init", id);
        return -2;
    }

    int32_t nowPos = 0;
    int ret = -1;
    struct timespec drvRun_ts;
    SET_TIMEOUT_MS(drvRun_ts, 500);
    if(0 == pthread_mutex_timedlock(&drvRun_mutex, &drvRun_ts)){
        if (ACTION_TYPE_MOVE == Driver_Table[id].action) {
            ret = osal_motor_state_set(&Driver_Table[id], MOTOR_STA_MOVE_POS);
            if(0 == ret){
                nowPos = osal_get_dev_pos(id);
                if(tarPos > nowPos){
                    Driver_Table[id].com.tarVel = abs(vel);
                }
                else if(tarPos < nowPos){
                    Driver_Table[id].com.tarVel = -abs(vel);
                }
                else{
                    Driver_Table[id].com.tarVel = 0;
                    LOG_INFO("Pos move %s in right pos already", Driver_Table[id].com.drvName);
                }
                Driver_Table[id].runParam.tarPos = tarPos;
                Driver_Table[id].runParam.isPulseCounting = true;
                Driver_Table[id].isStartDriver = true;
                get_time_stamp(&Driver_Table[id].runParam.workTimeStamp);
                //若目标距离与当前距离相差不大，就不移动
                if(abs(tarPos - nowPos) <= MOVE_POS_ERR){
                    Driver_Table[id].com.tarVel = 0;
                    LOG_INFO("%s now pos close tar pos, cancle move", Driver_Table[id].com.drvName);
                }
                else{
                    LOG_INFO("Pos move %s speed change to %d, pos %d to targ pos %d", Driver_Table[id].com.drvName, Driver_Table[id].com.tarVel, nowPos, Driver_Table[id].runParam.tarPos);
                }
            }
        }
        pthread_mutex_unlock(&drvRun_mutex);
    }
    return ret;
}

/**
 * @brief       驱动按指定速度移动一定时间
 * @param[in]	id                  驱动驱动索引id
 * @param[in]	vel                 移动速度
 * @param[in]	time                移动时间
 * @return      int
 */
int osal_move_time(Type_DriverIndex_Enum id, int vel, uint16_t time)
{
    // if(!isConfigMotor[id]){
    //     LOG_WARN("Driver %d no config, exit", id);
    //     return -1;
    // }

    if(!Driver_Table[id].isInit){
        LOG_WARN("Driver id %d is not init", id);
        return -2;
    }

    int ret = -1;
    struct timespec drvRun_ts;
    SET_TIMEOUT_MS(drvRun_ts, 500);
    if(0 == pthread_mutex_timedlock(&drvRun_mutex, &drvRun_ts)){
        if (ACTION_TYPE_MOVE == Driver_Table[id].action) {
            ret = osal_motor_state_set(&Driver_Table[id], MOTOR_STA_MOVE_TIME);
            if(0 == ret){
                Driver_Table[id].com.tarVel = vel;
                Driver_Table[id].runParam.moveTime = time;
                Driver_Table[id].runParam.isPulseCounting = true;
                Driver_Table[id].isStartDriver = true;
                get_time_stamp(&Driver_Table[id].runParam.workTimeStamp);
                LOG_INFO("Time move %s state %d, speed change to %d, move time %d", Driver_Table[id].com.drvName, Driver_Table[id].com.state, Driver_Table[id].com.tarVel, Driver_Table[id].runParam.moveTime);
            }
        }
        pthread_mutex_unlock(&drvRun_mutex);
    }
    return ret;
}

/**
 * @brief       驱动暂停移动
 * @param[in]	id                  驱动驱动索引id
 * @return      int
 */
int osal_move_pause(Type_DriverIndex_Enum id)
{
    // if(!isConfigMotor[id]){
    //     LOG_WARN("Driver %d no config, exit", id);
    //     return -1;
    // }

    if(!Driver_Table[id].isInit){
        LOG_WARN("Driver id %d is not init", id);
        return -2;
    }

    int ret = -1;
    struct timespec drvRun_ts;
    SET_TIMEOUT_MS(drvRun_ts, 500);
    if(0 == pthread_mutex_timedlock(&drvRun_mutex, &drvRun_ts)){
        if (ACTION_TYPE_MOVE == Driver_Table[id].action) {
            LOG_INFO("%s pause", Driver_Table[id].com.drvName);
            ret = osal_motor_state_set(&Driver_Table[id], MOTOR_STA_PAUSE);
        }
        pthread_mutex_unlock(&drvRun_mutex);
    }
    return ret;
}

/**
 * @brief       驱动继续移动
 * @param[in]	id                  驱动驱动索引id
 * @return      int
 */
int osal_move_resume(Type_DriverIndex_Enum id)
{
    // if(!isConfigMotor[id]){
    //     LOG_WARN("Driver %d no config, exit", id);
    //     return -1;
    // }

    if(!Driver_Table[id].isInit){
        LOG_WARN("Driver id %d is not init", id);
        return -2;
    }

    int ret = -1;
    struct timespec drvRun_ts;
    SET_TIMEOUT_MS(drvRun_ts, 500);
    if(0 == pthread_mutex_timedlock(&drvRun_mutex, &drvRun_ts)){
        if (ACTION_TYPE_MOVE == Driver_Table[id].action) {
            LOG_INFO("%s resume", Driver_Table[id].com.drvName);
            ret = osal_motor_state_set(&Driver_Table[id], MOTOR_STA_RESUME);
        }
        pthread_mutex_unlock(&drvRun_mutex);
    }
    return ret;
}

/**
 * @brief       驱动停止移动
 * @param[in]	id                  驱动驱动索引id
 * @return      int
 */
int osal_move_stop(Type_DriverIndex_Enum id)
{
    if(!Driver_Table[id].isInit){
        LOG_WARN("Driver id %d is not init", id);
        return -1;
    }

    int ret = -1;
    struct timespec drvRun_ts;
    SET_TIMEOUT_MS(drvRun_ts, 500);
    if(0 == pthread_mutex_timedlock(&drvRun_mutex, &drvRun_ts)){
        if (ACTION_TYPE_MOVE == Driver_Table[id].action) {
            LOG_INFO("%s stop", Driver_Table[id].com.drvName);
            ret = osal_motor_state_set(&Driver_Table[id], MOTOR_STA_STOP);
        }
        pthread_mutex_unlock(&drvRun_mutex);
    }
    return ret;
}

/*                                                         =======================                                                         */
/* ========================================================        其它接口        ======================================================== */
/*                                                         =======================                                                         */

/**
 * @brief       设置机构的限位模式
 * @param[in]	id                  机构索引
 * @param[in]	mode                限位模式
 * @param[in]	minPos              脉冲限位最小位置
 * @param[in]	maxPos              脉冲限位最大位置
 * @return      int                 
 */
int osal_set_dev_limit_mode(Type_DriverIndex_Enum id, Type_LimitMode_Enum mode, uint16_t minPos, uint16_t maxPos)
{
    if(ACTION_TYPE_MOVE ==Driver_Table[id].action){
        Driver_Table[id].limitParam.mode = mode;
        if(MODE_LIMIT_PULSE_MIN == mode){
            Driver_Table[id].limitParam.minPos = minPos;
            LOG_INFO("%s set soft min limit, minPos %d",Driver_Table[id].com.drvName, minPos);
        }
        else if(MODE_LIMIT_PULSE_MAX == mode){
            Driver_Table[id].limitParam.maxPos = maxPos;
            LOG_INFO("%s set soft max limit, maxPos %d",Driver_Table[id].com.drvName, maxPos);
        }
        else if(MODE_LIMIT_PULSE_MIN_MAX == mode){
            Driver_Table[id].limitParam.minPos = minPos;
            Driver_Table[id].limitParam.maxPos = maxPos;
            LOG_INFO("%s set soft limit, minPos %d, maxPos %d",Driver_Table[id].com.drvName, minPos, maxPos);
        }
        else{
            LOG_INFO("%s set signal limit",Driver_Table[id].com.drvName);
        }
    }
    return -1;
}

void set_side_brush_down_signal_limit(bool value)
{
    // Driver_Table[KM_FRONT_LEFT_MOVE].limitParam.ioIndexCW   = value ? BOARD3_INPUT_FRONT_LEFT_BRUSH_DOWN : INPUT_IO_NULL;
    // Driver_Table[KM_FRONT_RIGHT_MOVE].limitParam.ioIndexCW  = value ? BOARD3_INPUT_FRONT_RIGHT_BRUSH_DOWN : INPUT_IO_NULL;
    // Driver_Table[KM_BACK_LEFT_MOVE].limitParam.ioIndexCW    = value ? BOARD3_INPUT_BACK_LEFT_BRUSH_DOWN : INPUT_IO_NULL;
    // Driver_Table[KM_BACK_RIGHT_MOVE].limitParam.ioIndexCW   = value ? BOARD3_INPUT_BACK_RIGHT_BRUSH_DOWN : INPUT_IO_NULL;
}

void recheck_err_event(Type_OsalModule_Enum module)
{
    switch (module)
    {
    case OSAL_MODULE_DISPLAY:
        /* code */
        break;
    case OSAL_MODULE_VOICE:
        /* code */
        break;
    case OSAL_MODULE_WATER:
        for (uint8_t i = 0; i < WATER_EVENT_NUM; i++)
        {
            isWaterEventTrigger[i] = false;
        }
        break;
    case OSAL_MODULE_MOTOR:
        for (uint8_t i = 0; i < DRIVER_ALL_NUM; i++)
        {
            isOsalEventTrigger[i] = false;
        }
        break;
    
    default:
        break;
    }
}

static void (*offline_payment)(uint8_t washMode);

/**
 * @brief       线下收费机启动回调
 * @param[in]	callback            
 */
void offline_payment_callback_regist(void (*callback)(uint8_t washMode))
{
    offline_payment = callback;
}

/*                                                         =======================                                                         */
/* ========================================================      步进电机控制      ======================================================== */
/*                                                         =======================                                                         */


/**
 * @brief       设置 IO 脉冲输出频率
 * @param[in]   boardId              板卡 ID
 * @param[in]   pin                  IO 引脚号
 * @param[in]   frequency            输出频率，范围 1~1000，单位 Hz，配置为 0 时失能脉冲输出
 * @param[in]   dutyCycle            占空比，范围 1~100
 * @return      int
 */
int io_pluse_frequency_set(int boardId, int pin, int frequency, int dutyCycle)
{
    int data = (frequency << 16) + ((100 - dutyCycle) << 8);
    return io_SDO_write(boardId, 0x200F, pin, &data);
}

#define STEP_MOTOR_DEFAULT_BOARD_ID      (1)
#define STEP_MOTOR_DEFAULT_PIN           (26)
#define STEP_MOTOR_DEFAULT_FREQUENCY     (1000)
#define STEP_MOTOR_MIN_FREQUENCY         (1000)
#define STEP_MOTOR_MAX_FREQUENCY         (10000)
#define STEP_MOTOR_DEFAULT_DUTY_CYCLE    (50)
#define STEP_MOTOR_DEFAULT_DIR_IO        (BOARD1_OUTPUT_TOP_LIFT_DIR)
#define STEP_MOTOR_DEFAULT_BRAKE_IO      (126)
#define STEP_MOTOR_DIR_FORWARD_LEVEL     (IO_ENABLE)
#define STEP_MOTOR_BRAKE_OPEN_LEVEL      (IO_ENABLE)
#define STEP_MOTOR_THREAD_FRE_MS         (10)
#define STEP_MOTOR_BRAKE_DELAY_MS        (100)
#define STEP_MOTOR_DIR_SWITCH_DELAY_MS   (80)

typedef struct
{
    int             currentPos;
    int             targetPos;
    int             frequency;
    int             dir;
    bool            isRunning;
    uint64_t        pulseAcc;
    struct timespec updateTimeStamp;
} Type_StepMotorCtl_Def;

static pthread_mutex_t stepMotor_mutex = PTHREAD_MUTEX_INITIALIZER;
static Type_StepMotorCtl_Def stepMotorCtl = {0};

/**
 * @brief       刷新步进电机当前位置估算值
 * @param[in]   ctl                  步进电机控制对象
 */
static void osal_step_motor_refresh_pos(Type_StepMotorCtl_Def *ctl)
{
    struct timespec now;
    int64_t elapsedUs;
    uint64_t pulseCnt;
    int diffPos;

    if(NULL == ctl || !ctl->isRunning){
        if(NULL != ctl) get_time_stamp(&ctl->updateTimeStamp);
        return;
    }

    get_time_stamp(&now);
    elapsedUs = (int64_t)(now.tv_sec - ctl->updateTimeStamp.tv_sec) * 1000000 + (now.tv_nsec - ctl->updateTimeStamp.tv_nsec) / 1000;
    ctl->updateTimeStamp = now;
    if(elapsedUs <= 0) return;

    ctl->pulseAcc += (uint64_t)elapsedUs * ctl->frequency;
    pulseCnt = ctl->pulseAcc / 1000000;
    ctl->pulseAcc %= 1000000;
    if(0 == pulseCnt) return;

    diffPos = ctl->targetPos - ctl->currentPos;
    if(abs(diffPos) <= (int)pulseCnt){
        ctl->currentPos = ctl->targetPos;
        ctl->pulseAcc = 0;
    }
    else{
        ctl->currentPos += (ctl->dir > 0) ? (int)pulseCnt : -(int)pulseCnt;
    }
}

/**
 * @brief       设置步进电机方向 IO
 * @param[in]   dir                  方向，大于 0 为正向，小于 0 为反向
 */
static void osal_step_motor_set_dir(int dir)
{
    osal_io_state_change(STEP_MOTOR_DEFAULT_DIR_IO, (dir > 0) ? STEP_MOTOR_DIR_FORWARD_LEVEL : !STEP_MOTOR_DIR_FORWARD_LEVEL);
}

/**
 * @brief       设置步进电机抱闸状态
 * @param[in]   isOpen               true 打开抱闸，false 关闭抱闸
 */
static void osal_step_motor_brake_set(bool isOpen)
{
    osal_io_state_change(STEP_MOTOR_DEFAULT_BRAKE_IO, isOpen ? STEP_MOTOR_BRAKE_OPEN_LEVEL : !STEP_MOTOR_BRAKE_OPEN_LEVEL);
}

/**
 * @brief       限制步进电机输出频率范围
 * @param[in]   frequency            输出频率
 * @return      int
 */
static int osal_step_motor_limit_frequency(int frequency)
{
    if(frequency < STEP_MOTOR_MIN_FREQUENCY) return STEP_MOTOR_MIN_FREQUENCY;
    if(frequency > STEP_MOTOR_MAX_FREQUENCY) return STEP_MOTOR_MAX_FREQUENCY;
    return frequency;
}

/**
 * @brief       启停步进电机脉冲输出
 * @param[in]   isEnable             true 使能，false 失能
 * @return      int
 */
static int osal_step_motor_pulse_enable(bool isEnable)
{
    return io_pluse_frequency_set(STEP_MOTOR_DEFAULT_BOARD_ID,
                                  STEP_MOTOR_DEFAULT_PIN,
                                  isEnable ? stepMotorCtl.frequency : 0,
                                  STEP_MOTOR_DEFAULT_DUTY_CYCLE);
}

/**
 * @brief       调试模式下直接控制步进电机脉冲输出
 * @param[in]   dir                  方向，大于 0 为正向，小于 0 为反向
 * @param[in]   frequency            输出频率，配置为 0 时关闭脉冲输出
 * @param[in]   dutyCycle            占空比
 * @return      int
 */
static int osal_step_motor_debug_output_pulse(int dir, int frequency, int dutyCycle)
{
    int ret;

    pthread_mutex_lock(&stepMotor_mutex);
    osal_step_motor_refresh_pos(&stepMotorCtl);
    stepMotorCtl.frequency = osal_step_motor_limit_frequency(frequency);
    stepMotorCtl.targetPos = stepMotorCtl.currentPos;
    stepMotorCtl.isRunning = false;
    stepMotorCtl.dir = 0;
    stepMotorCtl.pulseAcc = 0;
    pthread_mutex_unlock(&stepMotor_mutex);

    /* ret = io_pluse_frequency_set(STEP_MOTOR_DEFAULT_BOARD_ID, STEP_MOTOR_DEFAULT_PIN, 0, dutyCycle);
    if(ret || frequency <= 0){
        osal_step_motor_brake_set(false);
        return ret;
    } */

    // osal_step_motor_brake_set(true);
    // usleep(MS_US(STEP_MOTOR_BRAKE_DELAY_MS));
    osal_step_motor_set_dir(dir);
    // usleep(MS_US(STEP_MOTOR_DIR_SWITCH_DELAY_MS));
    return io_pluse_frequency_set(STEP_MOTOR_DEFAULT_BOARD_ID, STEP_MOTOR_DEFAULT_PIN, frequency, dutyCycle);
}

/**
 * @brief       步进电机位置控制线程
 * @param[in]   arg
 * @return      void*
 */
static void* osal_step_motor_ctl_thread(void* arg)
{
    while (1)
    {
        bool needStart = false;
        bool needStop = false;
        bool needUpdateFrequency = false;
        int frequency = 0;
        int dir = 0;

        pthread_mutex_lock(&stepMotor_mutex);
        if(stepMotorCtl.dir < 0 && osal_is_signal_filter_trigger(SIGNAL_LIFT_UP_LIMIT)){
            stepMotorCtl.dir = 0;
            stepMotorCtl.pulseAcc = 0;
            needStop = true;
            stepMotorCtl.targetPos = 0;
            stepMotorCtl.currentPos = 0;
            LOG_INFO("lifter touch up limit, clear pos");
        }
        else if(stepMotorCtl.dir > 0 && osal_is_signal_filter_trigger(SIGNAL_LIFT_DOWN_LIMIT)){
            stepMotorCtl.dir = 0;
            stepMotorCtl.pulseAcc = 0;
            needStop = true;
            stepMotorCtl.targetPos = stepMotorCtl.currentPos;
            LOG_INFO("lifter touch down limit, current pos %d", stepMotorCtl.targetPos);
        }
        else{
            if(stepMotorCtl.targetPos == -1){
                if(stepMotorCtl.currentPos != -1){
                    needStart = true;
                    dir = -1;
                    stepMotorCtl.dir = -1;
                    stepMotorCtl.currentPos = -1;
                }
            }
            else{
                osal_step_motor_refresh_pos(&stepMotorCtl);

                if(stepMotorCtl.targetPos == stepMotorCtl.currentPos){
                    if(stepMotorCtl.isRunning){
                        stepMotorCtl.isRunning = false;
                        stepMotorCtl.dir = 0;
                        stepMotorCtl.pulseAcc = 0;
                        needStop = true;
                    }
                }
                else{
                    dir = (stepMotorCtl.targetPos > stepMotorCtl.currentPos) ? 1 : -1;
                    if(!stepMotorCtl.isRunning){
                        stepMotorCtl.dir = dir;
                        stepMotorCtl.isRunning = true;
                        stepMotorCtl.pulseAcc = 0;
                        get_time_stamp(&stepMotorCtl.updateTimeStamp);
                        frequency = stepMotorCtl.frequency;
                        needStart = true;
                    }
                    else if(stepMotorCtl.dir != dir){
                        stepMotorCtl.isRunning = false;
                        stepMotorCtl.dir = 0;
                        stepMotorCtl.pulseAcc = 0;
                        needStop = true;
                    }
                    else{
                        frequency = stepMotorCtl.frequency;
                        needUpdateFrequency = true;
                    }
                }
            }
        }
        pthread_mutex_unlock(&stepMotor_mutex);

        if(needStop){
            osal_step_motor_pulse_enable(false);
        }
        if(needStart){
            osal_step_motor_set_dir(dir);
            usleep(MS_US(STEP_MOTOR_DIR_SWITCH_DELAY_MS));
            osal_step_motor_pulse_enable(true);
        }
        else if(needUpdateFrequency){
            io_pluse_frequency_set(STEP_MOTOR_DEFAULT_BOARD_ID, STEP_MOTOR_DEFAULT_PIN, frequency, STEP_MOTOR_DEFAULT_DUTY_CYCLE);
        }

        usleep(MS_US(STEP_MOTOR_THREAD_FRE_MS));
    }

    return NULL;
}

/**
 * @brief       非阻塞设置步进电机目标位置与脉冲频率
 * @param[in]   position             目标位置，单位为脉冲数
 * @param[in]   frequency            输出频率（Hz）；大于 0 时写入并经 osal_step_motor_limit_frequency 限幅；小于等于 0 时不改当前频率（若当前频率无效则置 STEP_MOTOR_DEFAULT_FREQUENCY）
 * @return      int                  固定返回 0
 */
int io_step_motor_move(int position, int frequency)
{
    pthread_mutex_lock(&stepMotor_mutex);
    osal_step_motor_refresh_pos(&stepMotorCtl);
    if(frequency > 0) {
        stepMotorCtl.frequency = osal_step_motor_limit_frequency(frequency);
    }
    else if(stepMotorCtl.frequency <= 0) {
        stepMotorCtl.frequency = STEP_MOTOR_DEFAULT_FREQUENCY;
    }
    stepMotorCtl.targetPos = position;
    pthread_mutex_unlock(&stepMotor_mutex);
    return 0;
}

/**
 * @brief       获取步进电机当前位置估算值
 * @return      int                  当前位置，单位为脉冲数
 */
int io_step_motor_get_pos(void)
{
    int currentPos;

    pthread_mutex_lock(&stepMotor_mutex);
    osal_step_motor_refresh_pos(&stepMotorCtl);
    currentPos = stepMotorCtl.currentPos;
    pthread_mutex_unlock(&stepMotor_mutex);
    return currentPos;
}

/*                                                         =======================                                                         */
/* ========================================================      .c初始化接口      ======================================================== */
/*                                                         =======================                                                         */

static Type_Driver_Def A7Driver = { 0 };

/**
 * @brief
 * @return      int
 */
int osal_init(void)
{
    // osal_load_machine_hardware_config();
    osal_driver_register_init();                    //驱动注册放最前面，大部分IO注册信息均在驱动初始化中配置，其它设备注册IO信息部分依赖于驱动初始化配置
    osal_encoder_register_init();

    //voice config
    A7Driver.voice.init     = osal_voice_init;
    A7Driver.voice.play     = osal_voice_play;
    A7Driver.voice.isInit   = (0 == osal_voice_init()) ? true : false;
    //display config
    A7Driver.screen.init    = osal_display_init;
    A7Driver.screen.display = osal_display_set;
    A7Driver.screen.isInit  = (0 == osal_display_init()) ? true : false;
    //VFD config
    A7Driver.vfd.init       = osal_VFD_init;
    A7Driver.vfd.run        = osal_VFD_run;
    A7Driver.vfd.get_info   = osal_get_VFD_info;
    A7Driver.vfd.set_info   = osal_set_VFD_info;
    A7Driver.vfd.isInit     = (0 == osal_VFD_init()) ? true : false;
    //water system config
    A7Driver.water.init     = osal_water_system_init;
    A7Driver.water.set_cmd  = osal_water_system_control;
    A7Driver.water.get_sta  = osal_is_water_working;
    A7Driver.water.isInit   = (0 == osal_water_system_init()) ? true : false;

    char *buf = (char*)malloc(64 * sizeof(char));
    if(buf){
        sprintf(buf, "osal init ");
        if(A7Driver.voice.isInit)   strcat(buf, "voice ");
        if(A7Driver.screen.isInit)  strcat(buf, "screen ");
        if(A7Driver.vfd.isInit)     strcat(buf, "vfd ");
        if(A7Driver.water.isInit)   strcat(buf, "water");
        LOG_INFO("%s success !", buf);                      //Print success init module
        A7Driver.isInit = true;
        free(buf);
    }

    pthread_t osalEncUpdate_thread, osalSignalTrigger_thread, osalMotorRun_thread, osalStepMotorCtl_thread;
    pthread_t osalIoDataRW_thread;
    pthread_create(&osalEncUpdate_thread,       NULL, osal_encoder_update_thread, NULL);
    pthread_create(&osalSignalTrigger_thread,   NULL, osal_signal_trigger_thread, NULL);
    pthread_create(&osalMotorRun_thread,        NULL, osal_driver_run_thread, NULL);
    pthread_create(&osalIoDataRW_thread,        NULL, osal_io_data_r_w_thread, NULL);
    pthread_create(&osalStepMotorCtl_thread,    NULL, osal_step_motor_ctl_thread, NULL);

    // osal_io_state_change(BOARD2_OUTPUT_SAFE_RELAY_RESET, IO_ENABLE);    //等待io读写线程启动后再使能
    // sleep(1);
    // osal_io_state_change(BOARD2_OUTPUT_SAFE_RELAY_RESET, IO_DISABLE);   //主电源上电后电气自锁，使能1S后断开

    return 0;
}

/**
 * @brief       osal_deinit
 */
void A7_osal_deinit(void) {
    memset(&A7Driver, 0, sizeof(Type_Driver_Def));
}

/**
 * @brief
 * @return      Type_Driver_Def*
 */
Type_Driver_Def* A7_osal_get(void) {
    return &A7Driver;
}


/*                                                         =======================                                                         */
/* ========================================================       debug接口       ======================================================== */
/*                                                         =======================                                                         */

int osal_debug_ctl(char* fun, char* param_1, char* param_2)
{
    if (strcmp(fun, "voice_entry") == 0) {
        osal_voice_play(A7_VOICE_POS_ENTRY, (Type_A7Voice_Enum)atoi(param_1));
        // player_play(atoi(param_1));
    }
    else if (strcmp(fun, "voice_exit") == 0) {
        osal_voice_play(A7_VOICE_POS_EXIT, (Type_A7Voice_Enum)atoi(param_1));
    }
    else if (strcmp(fun, "display") == 0) {
        osal_display_set((Type_A7Display_Enum)atoi(param_1));
    }
    else if (strcmp(fun, "get_vfd_info") == 0) {
        LOG_INFO("Get VFD %d, info %d, value %d", atoi(param_1), atoi(param_2), osal_get_VFD_info(atoi(param_1), (Type_GetVFDInfo_Enum)(atoi(param_2))));
    }
    else if (strcmp(fun, "set_vfd_info") == 0) {
        LOG_INFO("Set VFD %d, info %d, value %d", atoi(param_1), atoi(param_2), osal_set_VFD_info(atoi(param_1), (Type_SetVFDInfo_Enum)(atoi(param_2))));
    }
    else if (strcmp(fun, "io_change") == 0) {
        osal_io_state_change((Type_OutputIo_Enum)atoi(param_1), (bool)atoi(param_2));
    }
    else if (strcmp(fun, "read_io_trig") == 0) {
        osal_is_io_trigger((Type_InputIo_Enum)atoi(param_1));
    }
    else if (strcmp(fun, "read_io") == 0) {
        LOG_INFO("Read board %d, io %d, value %d", atoi(param_1), atoi(param_2), io_read_input_pin_s(atoi(param_1), atoi(param_2)));
    }
    else if (strcmp(fun, "config") == 0) {
        // int ret = osal_set_machine_hardware_config(atoi(param_1), atoi(param_2));
        // LOG_INFO("Set config %d, value %d %s, ret = %d", atoi(param_1), atoi(param_2), (0 == ret) ? "success" : "failed", ret);
    }
    else if (strcmp(fun, "clearEnc") == 0) {
        osal_clear_dev_encoder(atoi(param_1));
    }
    else if (strcmp(fun, "io_test") == 0) {
        ioTestValue[BOARD_ID_RESOLUTION(atoi(param_1))][PIN_ID_RESOLUTION(atoi(param_1))] = atoi(param_2);
        isIoTestEnable[BOARD_ID_RESOLUTION(atoi(param_1))][PIN_ID_RESOLUTION(atoi(param_1))] = (atoi(param_2) != 0 && atoi(param_2) != 1) ? false : true;
        LOG_DEBUG("Test io %d, value %d", atoi(param_1), atoi(param_2));
    }
    else if (strcmp(fun, "step_pos") == 0) {
        int ret = io_step_motor_move(atoi(param_1), atoi(param_2));
        LOG_INFO("Step motor move to pos %d ret %d", atoi(param_1), ret);
    }
    else if (strcmp(fun, "get_adc") == 0) {
        int board = atoi(param_1);
        int id = atoi(param_2);
        LOG_DEBUG("Get adc board %d, io %d, value %d", board, id, io_adc_read(board, id));
    }
    else if (strcmp(fun, "step_pulse") == 0) {
        const char *splitPos = strchr(param_2, '_');
        int frequency = 0;
        int dutyCycle = STEP_MOTOR_DEFAULT_DUTY_CYCLE;
        int ret = -1;
        if (splitPos != NULL) {
            frequency = atoi(param_2);
            dutyCycle = atoi(splitPos + 1);
            ret = osal_step_motor_debug_output_pulse(atoi(param_1), frequency, dutyCycle);
            LOG_INFO("Step motor pulse dir %d, frequency %d, duty %d, ret %d", atoi(param_1), frequency, dutyCycle, ret);
        }
        else {
            LOG_WARN("param_2 format error, expect frequency_dutyCycle: %s", param_2);
        }
    }
    else{
        return -1;
    }
    return 1;
}

