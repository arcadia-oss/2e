/*****************************************************************************
 *
 * Filename:
 * ---------
 *	 IMX134mipi_Sensor.c
 *
 * Project:
 * --------
 *	 ALPS
 *
 * Description:
 * ------------
 *	 Source code of Sensor driver
 *
 *
 *------------------------------------------------------------------------------
 * Upper this line, this part is controlled by CC/CQ. DO NOT MODIFY!!
 *============================================================================
 ****************************************************************************/

#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/atomic.h>
#include <linux/types.h>

#include "kd_camera_typedef.h"
#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"

#include "imx134mipiraw_2lane_Sensor.h"



/****************************Modify following Strings for debug****************************/
//#define PFX "IMX134_camera_sensor"

#define LOG_1 pr_info("IMX134,MIPI 2LANE\n")
#define LOG_2 pr_info("preview 1280*960@30fps,864Mbps/lane; video 1280*960@30fps,864Mbps/lane; capture 5M@30fps,864Mbps/lane\n")
/****************************   Modify end    *******************************************/

//#define pr_info(format, args...)    xlog_printk(ANDROID_pr_infoO   , PFX, "[%s] " format, __FUNCTION__, ##args)

#define MIPI_SETTLEDELAY_AUTO     0
#define MIPI_SETTLEDELAY_MANNUAL  1
#define BIRD_IMX134_HV_MIRROR

static DEFINE_SPINLOCK(imgsensor_drv_lock);

static struct imgsensor_info_struct imgsensor_info = {
    .sensor_id = IMX134_2LANE_SENSOR_ID,

    //.checksum_value = 0x215125a0,
    //.checksum_value = 0x4ff3b7e6,
	.checksum_value = 0x7c580dc5,		//checksum value for Camera Auto Test

    .pre = {
		.pclk = 150000000,
		.linelength = 3600,
		.framelength = 1388,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 1600,
		.grabwindow_height = 1200,
		.mipi_data_lp2hs_settle_dc = 85,//unit , ns
		.max_framerate = 300,
	},
	.cap = {
		.pclk = 150000000,
		.linelength = 3600,
		.framelength = 2422,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 3200,
		.grabwindow_height = 2400,
		.mipi_data_lp2hs_settle_dc = 85,//unit , ns
		.max_framerate = 170,
	},
	.cap1 = {							//capture for PIP 24fps relative information, capture1 mode must use same framelength, linelength with Capture mode for shutter calculate
		.pclk = 150000000,
		.linelength = 3600,
		.framelength = 2422,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 3200,
		.grabwindow_height = 2400,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 170,	//less than 13M(include 13M),cap1 max framerate is 24fps,16M max framerate is 20fps, 20M max framerate is 15fps
	},
	.normal_video = {
		.pclk = 150000000,
		.linelength = 3600,
		.framelength = 1388,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 1600,
		.grabwindow_height = 1200,
		.mipi_data_lp2hs_settle_dc = 85,//unit , ns
		.max_framerate = 300,
	},
	.hs_video = {
		.pclk = 150000000,
		.linelength = 3600,
		.framelength = 1388,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 1600,
		.grabwindow_height = 1200,
		.mipi_data_lp2hs_settle_dc = 85,//unit , ns
		.max_framerate = 300,
	},
	.slim_video = {
		.pclk = 150000000,
		.linelength = 3600,
		.framelength = 1388,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 1600,
		.grabwindow_height = 1200,
		.mipi_data_lp2hs_settle_dc = 85,//unit , ns
		.max_framerate = 300,
	},
	.margin = 8,			//sensor framelength & shutter margin
	.min_shutter = 5,		//min shutter
	.max_frame_length = 0xffff,//max framelength by sensor register's limitation
	.ae_shut_delay_frame = 0,	//shutter delay frame for AE cycle, 2 frame with ispGain_delay-shut_delay=2-0=2
	.ae_sensor_gain_delay_frame = 0,//sensor gain delay frame for AE cycle,2 frame with ispGain_delay-sensor_gain_delay=2-0=2
	.ae_ispGain_delay_frame = 2,//isp gain delay frame for AE cycle
	.ihdr_support = 0,	  //1, support; 0,not support
	.ihdr_le_firstline = 0,  //1,le first ; 0, se first
	.sensor_mode_num = 5,	  //support sensor mode num
	.cap_delay_frame = 3,		//enter capture delay frame num
	.pre_delay_frame = 3, 		//enter preview delay frame num
	.video_delay_frame = 3,		//enter video delay frame num
	.hs_video_delay_frame = 3,	//enter high speed video  delay frame num
	.slim_video_delay_frame = 3,//enter slim video delay frame num
	.isp_driving_current = ISP_DRIVING_6MA, //mclk driving current
	.sensor_interface_type = SENSOR_INTERFACE_TYPE_MIPI,//sensor_interface_type
	.mipi_sensor_type = MIPI_OPHY_NCSI2, //0,MIPI_OPHY_NCSI2;  1,MIPI_OPHY_CSI2
	.mipi_settle_delay_mode = 1,//0,MIPI_SETTLEDELAY_AUTO; 1,MIPI_SETTLEDELAY_MANNUAL
	.sensor_output_dataformat = SENSOR_OUTPUT_FORMAT_RAW_R,//sensor output first pixel color
	.mclk = 24,//mclk value, suggest 24 or 26 for 24Mhz or 26Mhz
	.mipi_lane_num = SENSOR_MIPI_2_LANE,//mipi lane num
	.i2c_addr_table = {0x20, 0x6c, 0x34, 0xff},//record sensor support all write id addr, only supprt 4must end with 0xff
};


static struct imgsensor_struct imgsensor = {
    .mirror = IMAGE_NORMAL,             //mirrorflip information
    .sensor_mode = IMGSENSOR_MODE_INIT, //IMGSENSOR_MODE enum value,record current sensor mode,such as: INIT, Preview, Capture, Video,High Speed Video, Slim Video
    .shutter = 0x09AD,					//current shutter
    .gain = 222,						//current gain
    .dummy_pixel = 0,					//current dummypixel
    .dummy_line = 0,                    //current dummyline
    .current_fps = 300,  //full size current fps : 24fps for PIP, 30fps for Normal or ZSD
    .autoflicker_en = KAL_FALSE,  //auto flicker enable: KAL_FALSE for disable auto flicker, KAL_TRUE for enable auto flicker
    .test_pattern = KAL_FALSE,      //test pattern mode or not. KAL_FALSE for in test pattern mode, KAL_TRUE for normal output
    .current_scenario_id = MSDK_SCENARIO_ID_CAMERA_PREVIEW,//current scenario id
    .ihdr_en = 0, //sensor need support LE, SE with HDR feature
    .i2c_write_id = 0x20,
};


/* Sensor output window information */
static struct SENSOR_WINSIZE_INFO_STRUCT imgsensor_winsize_info[5] =
{{ 3200, 2400, 0, 0, 3200, 2400, 1600, 1200, 0, 0, 1600, 1200, 0, 0, 1600, 1200}, // Preview
 { 3200, 2400, 0,	0, 3200, 2400, 3200, 2400, 0, 0, 3200, 2400, 0, 0, 3200, 2400}, // capture
 { 3200, 2400, 0, 0, 3200, 2400, 1600, 1200, 0, 0, 1600, 1200, 0, 0, 1600, 1200},  // video
 { 3200, 2400, 0, 0, 3200, 2400, 1600, 1200, 0, 0, 1600, 1200, 0, 0, 1600, 1200}, //hight speed video
 { 3200, 2400, 0, 0, 3200, 2400, 1600, 1200, 0, 0, 1600, 1200, 0, 0, 1600, 1200}};// slim video


#define IMX134MIPI_MaxGainIndex (97)
static kal_uint16 sensorGainMapping[IMX134MIPI_MaxGainIndex][2] ={
{ 64 ,0  },   
{ 68 ,12 },   
{ 71 ,23 },   
{ 74 ,33 },   
{ 77 ,42 },   
{ 81 ,52 },   
{ 84 ,59 },   
{ 87 ,66 },   
{ 90 ,73 },   
{ 93 ,79 },   
{ 96 ,85 },   
{ 100,91 },   
{ 103,96 },   
{ 106,101},   
{ 109,105},   
{ 113,110},   
{ 116,114},   
{ 120,118},   
{ 122,121},   
{ 125,125},   
{ 128,128},   
{ 132,131},   
{ 135,134},   
{ 138,137},
{ 141,139},
{ 144,142},   
{ 148,145},   
{ 151,147},   
{ 153,149}, 
{ 157,151},
{ 160,153},      
{ 164,156},   
{ 168,158},   
{ 169,159},   
{ 173,161},   
{ 176,163},   
{ 180,165}, 
{ 182,166},   
{ 187,168},
{ 189,169},
{ 193,171},
{ 196,172},
{ 200,174},
{ 203,175}, 
{ 205,176},
{ 208,177}, 
{ 213,179}, //134==>179 yyf
{ 216,180},  
{ 219,181},   
{ 222,182},
{ 225,183},  
{ 228,184},   
{ 232,185},
{ 235,186},
{ 238,187},
{ 241,188},
{ 245,189},
{ 249,190},
{ 253,191},
{ 256,192}, 
{ 260,193},
{ 265,194},
{ 269,195},
{ 274,196},   
{ 278,197},
{ 283,198},
{ 288,199},
{ 293,200},
{ 298,201},   
{ 304,202},   
{ 310,203},
{ 315,204},
{ 322,205},   
{ 328,206},   
{ 335,207},   
{ 342,208},   
{ 349,209},   
{ 357,210},   
{ 365,211},   
{ 373,212}, 
{ 381,213},
{ 400,215},      
{ 420,217},   
{ 432,218},   
{ 443,219},      
{ 468,221},   
{ 482,222},   
{ 497,223},   
{ 512,224},
{ 529,225}, 	 
{ 546,226},   
{ 566,227},   
{ 585,228}, 	 
{ 607,229},   
{ 631,230},   
{ 656,231},   
{ 683,232}
};

/* #if IMX134_OTP_Enable */
/*
static void write_cmos_sensor_16(kal_uint16 addr, kal_uint16 para)
{
    char pusendcmd[4] = {(char)(addr >> 8) , (char)(addr & 0xFF) ,(char)(para >> 8),(char)(para & 0xFF)};

    //kdSetI2CSpeed(imgsensor_info.i2c_speed); // Add this func to set i2c speed by each sensor
    iWriteRegI2C(pusendcmd , 4, imgsensor.i2c_write_id);
}*/
//#endif

static kal_uint16 read_cmos_sensor(kal_uint32 addr)
{
    kal_uint16 get_byte=0;
    char pu_send_cmd[2] = {(char)(addr >> 8), (char)(addr & 0xFF) };

    //kdSetI2CSpeed(imgsensor_info.i2c_speed); // Add this func to set i2c speed by each sensor
    iReadRegI2C(pu_send_cmd, 2, (u8*)&get_byte, 1, imgsensor.i2c_write_id);
    return get_byte;
}

static void write_cmos_sensor(kal_uint32 addr, kal_uint32 para)
{
    char pu_send_cmd[3] = {(char)(addr >> 8), (char)(addr & 0xFF), (char)(para & 0xFF)};

    //kdSetI2CSpeed(imgsensor_info.i2c_speed); // Add this func to set i2c speed by each sensor
    iWriteRegI2C(pu_send_cmd, 3, imgsensor.i2c_write_id);
}
static void write_cmos_sensor_8(kal_uint16 addr, kal_uint8 para)
{
    char pu_send_cmd[4] = {(char)(addr >> 8), (char)(addr & 0xFF), (char)(para & 0xFF)};
    iWriteRegI2C(pu_send_cmd, 3, imgsensor.i2c_write_id);
}  
static kal_uint16 read_cmos_sensor_8(kal_uint16 addr)
{
    kal_uint16 get_byte=0;
    char pu_send_cmd[2] = {(char)(addr >> 8) , (char)(addr & 0xFF) };

    //kdSetI2CSpeed(imgsensor_info.i2c_speed); // Add this func to set i2c speed by each sensor
    iReadRegI2C(pu_send_cmd , 2, (u8*)&get_byte,1,imgsensor.i2c_write_id);
    return get_byte;
} 
static void set_dummy(void)
{
    pr_info("dummyline = %d, dummypixels = %d \n", imgsensor.dummy_line, imgsensor.dummy_pixel);

    write_cmos_sensor_8(0x0104, 0x01);
    write_cmos_sensor(0x0340, (imgsensor.frame_length >> 8) & 0xFF);
    write_cmos_sensor(0x0341, imgsensor.frame_length & 0xFF);
    write_cmos_sensor(0x0342, (imgsensor.line_length >> 8) & 0xFF);
    write_cmos_sensor(0x0343, imgsensor.line_length & 0xFF);
    write_cmos_sensor_8(0x0104, 0x00);

}   /*  set_dummy  */

static kal_uint32 return_sensor_id(void)
{
    return ((read_cmos_sensor_8(0x0016) << 8) | read_cmos_sensor_8(0x0017));
}

static void set_max_framerate(UINT16 framerate,kal_bool min_framelength_en)
{
    kal_uint32 frame_length = imgsensor.frame_length;
    //unsigned long flags;

    pr_info("framerate = %d, min framelength should enable = %d\n", framerate,min_framelength_en);

    frame_length = imgsensor.pclk / framerate * 10 / imgsensor.line_length;
    spin_lock(&imgsensor_drv_lock);
    imgsensor.frame_length = (frame_length > imgsensor.min_frame_length) ? frame_length : imgsensor.min_frame_length;
    imgsensor.dummy_line = imgsensor.frame_length - imgsensor.min_frame_length;
    //dummy_line = frame_length - imgsensor.min_frame_length;
    //if (dummy_line < 0)
        //imgsensor.dummy_line = 0;
    //else
        //imgsensor.dummy_line = dummy_line;
    //imgsensor.frame_length = frame_length + imgsensor.dummy_line;
    if (imgsensor.frame_length > imgsensor_info.max_frame_length)
    {
        imgsensor.frame_length = imgsensor_info.max_frame_length;
        imgsensor.dummy_line = imgsensor.frame_length - imgsensor.min_frame_length;
    }
    if (min_framelength_en)
        imgsensor.min_frame_length = imgsensor.frame_length;
    spin_unlock(&imgsensor_drv_lock);
}	/*	set_max_framerate  */
static void set_shutter(kal_uint16 shutter)
{
    unsigned long flags;
    kal_uint16 realtime_fps = 0;
    spin_lock_irqsave(&imgsensor_drv_lock, flags);
    imgsensor.shutter = shutter;
    spin_unlock_irqrestore(&imgsensor_drv_lock, flags);
    spin_lock(&imgsensor_drv_lock);
    if (shutter > imgsensor.min_frame_length - imgsensor_info.margin)
        imgsensor.frame_length = shutter + imgsensor_info.margin;
    else
        imgsensor.frame_length = imgsensor.min_frame_length;
    if (imgsensor.frame_length > imgsensor_info.max_frame_length)
        imgsensor.frame_length = imgsensor_info.max_frame_length;
    spin_unlock(&imgsensor_drv_lock);
    shutter = (shutter < imgsensor_info.min_shutter) ? imgsensor_info.min_shutter : shutter;
    shutter = (shutter > (imgsensor_info.max_frame_length - imgsensor_info.margin)) ? (imgsensor_info.max_frame_length - imgsensor_info.margin) : shutter;
    if (imgsensor.autoflicker_en) {
        realtime_fps = imgsensor.pclk / imgsensor.line_length * 10 / imgsensor.frame_length;
        if(realtime_fps >= 297 && realtime_fps <= 305)
		{   set_max_framerate(296,0);}
        else if(realtime_fps >= 147 && realtime_fps <= 150)
        {
		set_max_framerate(146,0);
		} else {
            write_cmos_sensor_8(0x0104, 0x01); 
            write_cmos_sensor(0x0340, (imgsensor.frame_length >> 8) & 0xFF);
            write_cmos_sensor(0x0341, imgsensor.frame_length & 0xFF);
            write_cmos_sensor_8(0x0104, 0x00); 
        }
    } else {
        write_cmos_sensor_8(0x0104, 0x01); 
        write_cmos_sensor(0x0340, (imgsensor.frame_length >> 8) & 0xFF);
        write_cmos_sensor(0x0341, imgsensor.frame_length & 0xFF);
        write_cmos_sensor_8(0x0104, 0x00); 
    }
    write_cmos_sensor_8(0x0104, 0x01); 
    write_cmos_sensor(0x0202, (shutter >> 8) & 0xFF);
    write_cmos_sensor(0x0203, shutter  & 0xFF);	
    write_cmos_sensor_8(0x0104, 0x00); 
    //LOG_INF("Exit! shutter =%d, framelength =%d\n", shutter,imgsensor.frame_length);
}    /*    set_shutter */
static kal_uint16 gain2reg(const kal_uint16 gain)
{
    kal_uint8 iI;	    
    for (iI = 0; iI < (IMX134MIPI_MaxGainIndex-1); iI++) 
    {
        if(gain <= sensorGainMapping[iI][0])
        {    
            break;
        }
    }
/*
    if(gain != sensorGainMapping[iI][0])
    {
         //SENSORDB("Gain mapping don't correctly:%d %d \n", gain, sensorGainMapping[iI][0]);
         return sensorGainMapping[iI][1];
    }
    else return (kal_uint16)gain;
*/
	return sensorGainMapping[iI][1];

}

/*************************************************************************
* FUNCTION
*   set_gain
*
* DESCRIPTION
*   This function is to set global gain to sensor.
*
* PARAMETERS
*   iGain : sensor global gain(base: 0x40)
*
* RETURNS
*   the actually gain set to sensor.
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint16 set_gain(kal_uint16 gain)
{
    kal_uint16 reg_gain;

    /* 0x350A[0:1], 0x350B[0:7] AGC real gain */
    /* [0:3] = N meams N /16 X  */
    /* [4:9] = M meams M X       */
    /* Total gain = M + N /16 X   */

    //
    if (gain < BASEGAIN || gain > 32 * BASEGAIN) {
        pr_info("Error gain setting");

        if (gain < BASEGAIN)
            gain = BASEGAIN;
        else if (gain > 32 * BASEGAIN)
            gain = 32 * BASEGAIN;
    }

    reg_gain = gain2reg(gain);
    spin_lock(&imgsensor_drv_lock);
    imgsensor.gain = reg_gain;
    spin_unlock(&imgsensor_drv_lock);
    pr_info("gain = %d , reg_gain = 0x%x\n ", gain, reg_gain);

    write_cmos_sensor_8(0x0104, 0x01);
    //  LE Gain
    write_cmos_sensor(0x0204, (reg_gain>>8)& 0xFF);
    write_cmos_sensor(0x0205, reg_gain & 0xFF);
    // SE  Gain
    //write_cmos_sensor(0x0233, reg_gain & 0xFF);
    write_cmos_sensor_8(0x0104, 0x00);

    return gain;
}	/*	set_gain  */
static void set_mirror_flip(kal_uint8 image_mirror)
{
    kal_uint8  iTemp; 
    iTemp = read_cmos_sensor(0x0101);
    iTemp&= ~0x03; //Clear the mirror and flip bits.

    switch (image_mirror) {
        case IMAGE_NORMAL:
            write_cmos_sensor_8(0x0101, iTemp);    //Set normal
            break;
        case IMAGE_H_MIRROR:
            write_cmos_sensor_8(0x0101, iTemp | 0x01); //Set mirror
            break;
        case IMAGE_V_MIRROR:
            write_cmos_sensor_8(0x0101, iTemp | 0x02); //Set flip
            break;
        case IMAGE_HV_MIRROR:
            write_cmos_sensor_8(0x0101, iTemp | 0x03); //Set mirror and flip
            break;
        default:
            pr_info("Error image_mirror setting\n");
    }

}
static void night_mode(kal_bool enable)
{
/*No Need to implement this function*/
}   /*  night_mode  */

static void sensor_init(void)
{
write_cmos_sensor(0x0101, 0x00);
  write_cmos_sensor(0x0105, 0x01);
  write_cmos_sensor(0x0110, 0x00);
  write_cmos_sensor(0x0220, 0x01);
  write_cmos_sensor(0x3302, 0x11);
  write_cmos_sensor(0x3833, 0x20);
  write_cmos_sensor(0x3893, 0x00);
  write_cmos_sensor(0x3906, 0x08);
  write_cmos_sensor(0x3907, 0x01);
  write_cmos_sensor(0x391B, 0x01);
  write_cmos_sensor(0x3C09, 0x01);
  write_cmos_sensor(0x600A, 0x00);
  write_cmos_sensor(0x3008, 0xB0);
  write_cmos_sensor(0x320A, 0x01);
  write_cmos_sensor(0x320D, 0x10);
  write_cmos_sensor(0x3216, 0x2E);
  write_cmos_sensor(0x322C, 0x02);
  write_cmos_sensor(0x3409, 0x0C);
  write_cmos_sensor(0x340C, 0x2D);
  write_cmos_sensor(0x3411, 0x39);
  write_cmos_sensor(0x3414, 0x1E);
  write_cmos_sensor(0x3427, 0x04);
  write_cmos_sensor(0x3480, 0x1E);
  write_cmos_sensor(0x3484, 0x1E);
  write_cmos_sensor(0x3488, 0x1E);
  write_cmos_sensor(0x348C, 0x1E);
  write_cmos_sensor(0x3490, 0x1E);
  write_cmos_sensor(0x3494, 0x1E);
  write_cmos_sensor(0x3511, 0x8F);
  write_cmos_sensor(0x3617, 0x2D);
  write_cmos_sensor(0x380A, 0x00);
  write_cmos_sensor(0x380B, 0x00);
  write_cmos_sensor(0x4103, 0x00);
  write_cmos_sensor(0x4243, 0x9A);
  write_cmos_sensor(0x4330, 0x01);
  write_cmos_sensor(0x4331, 0x90);
  write_cmos_sensor(0x4332, 0x02);
  write_cmos_sensor(0x4333, 0x58);
  write_cmos_sensor(0x4334, 0x03);
  write_cmos_sensor(0x4335, 0x20);
  write_cmos_sensor(0x4336, 0x03);
  write_cmos_sensor(0x4337, 0x84);
  write_cmos_sensor(0x433C, 0x01);
  write_cmos_sensor(0x4340, 0x02);
  write_cmos_sensor(0x4341, 0x58);
  write_cmos_sensor(0x4342, 0x03);
  write_cmos_sensor(0x4343, 0x52);
  write_cmos_sensor(0x4364, 0x0B);
  write_cmos_sensor(0x4368, 0x00);
  write_cmos_sensor(0x4369, 0x0F);
  write_cmos_sensor(0x436A, 0x03);
  write_cmos_sensor(0x436B, 0xA8);
  write_cmos_sensor(0x436C, 0x00);
  write_cmos_sensor(0x436D, 0x00);
  write_cmos_sensor(0x436E, 0x00);
  write_cmos_sensor(0x436F, 0x06);
  write_cmos_sensor(0x4281, 0x21);
  write_cmos_sensor(0x4282, 0x18);
  write_cmos_sensor(0x4283, 0x04);
  write_cmos_sensor(0x4284, 0x08);
  write_cmos_sensor(0x4287, 0x7F);
  write_cmos_sensor(0x4288, 0x08);
  write_cmos_sensor(0x428B, 0x7F);
  write_cmos_sensor(0x428C, 0x08);
  write_cmos_sensor(0x428F, 0x7F);
  write_cmos_sensor(0x4297, 0x00);
  write_cmos_sensor(0x4298, 0x7E);
  write_cmos_sensor(0x4299, 0x7E);
  write_cmos_sensor(0x429A, 0x7E);
  write_cmos_sensor(0x42A4, 0xFB);
  write_cmos_sensor(0x42A5, 0x7E);
  write_cmos_sensor(0x42A6, 0xDF);
  write_cmos_sensor(0x42A7, 0xB7);
  write_cmos_sensor(0x42AF, 0x03);
  write_cmos_sensor(0x4207, 0x03);
  write_cmos_sensor(0x4216, 0x08);
  write_cmos_sensor(0x4217, 0x08);
  write_cmos_sensor(0x4218, 0x00);
  write_cmos_sensor(0x421B, 0x20);
  write_cmos_sensor(0x421F, 0x04);
  write_cmos_sensor(0x4222, 0x02);
  write_cmos_sensor(0x4223, 0x22);
  write_cmos_sensor(0x422E, 0x54);
  write_cmos_sensor(0x422F, 0xFB);
  write_cmos_sensor(0x4230, 0xFF);
  write_cmos_sensor(0x4231, 0xFE);
  write_cmos_sensor(0x4232, 0xFF);
  write_cmos_sensor(0x4235, 0x58);
  write_cmos_sensor(0x4236, 0xF7);
  write_cmos_sensor(0x4237, 0xFD);
  write_cmos_sensor(0x4239, 0x4E);
  write_cmos_sensor(0x423A, 0xFC);
  write_cmos_sensor(0x423B, 0xFD);
  write_cmos_sensor(0x4300, 0x00);
  write_cmos_sensor(0x4316, 0x12);
  write_cmos_sensor(0x4317, 0x22);
  write_cmos_sensor(0x4318, 0x00);
  write_cmos_sensor(0x4319, 0x00);
  write_cmos_sensor(0x431A, 0x00);
  write_cmos_sensor(0x4324, 0x03);
  write_cmos_sensor(0x4325, 0x20);
  write_cmos_sensor(0x4326, 0x03);
  write_cmos_sensor(0x4327, 0x84);
  write_cmos_sensor(0x4328, 0x03);
  write_cmos_sensor(0x4329, 0x20);
  write_cmos_sensor(0x432A, 0x03);
  write_cmos_sensor(0x432B, 0x20);
  write_cmos_sensor(0x432C, 0x01);
  write_cmos_sensor(0x432D, 0x01);
  write_cmos_sensor(0x4338, 0x02);
  write_cmos_sensor(0x4339, 0x00);
  write_cmos_sensor(0x433A, 0x00);
  write_cmos_sensor(0x433B, 0x02);
  write_cmos_sensor(0x435A, 0x03);
  write_cmos_sensor(0x435B, 0x84);
  write_cmos_sensor(0x435E, 0x01);
  write_cmos_sensor(0x435F, 0xFF);
  write_cmos_sensor(0x4360, 0x01);
  write_cmos_sensor(0x4361, 0xF4);
  write_cmos_sensor(0x4362, 0x03);
  write_cmos_sensor(0x4363, 0x84);
  write_cmos_sensor(0x437B, 0x01);
  write_cmos_sensor(0x4401, 0x3F);
  write_cmos_sensor(0x4402, 0xFF);
  write_cmos_sensor(0x4404, 0x13);
  write_cmos_sensor(0x4405, 0x26);
  write_cmos_sensor(0x4406, 0x07);
  write_cmos_sensor(0x4408, 0x20);
  write_cmos_sensor(0x4409, 0xE5);
  write_cmos_sensor(0x440A, 0xFB);
  write_cmos_sensor(0x440C, 0xF6);
  write_cmos_sensor(0x440D, 0xEA);
  write_cmos_sensor(0x440E, 0x20);
  write_cmos_sensor(0x4410, 0x00);
  write_cmos_sensor(0x4411, 0x00);
  write_cmos_sensor(0x4412, 0x3F);
  write_cmos_sensor(0x4413, 0xFF);
  write_cmos_sensor(0x4414, 0x1F);
  write_cmos_sensor(0x4415, 0xFF);
  write_cmos_sensor(0x4416, 0x20);
  write_cmos_sensor(0x4417, 0x00);
  write_cmos_sensor(0x4418, 0x1F);
  write_cmos_sensor(0x4419, 0xFF);
  write_cmos_sensor(0x441A, 0x20);
  write_cmos_sensor(0x441B, 0x00);
  write_cmos_sensor(0x441D, 0x40);
  write_cmos_sensor(0x441E, 0x1E);
  write_cmos_sensor(0x441F, 0x38);
  write_cmos_sensor(0x4420, 0x01);
  write_cmos_sensor(0x4444, 0x00);
  write_cmos_sensor(0x4445, 0x00);
  write_cmos_sensor(0x4446, 0x1D);
  write_cmos_sensor(0x4447, 0xF9);
  write_cmos_sensor(0x4452, 0x00);
  write_cmos_sensor(0x4453, 0xA0);
  write_cmos_sensor(0x4454, 0x08);
  write_cmos_sensor(0x4455, 0x00);
  write_cmos_sensor(0x4456, 0x0F);
  write_cmos_sensor(0x4457, 0xFF);
  write_cmos_sensor(0x4458, 0x18);
  write_cmos_sensor(0x4459, 0x18);
  write_cmos_sensor(0x445A, 0x3F);
  write_cmos_sensor(0x445B, 0x3A);
  write_cmos_sensor(0x445C, 0x00);
  write_cmos_sensor(0x445D, 0x28);
  write_cmos_sensor(0x445E, 0x01);
  write_cmos_sensor(0x445F, 0x90);
  write_cmos_sensor(0x4460, 0x00);
  write_cmos_sensor(0x4461, 0x60);
  write_cmos_sensor(0x4462, 0x00);
  write_cmos_sensor(0x4463, 0x00);
  write_cmos_sensor(0x4464, 0x00);
  write_cmos_sensor(0x4465, 0x00);
  write_cmos_sensor(0x446C, 0x00);
  write_cmos_sensor(0x446D, 0x00);
  write_cmos_sensor(0x446E, 0x00);
  write_cmos_sensor(0x452A, 0x02);
  write_cmos_sensor(0x0712, 0x01);
  write_cmos_sensor(0x0713, 0x00);
  write_cmos_sensor(0x0714, 0x01);
  write_cmos_sensor(0x0715, 0x00);
  write_cmos_sensor(0x0716, 0x01);
  write_cmos_sensor(0x0717, 0x00);
  write_cmos_sensor(0x0718, 0x01);
  write_cmos_sensor(0x0719, 0x00);
  write_cmos_sensor(0x4500, 0x1F);
//	LOG_INF("IMX134MIPI_globle_setting  end \n");
}   /*  IMX134MIPI_Sensor_Init  */
static void preview_setting(void)
{
	write_cmos_sensor(0x0100, 0x00);
	mdelay(33);
  write_cmos_sensor(0x011E, 0x18);
  write_cmos_sensor(0x011F, 0x00);
  write_cmos_sensor(0x0301, 0x0A);
  write_cmos_sensor(0x0303, 0x01);
  write_cmos_sensor(0x0305, 0x0C);
  write_cmos_sensor(0x0309, 0x0A);
  write_cmos_sensor(0x030B, 0x02);
  write_cmos_sensor(0x030C, 0x01);
  write_cmos_sensor(0x030D, 0x77);
  write_cmos_sensor(0x030E, 0x01);
  write_cmos_sensor(0x3A06, 0x12);
  write_cmos_sensor(0x0108, 0x01);
  write_cmos_sensor(0x0112, 0x0A);
  write_cmos_sensor(0x0113, 0x0A);
  write_cmos_sensor(0x0381, 0x01);
  write_cmos_sensor(0x0383, 0x01);
  write_cmos_sensor(0x0385, 0x01);
  write_cmos_sensor(0x0387, 0x01);
  write_cmos_sensor(0x0390, 0x01);
  write_cmos_sensor(0x0391, 0x22);
  write_cmos_sensor(0x0392, 0x00);
  write_cmos_sensor(0x0401, 0x00);
  write_cmos_sensor(0x0404, 0x00);
  write_cmos_sensor(0x0405, 0x10);
  write_cmos_sensor(0x4082, 0x01);
  write_cmos_sensor(0x4083, 0x01);
  write_cmos_sensor(0x7006, 0x04);
  write_cmos_sensor(0x0700, 0x00);
  write_cmos_sensor(0x3A63, 0x00);
  write_cmos_sensor(0x4100, 0xF8);
  write_cmos_sensor(0x4203, 0xFF);
  write_cmos_sensor(0x4344, 0x00);
  write_cmos_sensor(0x441C, 0x01);
  write_cmos_sensor(0x0340, 0x05);
  write_cmos_sensor(0x0341, 0x6C);
  write_cmos_sensor(0x0342, 0x0E);
  write_cmos_sensor(0x0343, 0x10);
  write_cmos_sensor(0x0344, 0x00);
  write_cmos_sensor(0x0345, 0x28);
  write_cmos_sensor(0x0346, 0x00);
  write_cmos_sensor(0x0347, 0x20);
  write_cmos_sensor(0x0348, 0x0C);
  write_cmos_sensor(0x0349, 0xA7);
  write_cmos_sensor(0x034A, 0x09);
  write_cmos_sensor(0x034B, 0x7F);
  write_cmos_sensor(0x034C, 0x06);
  write_cmos_sensor(0x034D, 0x40);
  write_cmos_sensor(0x034E, 0x04);
  write_cmos_sensor(0x034F, 0xB0);
  write_cmos_sensor(0x0350, 0x00);
  write_cmos_sensor(0x0351, 0x00);
  write_cmos_sensor(0x0352, 0x00);
  write_cmos_sensor(0x0353, 0x00);
  write_cmos_sensor(0x0354, 0x06);
  write_cmos_sensor(0x0355, 0x40);
  write_cmos_sensor(0x0356, 0x04);
  write_cmos_sensor(0x0357, 0xB0);
  write_cmos_sensor(0x301D, 0x30);
  write_cmos_sensor(0x3310, 0x06);
  write_cmos_sensor(0x3311, 0x40);
  write_cmos_sensor(0x3312, 0x04);
  write_cmos_sensor(0x3313, 0xB0);
  write_cmos_sensor(0x331C, 0x00);
  write_cmos_sensor(0x331D, 0x78);
  write_cmos_sensor(0x4084, 0x00);
  write_cmos_sensor(0x4085, 0x00);
  write_cmos_sensor(0x4086, 0x00);
  write_cmos_sensor(0x4087, 0x00);
  write_cmos_sensor(0x4400, 0x00);
  write_cmos_sensor(0x0830, 0x5F);
  write_cmos_sensor(0x0831, 0x1F);
  write_cmos_sensor(0x0832, 0x3F);
  write_cmos_sensor(0x0833, 0x27);//1F
  write_cmos_sensor(0x0834, 0x1F);
  write_cmos_sensor(0x0835, 0x17);
  write_cmos_sensor(0x0836, 0x67);
  write_cmos_sensor(0x0837, 0x27);
  write_cmos_sensor(0x0839, 0x1F);
  write_cmos_sensor(0x083A, 0x17);
  write_cmos_sensor(0x083B, 0x02);
  write_cmos_sensor(0x0202, 0x05);
  write_cmos_sensor(0x0203, 0x68);
  write_cmos_sensor(0x0205, 0x00);
  write_cmos_sensor(0x020E, 0x01);
  write_cmos_sensor(0x020F, 0x00);
  write_cmos_sensor(0x0210, 0x01);
  write_cmos_sensor(0x0211, 0x00);
  write_cmos_sensor(0x0212, 0x01);
  write_cmos_sensor(0x0213, 0x00);
  write_cmos_sensor(0x0214, 0x01);
  write_cmos_sensor(0x0215, 0x00);
  write_cmos_sensor(0x0230, 0x00);
  write_cmos_sensor(0x0231, 0x00);
  write_cmos_sensor(0x0233, 0x00);
  write_cmos_sensor(0x0234, 0x00);
  write_cmos_sensor(0x0235, 0x40);
  write_cmos_sensor(0x0238, 0x00);
  write_cmos_sensor(0x0239, 0x04);
  write_cmos_sensor(0x023B, 0x00);
  write_cmos_sensor(0x023C, 0x01);
  write_cmos_sensor(0x33B0, 0x04);
  write_cmos_sensor(0x33B1, 0x00);
  write_cmos_sensor(0x33B3, 0x00);
  write_cmos_sensor(0x33B4, 0x01);
  write_cmos_sensor(0x3800, 0x00);
  write_cmos_sensor(0x3A43, 0x01);
  write_cmos_sensor(0x0100, 0x01);
	//LOG_INF("[IMX134MIPI]exit IMX134MIPI_set_8M function\n"); 
} 
static void capture_setting(void)
{
	write_cmos_sensor(0x0100, 0x00);
	mdelay(33);
  write_cmos_sensor(0x011E, 0x18);
  write_cmos_sensor(0x011F, 0x00);
  write_cmos_sensor(0x0301, 0x0A);
  write_cmos_sensor(0x0303, 0x01);
  write_cmos_sensor(0x0305, 0x0C);
  write_cmos_sensor(0x0309, 0x0A);
  write_cmos_sensor(0x030B, 0x01);
  write_cmos_sensor(0x030C, 0x01);
  write_cmos_sensor(0x030D, 0x77);
  write_cmos_sensor(0x030E, 0x01);
  write_cmos_sensor(0x3A06, 0x11);
  write_cmos_sensor(0x0108, 0x01);
  write_cmos_sensor(0x0112, 0x0A);
  write_cmos_sensor(0x0113, 0x0A);
  write_cmos_sensor(0x0381, 0x01);
  write_cmos_sensor(0x0383, 0x01);
  write_cmos_sensor(0x0385, 0x01);
  write_cmos_sensor(0x0387, 0x01);
  write_cmos_sensor(0x0390, 0x00);
  write_cmos_sensor(0x0391, 0x11);
  write_cmos_sensor(0x0392, 0x00);
  write_cmos_sensor(0x0401, 0x00);
  write_cmos_sensor(0x0404, 0x00);
  write_cmos_sensor(0x0405, 0x10);
  write_cmos_sensor(0x4082, 0x01);
  write_cmos_sensor(0x4083, 0x01);
  write_cmos_sensor(0x7006, 0x04);
  write_cmos_sensor(0x0700, 0x00);
  write_cmos_sensor(0x3A63, 0x00);
  write_cmos_sensor(0x4100, 0xF8);
  write_cmos_sensor(0x4203, 0xFF);
  write_cmos_sensor(0x4344, 0x00);
  write_cmos_sensor(0x441C, 0x01);
  write_cmos_sensor(0x0340, 0x09);
  write_cmos_sensor(0x0341, 0x76);
  write_cmos_sensor(0x0342, 0x0E);
  write_cmos_sensor(0x0343, 0x10);
  write_cmos_sensor(0x0344, 0x00);
  write_cmos_sensor(0x0345, 0x28);
  write_cmos_sensor(0x0346, 0x00);
  write_cmos_sensor(0x0347, 0x20);
  write_cmos_sensor(0x0348, 0x0C);
  write_cmos_sensor(0x0349, 0xA7);
  write_cmos_sensor(0x034A, 0x09);
  write_cmos_sensor(0x034B, 0x7F);
  write_cmos_sensor(0x034C, 0x0C);
  write_cmos_sensor(0x034D, 0x80);
  write_cmos_sensor(0x034E, 0x09);
  write_cmos_sensor(0x034F, 0x60);
  write_cmos_sensor(0x0350, 0x00);
  write_cmos_sensor(0x0351, 0x00);
  write_cmos_sensor(0x0352, 0x00);
  write_cmos_sensor(0x0353, 0x00);
  write_cmos_sensor(0x0354, 0x0C);
  write_cmos_sensor(0x0355, 0x80);
  write_cmos_sensor(0x0356, 0x09);
  write_cmos_sensor(0x0357, 0x60);
  write_cmos_sensor(0x301D, 0x30);
  write_cmos_sensor(0x3310, 0x0C);
  write_cmos_sensor(0x3311, 0x80);
  write_cmos_sensor(0x3312, 0x09);
  write_cmos_sensor(0x3313, 0x60);
  write_cmos_sensor(0x331C, 0x01);
  write_cmos_sensor(0x331D, 0xAE);
  write_cmos_sensor(0x4084, 0x00);
  write_cmos_sensor(0x4085, 0x00);
  write_cmos_sensor(0x4086, 0x00);
  write_cmos_sensor(0x4087, 0x00);
  write_cmos_sensor(0x4400, 0x00);
  write_cmos_sensor(0x0830, 0x77);
  write_cmos_sensor(0x0831, 0x2F);
  write_cmos_sensor(0x0832, 0x5F);
  write_cmos_sensor(0x0833, 0x37);
  write_cmos_sensor(0x0834, 0x37);
  write_cmos_sensor(0x0835, 0x37);
  write_cmos_sensor(0x0836, 0xBF);
  write_cmos_sensor(0x0837, 0x3F);
  write_cmos_sensor(0x0839, 0x1F);
  write_cmos_sensor(0x083A, 0x17);
  write_cmos_sensor(0x083B, 0x02);
  write_cmos_sensor(0x0202, 0x09);
  write_cmos_sensor(0x0203, 0x72);
  write_cmos_sensor(0x0205, 0x00);
  write_cmos_sensor(0x020E, 0x01);
  write_cmos_sensor(0x020F, 0x00);
  write_cmos_sensor(0x0210, 0x01);
  write_cmos_sensor(0x0211, 0x00);
  write_cmos_sensor(0x0212, 0x01);
  write_cmos_sensor(0x0213, 0x00);
  write_cmos_sensor(0x0214, 0x01);
  write_cmos_sensor(0x0215, 0x00);
  write_cmos_sensor(0x0230, 0x00);
  write_cmos_sensor(0x0231, 0x00);
  write_cmos_sensor(0x0233, 0x00);
  write_cmos_sensor(0x0234, 0x00);
  write_cmos_sensor(0x0235, 0x40);
  write_cmos_sensor(0x0238, 0x00);
  write_cmos_sensor(0x0239, 0x04);
  write_cmos_sensor(0x023B, 0x00);
  write_cmos_sensor(0x023C, 0x01);
  write_cmos_sensor(0x33B0, 0x04);
  write_cmos_sensor(0x33B1, 0x00);
  write_cmos_sensor(0x33B3, 0x00);
  write_cmos_sensor(0x33B4, 0x01);
  write_cmos_sensor(0x3800, 0x00);
  write_cmos_sensor(0x3A43, 0x01);
  write_cmos_sensor(0x0100, 0x01);	
	//LOG_INF("[IMX134MIPI]exit IMX134MIPI_set_8M function\n"); 
}
static void normal_video_setting(void)
{
  preview_setting();
}
static void hs_video_setting(void)
{
  preview_setting();
}
static void slim_video_setting(void)
{
  preview_setting();
}
static kal_uint32 set_test_pattern_mode(kal_bool enable)
{
    pr_info("enable: %d\n", enable);

    if (enable) {
        // 0x5E00[8]: 1 enable,  0 disable
        // 0x5E00[1:0]; 00 Color bar, 01 Random Data, 10 Square, 11 BLACK
        write_cmos_sensor(0x30D8, 0x10);
        write_cmos_sensor(0x0600, 0x00);
        write_cmos_sensor(0x0601, 0x02);
    } else {
        // 0x5E00[8]: 1 enable,  0 disable
        // 0x5E00[1:0]; 00 Color bar, 01 Random Data, 10 Square, 11 BLACK
        write_cmos_sensor(0x30D8, 0x00);
    }
    spin_lock(&imgsensor_drv_lock);
    imgsensor.test_pattern = enable;
    spin_unlock(&imgsensor_drv_lock);
    return ERROR_NONE;
}


/*************************************************************************
* FUNCTION
*   get_imgsensor_id
*
* DESCRIPTION
*   This function get the sensor ID
*
* PARAMETERS
*   *sensorID : return the sensor ID
*
* RETURNS
*   None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 get_imgsensor_id(UINT32 *sensor_id)
{
    kal_uint8 i = 0;
    kal_uint8 retry = 2;
    //sensor have two i2c address 0x6c 0x6d & 0x21 0x20, we should detect the module used i2c address
    while (imgsensor_info.i2c_addr_table[i] != 0xff) {
        spin_lock(&imgsensor_drv_lock);
        imgsensor.i2c_write_id = imgsensor_info.i2c_addr_table[i];
        spin_unlock(&imgsensor_drv_lock);
        do {
            *sensor_id = return_sensor_id();

	    if(*sensor_id == IMX134_SENSOR_ID)
		*sensor_id = imgsensor_info.sensor_id;

            if (*sensor_id == imgsensor_info.sensor_id) {
//#ifdef CONFIG_MTK_CAM_CAL
//		read_imx134_otp_mtk_fmt();
//#endif
                pr_info("i2c write id: 0x%x, sensor id: 0x%x\n", imgsensor.i2c_write_id,*sensor_id);
                return ERROR_NONE;
            }
            retry--;
        } while(retry > 0);
		pr_info("Read sensor id fail, write id:0x%x id: 0x%x\n", imgsensor.i2c_write_id,*sensor_id);
        i++;
        retry = 2;
    }
    if (*sensor_id != imgsensor_info.sensor_id) {
        // if Sensor ID is not correct, Must set *sensor_id to 0xFFFFFFFF
        *sensor_id = 0xFFFFFFFF;
        return ERROR_SENSOR_CONNECT_FAIL;
    }
    return ERROR_NONE;
}


/*************************************************************************
* FUNCTION
*   open
*
* DESCRIPTION
*   This function initialize the registers of CMOS sensor
*
* PARAMETERS
*   None
*
* RETURNS
*   None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 open(void)
{
    //const kal_uint8 i2c_addr[] = {IMGSENSOR_WRITE_ID_1, IMGSENSOR_WRITE_ID_2};
    kal_uint8 i = 0;
    kal_uint8 retry = 2;
    kal_uint32 sensor_id = 0;
    LOG_1;
    LOG_2;

    //sensor have two i2c address 0x6c 0x6d & 0x21 0x20, we should detect the module used i2c address
    while (imgsensor_info.i2c_addr_table[i] != 0xff) {
        spin_lock(&imgsensor_drv_lock);
        imgsensor.i2c_write_id = imgsensor_info.i2c_addr_table[i];
        spin_unlock(&imgsensor_drv_lock);
        do {
            sensor_id = return_sensor_id();

	    if(sensor_id == IMX134_SENSOR_ID)
		sensor_id = imgsensor_info.sensor_id;

            if (sensor_id == imgsensor_info.sensor_id) {
                pr_info("i2c write id: 0x%x, sensor id: 0x%x\n", imgsensor.i2c_write_id,sensor_id);
                break;
            }
            pr_info("Read sensor id fail, id: 0x%x, sensor id: 0x%x\n", imgsensor.i2c_write_id,sensor_id);
            retry--;
        } while(retry > 0);
        i++;
        if (sensor_id == imgsensor_info.sensor_id)
            break;
        retry = 2;
    }
    if (imgsensor_info.sensor_id != sensor_id)
        return ERROR_SENSOR_CONNECT_FAIL;
    sensor_init();
    imgsensor.gain = 0;
    spin_lock(&imgsensor_drv_lock);
    imgsensor.autoflicker_en= KAL_FALSE;
    imgsensor.sensor_mode = IMGSENSOR_MODE_INIT;
    imgsensor.pclk = imgsensor_info.pre.pclk;
    imgsensor.frame_length = imgsensor_info.pre.framelength;
    imgsensor.line_length = imgsensor_info.pre.linelength;
    imgsensor.min_frame_length = imgsensor_info.pre.framelength;
    imgsensor.dummy_pixel = 0;
    imgsensor.dummy_line = 0;
    imgsensor.ihdr_en = 0; /* KAL_FALSE; */
    imgsensor.test_pattern = KAL_FALSE;
    imgsensor.current_fps = imgsensor_info.pre.max_framerate;
    spin_unlock(&imgsensor_drv_lock);

    return ERROR_NONE;
}   /*  open  */



/*************************************************************************
* FUNCTION
*   close
*
* DESCRIPTION
*
*
* PARAMETERS
*   None
*
* RETURNS
*   None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 close(void)
{


    return ERROR_NONE;
}   /*  close  */


/*************************************************************************
* FUNCTION
* preview
*
* DESCRIPTION
*   This function start the sensor preview.
*
* PARAMETERS
*   *image_window : address pointer of pixel numbers in one period of HSYNC
*  *sensor_config_data : address pointer of line numbers in one period of VSYNC
*
* RETURNS
*   None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 preview(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
    pr_info("E\n");

    spin_lock(&imgsensor_drv_lock);
    imgsensor.sensor_mode = IMGSENSOR_MODE_PREVIEW;
    imgsensor.pclk = imgsensor_info.pre.pclk;
    //imgsensor.video_mode = KAL_FALSE;
    imgsensor.line_length = imgsensor_info.pre.linelength;
    imgsensor.frame_length = imgsensor_info.pre.framelength;
    imgsensor.min_frame_length = imgsensor_info.pre.framelength;
    imgsensor.autoflicker_en = KAL_FALSE;
    spin_unlock(&imgsensor_drv_lock);
    preview_setting();
	set_mirror_flip(imgsensor.mirror);
    pr_info("L\n");
    return ERROR_NONE;
}   /*  preview   */

/*************************************************************************
* FUNCTION
*   capture
*
* DESCRIPTION
*   This function setup the CMOS sensor in capture MY_OUTPUT mode
*
* PARAMETERS
*
* RETURNS
*   None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 capture(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                          MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
    pr_info("E\n");
    spin_lock(&imgsensor_drv_lock);
    imgsensor.sensor_mode = IMGSENSOR_MODE_CAPTURE;
    if (imgsensor.current_fps == imgsensor_info.cap1.max_framerate) {//PIP capture: 24fps for less than 13M, 20fps for 16M,15fps for 20M
        imgsensor.pclk = imgsensor_info.cap1.pclk;
        imgsensor.line_length = imgsensor_info.cap1.linelength;
        imgsensor.frame_length = imgsensor_info.cap1.framelength;
        imgsensor.min_frame_length = imgsensor_info.cap1.framelength;
        imgsensor.autoflicker_en = KAL_FALSE;
    } else {
        //if (imgsensor.current_fps != imgsensor_info.cap.max_framerate)
            //LOG_INF("Warning: current_fps %d fps is not support, so use cap's setting: %d fps!\n",imgsensor.current_fps,imgsensor_info.cap.max_framerate/10);
        imgsensor.pclk = imgsensor_info.cap.pclk;
        imgsensor.line_length = imgsensor_info.cap.linelength;
        imgsensor.frame_length = imgsensor_info.cap.framelength;
        imgsensor.min_frame_length = imgsensor_info.cap.framelength;
        imgsensor.autoflicker_en = KAL_FALSE;
    }
    spin_unlock(&imgsensor_drv_lock);
    capture_setting();
    if(imgsensor.test_pattern == KAL_TRUE)
    {
        set_test_pattern_mode(TRUE);
        spin_lock(&imgsensor_drv_lock);
        imgsensor.test_pattern = KAL_FALSE;
        spin_unlock(&imgsensor_drv_lock);
    }
	capture_setting();
	set_mirror_flip(imgsensor.mirror);
    pr_info("L\n");
    return ERROR_NONE;
}   /* capture() */
static kal_uint32 normal_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
    pr_info("E\n");

    spin_lock(&imgsensor_drv_lock);
    imgsensor.sensor_mode = IMGSENSOR_MODE_VIDEO;
    imgsensor.pclk = imgsensor_info.normal_video.pclk;
    imgsensor.line_length = imgsensor_info.normal_video.linelength;
    imgsensor.frame_length = imgsensor_info.normal_video.framelength;
    imgsensor.min_frame_length = imgsensor_info.normal_video.framelength;
    //imgsensor.current_fps = 300;
    imgsensor.autoflicker_en = KAL_FALSE;
    spin_unlock(&imgsensor_drv_lock);
	normal_video_setting();
	set_mirror_flip(imgsensor.mirror);

    pr_info("L\n");
    return ERROR_NONE;
}   /*  normal_video   */

static kal_uint32 hs_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
    pr_info("E\n");

    spin_lock(&imgsensor_drv_lock);
    imgsensor.sensor_mode = IMGSENSOR_MODE_HIGH_SPEED_VIDEO;
    imgsensor.pclk = imgsensor_info.hs_video.pclk;
    //imgsensor.video_mode = KAL_TRUE;
    imgsensor.line_length = imgsensor_info.hs_video.linelength;
    imgsensor.frame_length = imgsensor_info.hs_video.framelength;
    imgsensor.min_frame_length = imgsensor_info.hs_video.framelength;
    imgsensor.dummy_line = 0;
    imgsensor.dummy_pixel = 0;
    imgsensor.autoflicker_en = KAL_FALSE;
    spin_unlock(&imgsensor_drv_lock);
    hs_video_setting();
	set_mirror_flip(imgsensor.mirror);
    pr_info("L\n");
    return ERROR_NONE;
}   /*  hs_video   */

static kal_uint32 slim_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
    pr_info("E\n");

    spin_lock(&imgsensor_drv_lock);
    imgsensor.sensor_mode = IMGSENSOR_MODE_SLIM_VIDEO;
    imgsensor.pclk = imgsensor_info.slim_video.pclk;
    imgsensor.line_length = imgsensor_info.slim_video.linelength;
    imgsensor.frame_length = imgsensor_info.slim_video.framelength;
    imgsensor.min_frame_length = imgsensor_info.slim_video.framelength;
    imgsensor.dummy_line = 0;
    imgsensor.dummy_pixel = 0;
    imgsensor.autoflicker_en = KAL_FALSE;
    spin_unlock(&imgsensor_drv_lock);
    slim_video_setting();
	set_mirror_flip(imgsensor.mirror);
    //LOG_INF("L\n");
	return ERROR_NONE;
}   /*  slim_video   */

static kal_uint32 get_resolution(MSDK_SENSOR_RESOLUTION_INFO_STRUCT(*sensor_resolution))
{
    pr_info("E\n");

    pr_info("imgsensor_info.cap.grabwindow_width: %d\n", imgsensor_info.cap.grabwindow_width);
    sensor_resolution->SensorFullWidth = imgsensor_info.cap.grabwindow_width;
    sensor_resolution->SensorFullHeight = imgsensor_info.cap.grabwindow_height;

    sensor_resolution->SensorPreviewWidth = imgsensor_info.pre.grabwindow_width;
    sensor_resolution->SensorPreviewHeight = imgsensor_info.pre.grabwindow_height;

    sensor_resolution->SensorVideoWidth = imgsensor_info.normal_video.grabwindow_width;
    sensor_resolution->SensorVideoHeight = imgsensor_info.normal_video.grabwindow_height;


    sensor_resolution->SensorHighSpeedVideoWidth     = imgsensor_info.hs_video.grabwindow_width;
    sensor_resolution->SensorHighSpeedVideoHeight    = imgsensor_info.hs_video.grabwindow_height;

    sensor_resolution->SensorSlimVideoWidth  = imgsensor_info.slim_video.grabwindow_width;
    sensor_resolution->SensorSlimVideoHeight     = imgsensor_info.slim_video.grabwindow_height;
    pr_info("L\n");
    return ERROR_NONE;
}   /*  get_resolution  */

static kal_uint32 get_info(enum MSDK_SCENARIO_ID_ENUM scenario_id,
                      MSDK_SENSOR_INFO_STRUCT *sensor_info,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
    pr_info("scenario_id = %d\n", scenario_id);

    sensor_info->SensorClockPolarity = SENSOR_CLOCK_POLARITY_LOW;
    sensor_info->SensorClockFallingPolarity = SENSOR_CLOCK_POLARITY_LOW; /* not use */
    sensor_info->SensorHsyncPolarity = SENSOR_CLOCK_POLARITY_LOW; // inverse with datasheet
    sensor_info->SensorVsyncPolarity = SENSOR_CLOCK_POLARITY_LOW;
    sensor_info->SensorInterruptDelayLines = 1; /* not use */
    sensor_info->SensorResetActiveHigh = FALSE; /* not use */
    sensor_info->SensorResetDelayCount = 5; /* not use */

    sensor_info->SensroInterfaceType = imgsensor_info.sensor_interface_type;
    sensor_info->MIPIsensorType = imgsensor_info.mipi_sensor_type;
    sensor_info->SettleDelayMode = imgsensor_info.mipi_settle_delay_mode;
    sensor_info->SensorOutputDataFormat = imgsensor_info.sensor_output_dataformat;

    sensor_info->CaptureDelayFrame = imgsensor_info.cap_delay_frame;
    sensor_info->PreviewDelayFrame = imgsensor_info.pre_delay_frame;
    sensor_info->VideoDelayFrame = imgsensor_info.video_delay_frame;
    sensor_info->HighSpeedVideoDelayFrame = imgsensor_info.hs_video_delay_frame;
    sensor_info->SlimVideoDelayFrame = imgsensor_info.slim_video_delay_frame;
    sensor_info->SensorMasterClockSwitch = 0; /* not use */
    sensor_info->SensorDrivingCurrent = imgsensor_info.isp_driving_current;

    sensor_info->AEShutDelayFrame = imgsensor_info.ae_shut_delay_frame;          /* The frame of setting shutter default 0 for TG int */
    sensor_info->AESensorGainDelayFrame = imgsensor_info.ae_sensor_gain_delay_frame;    /* The frame of setting sensor gain */
    sensor_info->AEISPGainDelayFrame = imgsensor_info.ae_ispGain_delay_frame;
    sensor_info->IHDR_Support = imgsensor_info.ihdr_support;
    sensor_info->IHDR_LE_FirstLine = imgsensor_info.ihdr_le_firstline;
    sensor_info->SensorModeNum = imgsensor_info.sensor_mode_num;

    sensor_info->SensorMIPILaneNumber = imgsensor_info.mipi_lane_num;
    sensor_info->SensorClockFreq = imgsensor_info.mclk;
    sensor_info->SensorClockDividCount = 5; /* not use */
    sensor_info->SensorClockRisingCount = 0;
    sensor_info->SensorClockFallingCount = 2; /* not use */
    sensor_info->SensorPixelClockCount = 3; /* not use */
    sensor_info->SensorDataLatchCount = 2; /* not use */

    sensor_info->MIPIDataLowPwr2HighSpeedTermDelayCount = 0;
    sensor_info->MIPICLKLowPwr2HighSpeedTermDelayCount = 0;
    sensor_info->SensorWidthSampling = 0;  /* 0 is default 1x */
    sensor_info->SensorHightSampling = 0;   /* 0 is default 1x */
    sensor_info->SensorPacketECCOrder = 1;
    switch (scenario_id) {
        case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
            sensor_info->SensorGrabStartX = imgsensor_info.pre.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.pre.starty;
            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.pre.mipi_data_lp2hs_settle_dc;
            break;
        case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
            sensor_info->SensorGrabStartX = imgsensor_info.cap.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.cap.starty;
            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.cap.mipi_data_lp2hs_settle_dc; 
            break;	 
        case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
            sensor_info->SensorGrabStartX = imgsensor_info.normal_video.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.normal_video.starty;
            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.normal_video.mipi_data_lp2hs_settle_dc; 
            break;	  
        case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:			
            sensor_info->SensorGrabStartX = imgsensor_info.hs_video.startx; 
            sensor_info->SensorGrabStartY = imgsensor_info.hs_video.starty;
            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.hs_video.mipi_data_lp2hs_settle_dc; 
            break;
        case MSDK_SCENARIO_ID_SLIM_VIDEO:
            sensor_info->SensorGrabStartX = imgsensor_info.slim_video.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.slim_video.starty;
            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.slim_video.mipi_data_lp2hs_settle_dc; 
            break;
        default:			
            sensor_info->SensorGrabStartX = imgsensor_info.pre.startx; 
            sensor_info->SensorGrabStartY = imgsensor_info.pre.starty;		
            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.pre.mipi_data_lp2hs_settle_dc;
            break;
    }

    return ERROR_NONE;
}   /*  get_info  */

static kal_uint32 control(enum MSDK_SCENARIO_ID_ENUM scenario_id, MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
    pr_info("scenario_id = %d\n", scenario_id);
    spin_lock(&imgsensor_drv_lock);
    imgsensor.current_scenario_id = scenario_id;
    spin_unlock(&imgsensor_drv_lock);
    switch (scenario_id) {
        case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
            preview(image_window, sensor_config_data);
            break;
        case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
            capture(image_window, sensor_config_data);
            break;
        case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
            normal_video(image_window, sensor_config_data);  // VideoFullSizeSetting
            break;
        case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
            hs_video(image_window, sensor_config_data);  // VideoHDSetting_120fps
            break;
        case MSDK_SCENARIO_ID_SLIM_VIDEO:
            slim_video(image_window, sensor_config_data);
            break;	  
        default:
            pr_info("Error ScenarioId setting");
            preview(image_window, sensor_config_data);
            return ERROR_INVALID_SCENARIO_ID;
    }
    return ERROR_NONE;
}   /* control() */



static kal_uint32 set_video_mode(UINT16 framerate)
{
    pr_info("framerate = %d\n ", framerate);
    // SetVideoMode Function should fix framerate
    if (framerate == 0)
        // Dynamic frame rate
        return ERROR_NONE;
    spin_lock(&imgsensor_drv_lock);
    if ((framerate == 300) && (imgsensor.autoflicker_en == KAL_TRUE))
        imgsensor.current_fps = 296;
    else if ((framerate == 150) && (imgsensor.autoflicker_en == KAL_TRUE))
        imgsensor.current_fps = 146;
    else
        imgsensor.current_fps = framerate;
    spin_unlock(&imgsensor_drv_lock);
    set_max_framerate(imgsensor.current_fps,1);
    return ERROR_NONE;
}

static kal_uint32 set_auto_flicker_mode(kal_bool enable, UINT16 framerate)
{
    pr_info("enable = %d, framerate = %d \n", enable, framerate);
    spin_lock(&imgsensor_drv_lock);
    if (enable) //enable auto flicker
        imgsensor.autoflicker_en = KAL_TRUE;
    else //Cancel Auto flick
        imgsensor.autoflicker_en = KAL_FALSE;
    spin_unlock(&imgsensor_drv_lock);
    return ERROR_NONE;
}


static kal_uint32 set_max_framerate_by_scenario(enum MSDK_SCENARIO_ID_ENUM scenario_id, MUINT32 framerate)
{
    kal_uint32 frame_length;

    pr_info("scenario_id = %d, framerate = %d\n", scenario_id, framerate);

    switch (scenario_id) {
        case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
            frame_length = imgsensor_info.pre.pclk / framerate * 10 / imgsensor_info.pre.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.pre.framelength) ? (frame_length - imgsensor_info.pre.framelength) : 0;
            imgsensor.frame_length = imgsensor_info.pre.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            set_dummy();			
            break;			
        case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
            if(framerate == 0)
                return ERROR_NONE;
            frame_length = imgsensor_info.normal_video.pclk / framerate * 10 / imgsensor_info.normal_video.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.normal_video.framelength) ? (frame_length - imgsensor_info.normal_video.framelength) : 0;
            imgsensor.frame_length = imgsensor_info.normal_video.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            set_dummy();			
            break;
        case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
            if (imgsensor.current_fps == imgsensor_info.cap1.max_framerate) {
                frame_length = imgsensor_info.cap1.pclk / framerate * 10 / imgsensor_info.cap1.linelength;
                spin_lock(&imgsensor_drv_lock);
                imgsensor.dummy_line = (frame_length > imgsensor_info.cap1.framelength) ? (frame_length - imgsensor_info.cap1.framelength) : 0;
                imgsensor.frame_length = imgsensor_info.cap1.framelength + imgsensor.dummy_line;
                imgsensor.min_frame_length = imgsensor.frame_length;
                spin_unlock(&imgsensor_drv_lock);
            } else {
                //if (imgsensor.current_fps != imgsensor_info.cap.max_framerate)
                  //  LOG_INF("Warning: current_fps %d fps is not support, so use cap's setting: %d fps!\n",framerate,imgsensor_info.cap.max_framerate/10);
                frame_length = imgsensor_info.cap.pclk / framerate * 10 / imgsensor_info.cap.linelength;
                spin_lock(&imgsensor_drv_lock);
                imgsensor.dummy_line = (frame_length > imgsensor_info.cap.framelength) ? (frame_length - imgsensor_info.cap.framelength) : 0;
                imgsensor.frame_length = imgsensor_info.cap.framelength + imgsensor.dummy_line;
                imgsensor.min_frame_length = imgsensor.frame_length;
                spin_unlock(&imgsensor_drv_lock);
            }
            set_dummy();			
            break;	
        case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
            frame_length = imgsensor_info.hs_video.pclk / framerate * 10 / imgsensor_info.hs_video.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.hs_video.framelength) ? (frame_length - imgsensor_info.hs_video.framelength) : 0;
            imgsensor.frame_length = imgsensor_info.hs_video.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            set_dummy();			
            break;
        case MSDK_SCENARIO_ID_SLIM_VIDEO:
            frame_length = imgsensor_info.slim_video.pclk / framerate * 10 / imgsensor_info.slim_video.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.slim_video.framelength) ? (frame_length - imgsensor_info.slim_video.framelength): 0;
            imgsensor.frame_length = imgsensor_info.slim_video.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            set_dummy();			
            break;		
        default:  //coding with  preview scenario by default
            frame_length = imgsensor_info.pre.pclk / framerate * 10 / imgsensor_info.pre.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.pre.framelength) ? (frame_length - imgsensor_info.pre.framelength) : 0;
            imgsensor.frame_length = imgsensor_info.pre.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            set_dummy();	
            pr_info("error scenario_id = %d, we use preview scenario \n", scenario_id);
            break;
    }
    return ERROR_NONE;
}


static kal_uint32 get_default_framerate_by_scenario(enum MSDK_SCENARIO_ID_ENUM scenario_id, MUINT32 *framerate)
{
    pr_info("scenario_id = %d\n", scenario_id);

    switch (scenario_id) {
        case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
            *framerate = imgsensor_info.pre.max_framerate;
            break;
        case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
            *framerate = imgsensor_info.normal_video.max_framerate;
            break;
        case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
            *framerate = imgsensor_info.cap.max_framerate;
            break;
        case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
            *framerate = imgsensor_info.hs_video.max_framerate;
            break;
        case MSDK_SCENARIO_ID_SLIM_VIDEO:
            *framerate = imgsensor_info.slim_video.max_framerate;
            break;
        default:
            break;
    }

    return ERROR_NONE;
}

static kal_uint32 feature_control(MSDK_SENSOR_FEATURE_ENUM feature_id,
                             UINT8 *feature_para,UINT32 *feature_para_len)
{
    UINT16 *feature_return_para_16=(UINT16 *) feature_para;
    UINT16 *feature_data_16=(UINT16 *) feature_para;
    UINT32 *feature_return_para_32=(UINT32 *) feature_para;
    UINT32 *feature_data_32=(UINT32 *) feature_para;
    unsigned long long *feature_data=(unsigned long long *) feature_para;
    //unsigned long long *feature_return_para=(unsigned long long *) feature_para;

    struct SENSOR_WINSIZE_INFO_STRUCT *wininfo;
    MSDK_SENSOR_REG_INFO_STRUCT *sensor_reg_data=(MSDK_SENSOR_REG_INFO_STRUCT *) feature_para;

    //pr_info("feature_id = %d\n", feature_id);
    switch (feature_id) {
        case SENSOR_FEATURE_GET_PERIOD:
            *feature_return_para_16++ = imgsensor.line_length;
            *feature_return_para_16 = imgsensor.frame_length;
            *feature_para_len=4;
            break;
        case SENSOR_FEATURE_GET_PIXEL_CLOCK_FREQ:
            //pr_info("feature_Control imgsensor.pclk = %d,imgsensor.current_fps = %d\n", imgsensor.pclk,imgsensor.current_fps);
            *feature_return_para_32 = imgsensor.pclk;
            *feature_para_len=4;
            break;
        case SENSOR_FEATURE_SET_ESHUTTER:
            set_shutter((kal_uint16)*feature_data);
            break;
        case SENSOR_FEATURE_SET_NIGHTMODE:
            night_mode((kal_bool) *feature_data);
            break;
        case SENSOR_FEATURE_SET_GAIN:
            set_gain((UINT16) *feature_data);
            break;
        case SENSOR_FEATURE_SET_FLASHLIGHT:
            break;
        case SENSOR_FEATURE_SET_ISP_MASTER_CLOCK_FREQ:
            break;
        case SENSOR_FEATURE_SET_REGISTER:
            write_cmos_sensor(sensor_reg_data->RegAddr, sensor_reg_data->RegData);
            break;
        case SENSOR_FEATURE_GET_REGISTER:
            sensor_reg_data->RegData = read_cmos_sensor(sensor_reg_data->RegAddr);
            break;
        case SENSOR_FEATURE_GET_LENS_DRIVER_ID:
            // get the lens driver ID from EEPROM or just return LENS_DRIVER_ID_DO_NOT_CARE
            // if EEPROM does not exist in camera module.
            *feature_return_para_32=LENS_DRIVER_ID_DO_NOT_CARE;
            *feature_para_len=4;
            break;
        case SENSOR_FEATURE_SET_VIDEO_MODE:
            set_video_mode((UINT16)*feature_data);
            break;
        case SENSOR_FEATURE_CHECK_SENSOR_ID:
            get_imgsensor_id((UINT32 *)feature_return_para_32);
            break;
        case SENSOR_FEATURE_SET_AUTO_FLICKER_MODE:
            set_auto_flicker_mode((BOOL)*feature_data_16,(UINT16)*(feature_data_16+1));
            break;
        case SENSOR_FEATURE_SET_MAX_FRAME_RATE_BY_SCENARIO:
            set_max_framerate_by_scenario((enum MSDK_SCENARIO_ID_ENUM)*feature_data, (MUINT32)*(feature_data+1));
            break;
        case SENSOR_FEATURE_GET_DEFAULT_FRAME_RATE_BY_SCENARIO:
            get_default_framerate_by_scenario((enum MSDK_SCENARIO_ID_ENUM)*(feature_data), (MUINT32 *)(uintptr_t)(*(feature_data+1)));
            break;
        case SENSOR_FEATURE_SET_TEST_PATTERN:
            set_test_pattern_mode((BOOL)*feature_data);
            break;
        case SENSOR_FEATURE_GET_TEST_PATTERN_CHECKSUM_VALUE: //for factory mode auto testing
            *feature_return_para_32 = imgsensor_info.checksum_value;
            *feature_para_len=4;
            break;
        case SENSOR_FEATURE_SET_FRAMERATE:
            //pr_info("current fps :%d\n", (UINT32)*feature_data);
            spin_lock(&imgsensor_drv_lock);
            imgsensor.current_fps = (UINT16)*feature_data;
            spin_unlock(&imgsensor_drv_lock);
            break;
        case SENSOR_FEATURE_SET_HDR:
            //pr_info("ihdr enable :%d\n", (BOOL)*feature_data);
            spin_lock(&imgsensor_drv_lock);
            imgsensor.ihdr_en = (BOOL)*feature_data;
            spin_unlock(&imgsensor_drv_lock);
            break;
        case SENSOR_FEATURE_GET_CROP_INFO:
            //pr_info("SENSOR_FEATURE_GET_CROP_INFO scenarioId:%d\n", (UINT32)*feature_data);

            wininfo = (struct SENSOR_WINSIZE_INFO_STRUCT *)(uintptr_t)(*(feature_data+1));

            switch (*feature_data_32) {
                case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
                    memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[1],sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
                    break;
                case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
                    memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[2],sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
                    break;
                case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
                    memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[3],sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
                    break;
                case MSDK_SCENARIO_ID_SLIM_VIDEO:
                    memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[4],sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
                    break;
                case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
                default:
                    memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[0],sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
                    break;
            }
            break;
        case SENSOR_FEATURE_SET_IHDR_SHUTTER_GAIN:
            //LOG_INF("SENSOR_SET_SENSOR_IHDR is no support");
            break;
        default:
            break;
    }

    return ERROR_NONE;
}   /*  feature_control()  */

static struct SENSOR_FUNCTION_STRUCT sensor_func = {
    open,
    get_info,
    get_resolution,
    feature_control,
    control,
    close
};

UINT32 IMX134_2LANE_MIPI_RAW_SensorInit(struct SENSOR_FUNCTION_STRUCT **pfFunc)
{
    /* To Do : Check Sensor status here */
    if (pfFunc!=NULL)
        *pfFunc=&sensor_func;
    return ERROR_NONE;
}   /*  IMX134_MIPI_RAW_SensorInit  */
