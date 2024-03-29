#include <ros.h>
#include <std_msgs/Int32.h>
#include <std_msgs/Float32.h>
#include <auv_cal_state_la_2017/HControl.h>
#include <auv_cal_state_la_2017/RControl.h>
#include <auv_cal_state_la_2017/MControl.h>
#include <ez_async_data/Rotation.h>
#include <auv_cal_state_la_2017/FrontCamDistance.h>
#include <auv_cal_state_la_2017/BottomCamDistance.h>
#include <Servo.h>
#include <Wire.h>
#include <SPI.h>
#include <Wire.h>
#include "MS5837.h"
#include "SFE_LSM9DS0.h"
#include "fontv1.h";

#define GyroMeasError PI * (40.0f / 180.0f)       // gyroscope measurement error in rads/s (shown as 3 deg/s)
#define GyroMeasDrift PI * (0.0f / 180.0f)      // gyroscope measurement drift in rad/s/s (shown as 0.0 deg/s/s)
#define beta sqrt(3.0f / 4.0f) * GyroMeasError   // compute beta
#define zeta sqrt(3.0f / 4.0f) * GyroMeasDrift   // compute zeta, the other free parameter in the Madgwick scheme usually set to a small or zero value
#define Kp 2.0f * 5.0f // these are the free parameters in the Mahony filter and fusion scheme, Kp for proportional feedback, Ki for integral
#define Ki 0.0f

#define LSM9DS0_XM  0x1D
#define LSM9DS0_G   0x6B
LSM9DS0 dof(MODE_I2C, LSM9DS0_G, LSM9DS0_XM);

//LCD Display
#define RST 48
#define CE  47
#define DC  46
#define DIN 45
#define CLK 44

const byte INT1XM = 53; // INT1XM tells us when accel data is ready
const byte INT2XM = 51; // INT2XM tells us when mag data is ready
const byte DRDYG  = 49; // DRDYG  tells us when gyro data is ready

//Initialization of the Servo's for Blue Robotics Motors
Servo T1;     //right front
Servo T2;     //right back
Servo T3;     //left front
Servo T4;     //left back
Servo T5;     //right front
Servo T6;     //right back
Servo T7;     //left front
Servo T8;     //left back

MS5837 sensor;

//variable for LCD 
char string[8];

//CHANGED PWM_Motors TO PWM_Motors_depth SINCE THERE ARE 2 DIFFERENT PWM CALCULATIONS
//ONE IS FOR DEPTH AND THE OTHER IS USED FOR MOTORS TO ROTATE TO PROPER NEW LOCATION
int PWM_Motors_Depth;
float dutyCycl_depth;
float assignedDepth;
float feetDepth_read;

//initializations for IMU
float pitch, yaw, roll, heading;
float deltat = 0.0f;        // integration interval for both filter schemes
uint32_t lastUpdate = 0;    // used to calculate integration interval
uint32_t Now = 0;           // used to calculate integration interval

int i;
int PWM_Motors_orient;
float abias[3] = {0, 0, 0}, gbias[3] = {0, 0, 0};
float ax, ay, az, gx, gy, gz, mx, my, mz; // variables to hold latest sensor data values
float q[4] = {1.0f, 0.0f, 0.0f, 0.0f};    // vector to hold quaternion
float eInt[3] = {0.0f, 0.0f, 0.0f};       // vector to hold integral error for Mahony method
float temperature;
float dutyCycl_orient;
float assignedYaw;

//Initialize ROS node
//const float rotationUpperBound = 166.2;
//const float rotationLowerBound = -193.8;
const float rotationUpperBound = 179;
const float rotationLowerBound = -179;
const float topDepth = 0.5;
const float bottomDepth = 12;
int mControlDirection;
float mControlPower;
float mControlDistance;
float mControlRunningTime;
float mControlMode5Timer;
float frontCamForwardDistance;
float frontCamHorizontalDistance;
float frontCamVerticalDistance;
float bottomCamForwardDistance;
float bottomCamHorizontalDistance;
float bottomCamVerticalDistance;
float centerTimer;
float rotationTimer;
float rotationTime;
float movementTimer;
float movementTime;
bool subIsReady;
bool isGoingUp;
bool isGoingDown;
bool isTurningRight;
bool isTurningLeft;
bool keepTurningRight;
bool keepTurningLeft;
bool rControlMode3;
bool rControlMode4;
bool mControlMode1;
bool mControlMode2;
bool mControlMode3;
bool mControlMode4;
bool mControlMode5;
int mControlMode5Direction;
bool keepMovingForward;
bool keepMovingRight;
bool keepMovingBackward;
bool keepMovingLeft;

//Testing-----------------------
float positionX = 0;
float positionY = 0;

ros::NodeHandle nh;
std_msgs::Float32 currentDepth;
//std_msgs::Float32 currentRotation;
auv_cal_state_la_2017::HControl hControlStatus;
auv_cal_state_la_2017::RControl rControlStatus;
auv_cal_state_la_2017::MControl mControlStatus;

ros::Publisher hControlPublisher("height_control_status", &hControlStatus);     //int: state, float: depth
ros::Publisher rControlPublisher("rotation_control_status", &rControlStatus);   //int: state, float: rotation
ros::Publisher mControlPublisher("movement_control_status", &mControlStatus);   //int: state, int: direction, float: distance
ros::Publisher currentDepthPublisher("current_depth", &currentDepth);           //float: depth
//ros::Publisher currentRotationPublisher("current_rotation", &currentRotation);  //float: rotation

void hControlCallback(const auv_cal_state_la_2017::HControl& hControl);
void rControlCallback(const auv_cal_state_la_2017::RControl& rControl);
void mControlCallback(const auv_cal_state_la_2017::MControl& mControl);

void rotationCallback(const ez_async_data::Rotation& rotation);

void frontCamDistanceCallback(const auv_cal_state_la_2017::FrontCamDistance& frontCamDistance);
void bottomCamDistanceCallback(const auv_cal_state_la_2017::BottomCamDistance& bottomCamDistance);
ros::Subscriber<auv_cal_state_la_2017::HControl> hControlSubscriber("height_control", &hControlCallback);   //int: state, float: depth
ros::Subscriber<auv_cal_state_la_2017::RControl> rControlSubscriber("rotation_control", &rControlCallback); //int: state, float: rotation
ros::Subscriber<auv_cal_state_la_2017::MControl> mControlSubscriber("movement_control", &mControlCallback);

ros::Subscriber<ez_async_data::Rotation> rotationSubscriber("current_rotation", &rotationCallback);

ros::Subscriber<auv_cal_state_la_2017::FrontCamDistance> frontCamDistanceSubscriber("front_cam_distance", &frontCamDistanceCallback);
ros::Subscriber<auv_cal_state_la_2017::BottomCamDistance> bottomCamDistanceSubscriber("bottom_cam_distance", &bottomCamDistanceCallback);

void setup() {

  //For servo motors on Pelican, pins 2-5 are for motors 1-4. PWM on these motors are 1100-1499 (counter
  //clockwise direction) and 1501-1900 (clockwise direction). Note that in code I use pins 6-8, this was used
  //for testing with leds.
  pinMode(INT1XM, INPUT);
  pinMode(INT2XM, INPUT);
  pinMode(DRDYG,  INPUT);
  pinMode(2, OUTPUT); //1 on motor
  pinMode(3, OUTPUT); //2 on motor
  pinMode(4, OUTPUT); //3 on motor
  pinMode(5, OUTPUT); //4 on motor
  pinMode(6, OUTPUT); //5 on motor
  pinMode(7, OUTPUT); //6 on motor
  pinMode(8, OUTPUT); //7 on motor
  pinMode(9, OUTPUT); //8 on motor

  //Pins for LCD Display (NOKIA 5110)
  pinMode(RST, OUTPUT);
  pinMode(CE, OUTPUT);
  pinMode(DC, OUTPUT);
  pinMode(DIN, OUTPUT);
  pinMode(CLK, OUTPUT);
  digitalWrite(RST, LOW);
  digitalWrite(RST, HIGH);

  //Initialization of LCD 
  LcdWriteCmd(0x21);                    //LCD extended commands
  LcdWriteCmd(0xB8);                    //set LCD VOp (contrast)
  LcdWriteCmd(0x04);                    //set emp coefficent
  LcdWriteCmd(0x14);                    //LCD bias mode 1:30
  LcdWriteCmd(0x20);                    //LCD basic commands
  LcdWriteCmd(0x0C);                    

  for (int i = 0; i < 504; i++) {
    LcdWriteData(0x00);
  }

 LcdXY(0,0);
 LcdWriteString("Roll: ");
 LcdXY(0,2);
 LcdWriteString("Pitch: ");
 LcdXY(0,4);
 LcdWriteString("Yaw: ");

 //Pelican Motors activation of motors (initialization of pins to servo motors)
  T1.attach(2); //right front servo
  T1.writeMicroseconds(1500);
  T2.attach(3); //right back servo
  T2.writeMicroseconds(1500);
  T3.attach(4); //back left servo
  T3.writeMicroseconds(1500);
  T4.attach(5); //front left servo
  T4.writeMicroseconds(1500);
  T5.attach(6); //front left servo
  T5.writeMicroseconds(1500);
  T6.attach(7); //front left servo
  T6.writeMicroseconds(1500);
  T7.attach(8); //front left servo
  T7.writeMicroseconds(1500);
  T8.attach(9); //front left servo
  T8.writeMicroseconds(1500);

  delay(1000);


  Wire.begin();
  Serial.begin(38400);
  Serial.println("Serial 38400");

  //Initialize ROS variable
  mControlDirection = 0;
  mControlPower = 0;
  mControlDistance = 0;
  mControlRunningTime = 0;
  mControlMode5Timer = 0;
  frontCamForwardDistance = 0;
  frontCamHorizontalDistance = 0;
  frontCamVerticalDistance = 0;
  bottomCamForwardDistance = 0;
  bottomCamHorizontalDistance = 0;
  bottomCamVerticalDistance = 0;
  centerTimer = 0;
  rotationTimer = 0;
  rotationTime = 10;
  movementTimer = 0;
  movementTime = 10;
  subIsReady = false;
  isGoingUp = false;
  isGoingDown = false;
  isTurningRight = false;
  isTurningLeft = false;
  keepTurningRight = false;
  keepTurningLeft = false;
  rControlMode3 = false;
  rControlMode4 = false;
  mControlMode1 = false;
  mControlMode2 = false;
  mControlMode3 = false;
  mControlMode4 = false;
  mControlMode5 = false;
  keepMovingForward = false;
  keepMovingRight = false;
  keepMovingBackward = false;
  keepMovingLeft = false;

  //Testing------------------
  feetDepth_read = 0;
  roll = 999;
  pitch = 999;
  yaw = 999;
  positionX = 0;
  positionY = 0;

  assignedDepth = topDepth;
  currentDepth.data = feetDepth_read;

//  assignedDepth = 0.2;
  //assignedYaw = -191.5;
  //subIsReady = true;
  assignedYaw = 85;
//  currentRotation.data = yaw;

  hControlStatus.state = 1;
  hControlStatus.depth = 0;
  rControlStatus.state = 1;
  rControlStatus.rotation = 0;

  // MControl
  // state => 0: off, 1: on with power, 2: on with distance, 3: centered with front cam, 4: centered with bottom cam
  // direction => 0: none, 1: forward, 2: right, 3: backward, 4: left
  // power => 0: none, x: x power add to the motors
  // distance => 0: none, x: x units away from the object
  // runningTime => 0: none, x: x seconds for the motors to run
  mControlStatus.state = 0;
  mControlStatus.mDirection = 0;
  mControlStatus.power = 0;
  mControlStatus.distance = 0;
  mControlStatus.runningTime = 0;

  nh.initNode();
  nh.subscribe(hControlSubscriber);
  nh.subscribe(rControlSubscriber);
  nh.subscribe(mControlSubscriber);
  nh.subscribe(rotationSubscriber);
  nh.subscribe(frontCamDistanceSubscriber);
  nh.subscribe(bottomCamDistanceSubscriber);
  nh.advertise(hControlPublisher);
  nh.advertise(rControlPublisher);
  nh.advertise(mControlPublisher);
  nh.advertise(currentDepthPublisher);
//  nh.advertise(currentRotationPublisher);

//  initializeIMU();
//
  sensor.init();
  sensor.setFluidDensity(997); // kg/m^3 (997 freshwater, 1029 for seawater)

//  nh.loginfo("Data is ready.");
  nh.loginfo("Sub is staying. Waiting to receive data from master...\n");

}

void loop() {

//  gettingRawData();
    sensor.read();
//  //Timer
//  Now = micros();
//  deltat = ((Now - lastUpdate)/1000000.0f); // set integration time by time elapsed since last filter update
//  lastUpdate = Now;
//  // Sensors x- and y-axes are aligned but magnetometer z-axis (+ down) is opposite to z-axis (+ up) of accelerometer and gyro!
//  // This is ok by aircraft orientation standards!
//  // Pass gyro rate as rad/s
//  MahonyQuaternionUpdate(ax, ay, az, gx*PI/180.0f, gy*PI/180.0f, gz*PI/180.0f, mx, my, mz);
//
//  //yaw   = atan2(2.0f * (q[1] * q[2] + q[0] * q[3]), q[0] * q[0] + q[1] * q[1] - q[2] * q[2] - q[3] * q[3]);
//  pitch = -asin(2.0f * (q[1] * q[3] - q[0] * q[2]));
//  roll  = atan2(2.0f * (q[0] * q[1] + q[2] * q[3]), q[0] * q[0] - q[1] * q[1] - q[2] * q[2] + q[3] * q[3]);
//  pitch *= 180.0f / PI;
//  //yaw   *= 180.0f / PI;
//  //yaw   -= 13.8; // Declination at Danville, California is 13 degrees 48 minutes and 47 seconds on 2014-04-04
//  roll  *= 180.0f / PI;
//  pitch -= 7.1;

  //Set the display outputs for roll, pitch, and yaw
  LcdXY(40, 0);
  LcdWriteString(dtostrf(roll, 5, 2, string));
  LcdXY(40, 2);
  LcdWriteString(dtostrf(pitch, 5, 2, string));
  LcdXY(40, 4);
  LcdWriteString(dtostrf(yaw, 5, 2, string));


  //Depth
  //Testing----------------------
  feetDepth_read =  sensor.depth() * 3.28 + 0.23;                                   //1 meter = 3.28 feet
  dutyCycl_depth = (abs(assignedDepth - feetDepth_read)/ 13.0);              //function to get a percentage of assigned height to the feet read
  PWM_Motors_Depth = dutyCycl_depth * 400;                                   //PWM for motors are between 1500 - 1900; difference is 400

  //Rotation
  //duty cycle and PWM calculation for orientation
  dutyCycl_orient = degreeToTurn() / 180.0;
  PWM_Motors_orient = dutyCycl_orient * 400; //Maximum is 200

  if(subIsReady){
    heightControl();
    rotationControl();
    movementControl();
  }

  //Update and publish current data to master
  currentDepth.data = feetDepth_read;
  currentDepthPublisher.publish(&currentDepth);
//  currentRotation.data = yaw;
//  currentRotationPublisher.publish(&currentRotation);
  
//  Serial.pr/intln("Serial print test");
  nh.spinOnce();

  delay(10);
}


void rotationCallback(const ez_async_data::Rotation& rotation){
//  pitch = rotation.roll * (-1);
//  roll = rotation.pitch;
//  yaw = rotation.yaw;

  yaw = rotation.yaw;
  roll = rotation.roll;
  pitch = rotation.pitch;
  char log_msg[30];
  float temp = yaw;
  int temp1 = (temp - (int)temp) * 100;

  float temp2 = pitch;
  int temp21 = (temp2 - (int)temp2) * 100;
  
  float temp3 = roll;
  int temp31 = (temp3 - (int)temp3) * 100;

  sprintf(log_msg, "y,p,r = %0d.%d, %0d.%d, %0d.%d", (int)temp, temp1, (int)temp2, temp21, (int)temp3, temp31);
  nh.loginfo(log_msg);
  
//  char yawChar[11];
//  dtostrf(yaw, 4, 2, yawChar);
//  nh.loginfo("yaw: ");
//  nh.loginfo(yawChar);
}


void hControlCallback(const auv_cal_state_la_2017::HControl& hControl) {
  int hState = hControl.state;
  float hDepth = hControl.depth;
  char depthChar[6];
  float depth = hControl.depth;
  dtostrf(depth, 4, 2, depthChar);

  if(hControl.state == 0){
    if(!isGoingUp && !isGoingDown){
      if(depth == -1 || depth + assignedDepth >= bottomDepth)
        assignedDepth = bottomDepth;
      else
        assignedDepth = assignedDepth + depth;
      isGoingDown = true;
      nh.loginfo("Going down...");
      nh.loginfo(depthChar);
      nh.loginfo("ft...(-1 means infinite)\n");
    }else
      nh.loginfo("Sub is still running. Command abort.");
  }
  else if(hControl.state == 1){
    if(isGoingUp || isGoingDown){
      isGoingUp = false;
      isGoingDown = false;
      nh.loginfo("Height control is now cancelled\n");
    }
    assignedDepth = feetDepth_read;
  }
  else if(hControl.state == 2){
    if(!isGoingUp && !isGoingDown){
      if(depth == -1 || depth >= assignedDepth - topDepth)
        assignedDepth = topDepth;
      else
        assignedDepth = assignedDepth - depth;
      isGoingUp = true;
      nh.loginfo("Going up...");
      nh.loginfo(depthChar);
      nh.loginfo("ft...(-1 means infinite)\n");
    }else
      nh.loginfo("Sub is still running. Command abort.");
  }
  else if(hControl.state == 4){
    if(!subIsReady){
      subIsReady = true;
      nh.loginfo("Motors unlocked.");
    }
    assignedDepth = feetDepth_read;
    //assignedDepth = 0.1;
  }
  else if(hControl.state == 5){
    if(subIsReady){
      subIsReady = false;
      nh.loginfo("Motors are locked.");

    }
    assignedDepth = feetDepth_read;
//    assignedDepth = 0.2;
    subIsReady = false;
    T1.writeMicroseconds(1500);
    T2.writeMicroseconds(1500);
    T3.writeMicroseconds(1500);
    T4.writeMicroseconds(1500);
    T5.writeMicroseconds(1500);
    T6.writeMicroseconds(1500);
    T7.writeMicroseconds(1500);
    T8.writeMicroseconds(1500);
  }
  hControlStatus.state = hState;
  hControlStatus.depth = hDepth;
  hControlPublisher.publish(&hControlStatus);

}


void rControlCallback(const auv_cal_state_la_2017::RControl& rControl){
  char rotationChar[11];
  float rotation = rControl.rotation;
  dtostrf(rotation, 4, 2, rotationChar);

  if(rControl.state == 0){
    if(!isTurningRight && !isTurningLeft && !rControlMode3 && !rControlMode4){
      if(rotation == -1) {
        keepTurningLeft = true;
        //Testing---------------------------------------------------
        rotationTimer = 0;
      }
      else{
        if (yaw - rotation <= rotationLowerBound)
          assignedYaw = yaw - rotation + 360;
        else
          assignedYaw = yaw - rotation;
      }
      isTurningLeft = true;
      nh.loginfo("Turning left...");
      nh.loginfo(rotationChar);
      nh.loginfo("degree...(-1 means infinite)\n");
    }else
      nh.loginfo("Sub is still rotating. Command abort.");
  }
  else if(rControl.state == 1){
    if(isTurningRight || isTurningLeft || keepTurningRight || keepTurningLeft || rControlMode3 || rControlMode4){
      isTurningRight = false;
      isTurningLeft = false;
      keepTurningRight = false;
      keepTurningLeft = false;
      rControlMode3 = false;
      rControlMode4 = false;
      nh.loginfo("Rotation control is now cancelled\n");
    }
    assignedYaw = yaw;
  }
  else if(rControl.state == 2){
    if(!isTurningRight && !isTurningLeft && !rControlMode3 && !rControlMode4){
      if(rotation == -1) {
        keepTurningRight = true;
        //Testing---------------------------------------------------------------
        rotationTimer = 0;
      }
      else{
        if (yaw + rotation > rotationUpperBound)
          assignedYaw = yaw + rotation - 360;
        else
          assignedYaw = yaw + rotation;
      }
      isTurningRight = true;
      nh.loginfo("Turning right...");
      nh.loginfo(rotationChar);
      nh.loginfo("degree...(-1 means infinite)\n");
    }else
      nh.loginfo("Sub is still rotating.Command abort.");
  }
  else if(rControl.state == 3){
    if(!isTurningRight && !isTurningLeft && !rControlMode3 && !rControlMode4){
      rControlMode3 = true;
      rotationTime = 3;
      rotationTimer = 0;
      nh.loginfo("Rotate with front camera horizontal distance.\n");
    }
    else
      nh.loginfo("Sub is still rotating.Command abort.");
  }
  else if(rControl.state == 4){
    if(!isTurningRight && !isTurningLeft && !rControlMode3 && !rControlMode4){
      rControlMode4 = true;
      nh.loginfo("Keep rotating with front camera horizontal distance.\n");
    }
    else
      nh.loginfo("Sub is still rotating.Command abort.");
    
  }
  rControlStatus.state = rControl.state;
  rControlStatus.rotation = rControl.rotation;
  rControlPublisher.publish(&rControlStatus);

}

// MControl
// state => 0: off, 1: on with power, 2: on with distance, 3: centered with front cam, 4: centered with bottom cam
// direction => 0: none, 1: forward, 2: right, 3: backward, 4: left
// power => 0: none, x: x power add to the motors
// distance => 0: none, x: x units away from the object
void mControlCallback(const auv_cal_state_la_2017::MControl& mControl){

  String directionStr;
  char powerChar[11];
  char distanceChar[11];
  char timeChar[11];
  float power = mControl.power;
  float distance = mControl.distance;
  float mode5Time = mControl.runningTime;
  dtostrf(power, 4, 2, powerChar);
  dtostrf(distance, 4, 2, distanceChar);
  dtostrf(mode5Time, 4, 2, timeChar);


  if(mControl.state == 0){
    if(mControlMode1 || mControlMode2|| mControlMode3 || mControlMode4 || mControlMode5){
      T6.writeMicroseconds(1500);
      T8.writeMicroseconds(1500);
      T5.writeMicroseconds(1500);
      T7.writeMicroseconds(1500);
      mControlMode1 = false;
      mControlMode2 = false;
      mControlMode3 = false;
      mControlMode4 = false;
      mControlMode5 = false;
      keepMovingForward = false;
      keepMovingRight = false;
      keepMovingBackward = false;
      keepMovingLeft = false;
      nh.loginfo("Movement control is now cancelled\n");
    }
    mControlDirection = 0;
    mControlPower = 0;
    mControlDistance = 0;
  }
  else if(mControl.state == 1){
    if(mControlMode1 || mControlMode2|| mControlMode3 || mControlMode4 || mControlMode5)
      nh.loginfo("Sub is still moving. Command abort.");
    else if(mControl.mDirection != 1 && mControl.mDirection != 2 && mControl.mDirection != 3 && mControl.mDirection != 4)
      nh.loginfo("Invalid direction with state 1. Please check the program and try again.\n");
    else if(mControl.power == 0)
      nh.loginfo("Invalid power with state 1. Please check the program and try again.");
    else{
      if(mControl.mDirection == 1){
        keepMovingForward = true;
        directionStr = "forward";
      }
      else if(mControl.mDirection == 2){
        keepMovingRight = true;
        directionStr = "right";
      }
      else if(mControl.mDirection == 3){
        keepMovingBackward = true;
        directionStr = "backward";
      }
      else if(mControl.mDirection == 4){
        keepMovingLeft = true;
        directionStr = "left";
      }

      directionStr = "Moving " + directionStr + " with power...";
      nh.loginfo(directionStr.c_str());
      nh.loginfo(powerChar);
      nh.loginfo("...\n");

      //Testing -----------------------------------------------------------
      movementTimer = 0;
      mControlMode1 = true;
      mControlDirection = mControl.mDirection;
      mControlPower = mControl.power;
      mControlDistance = 0;
    }
  }
  else if(mControl.state == 2){
    if(mControlMode1 || mControlMode2|| mControlMode3 || mControlMode4 || mControlMode5)
      nh.loginfo("Sub is still moving. Command abort.");
    else if(mControl.mDirection != 1)
      nh.loginfo("Invalid direction with state 2. Please check the program and try again.\n");
    else{
      nh.loginfo("Adjusting distance to...");
      nh.loginfo(distanceChar);
      nh.loginfo("away from the target.../n");

      mControlMode2 = true;
      mControlPower = 0;
      mControlDistance = mControl.distance;
    }
  }
  else if(mControl.state == 3){
    if(mControlMode1 || mControlMode2|| mControlMode3 || mControlMode4 || mControlMode5)
      nh.loginfo("Sub is still moving. Command abort.");
    else{
      nh.loginfo("Centering the sub with target from the front camera...\n");

      mControlMode3 = true;
      mControlPower = 0;
      mControlDistance = 0;
    }
  }
  else if(mControl.state == 4){
    if(mControlMode1 || mControlMode2 || mControlMode3 || mControlMode4 || mControlMode5)
      nh.loginfo("Sub is still moving. Command abort.");
    else{
      nh.loginfo("Centering the sub with target from the bottom camera...\n");

      mControlMode4 = true;
      mControlPower = 0;
      mControlDistance = 0;
    }
  }
  else if(mControl.state == 5){
    if(mControlMode1 || mControlMode2|| mControlMode3 || mControlMode4 || mControlMode5)
      nh.loginfo("Sub is still moving. Command abort.");
    else if(mControl.mDirection != 1 && mControl.mDirection != 2 && mControl.mDirection != 3 && mControl.mDirection != 4)
      nh.loginfo("Invalid direction with state 5. Please check the program and try again.\n");
    else if(mControl.power == 0)
      nh.loginfo("Invalid power with state 5. Please check the program and try again.");
    else{
      if(mControl.mDirection == 1){
        directionStr = "forward";
      }
      else if(mControl.mDirection == 2){
        directionStr = "right";
      }
      else if(mControl.mDirection == 3){
        directionStr = "backward";
      }
      else if(mControl.mDirection == 4){
        directionStr = "left";
      }

      directionStr = "Moving " + directionStr + " with power...";
      nh.loginfo(directionStr.c_str());
      nh.loginfo(powerChar);
      nh.loginfo("for...");
      nh.loginfo(timeChar);
      nh.loginfo("seconds...\n");

      mControlMode5 = true;
      mControlMode5Timer = 0;
      mControlDirection = mControl.mDirection;
      mControlPower = mControl.power;
      mControlRunningTime = mControl.runningTime;
      mControlDistance = 0;
    }
  }
  mControlStatus.state = mControl.state;
  mControlStatus.mDirection = mControl.mDirection;
  mControlStatus.power = mControl.power;
  mControlStatus.distance = mControl.distance;
  mControlStatus.runningTime = mControl.runningTime;
  mControlPublisher.publish(&mControlStatus);

}

//Going upward
void goingUpward(){

  float mp = 3;
  int depthPower = PWM_Motors_Depth * 4;
  int levelPower = (400 - depthPower) / 45 * mp;
  //int reversedLevelPower = (PWM_Motors_Depth / 45) * (-1) * mp;
  int reversedLevelPower = -levelPower / 2;

  for(i = 0; (2 * i) < 90; i++){
    float t1 = depthPower + i * levelPower;
    float t2 = depthPower + i * reversedLevelPower;
    if(t1 > 300) t1 = 300;
    if(t2 < -200) t2 = -200;
    
    //rolled left(positive value)
    if((roll < -1 *( 2 * i)) && (roll > -1 * (2 * i + 2)) ){   
      //Boost the left motors
      //nh.loginfo("up   rolled left");
      T1.writeMicroseconds(1500 + t1);
      T2.writeMicroseconds(1500 - t1);
      //Downgrade the right motors
      T3.writeMicroseconds(1500 - t2);
      T4.writeMicroseconds(1500 - t2);
    }
    //rolled right(negative value)
    if((roll > 2 * i) && (roll < (2 * i + 2))){
      //Boost the right motors
      //nh.loginfo("up   rolled right");
      T3.writeMicroseconds(1500 - t1);
      T4.writeMicroseconds(1500 - t1);
      //Downgrade the left motors
      T1.writeMicroseconds(1500 + t2);
      T2.writeMicroseconds(1500 - t2);
    }
    //pitched backward(positive value)
    if((pitch > 2 * i) && (pitch < (2 * i + 2))){
      //Boost the back motors
      //nh.loginfo("up   pitch backward");
      T1.writeMicroseconds(1500 + t1);
      T4.writeMicroseconds(1500 - t1);
      //Downgrade the front motors
      T2.writeMicroseconds(1500 - t2);
      T3.writeMicroseconds(1500 - t2);
    }
    //pitched forward(negative value)
    if((pitch < -1 * (2 * i)) && (pitch > -1 * (2 * i + 2))){
      //Boost the front motors
      //nh.loginfo("up   pitch forward");
      T2.writeMicroseconds(1500 - t1);
      T3.writeMicroseconds(1500 - t1);
      //Downgrade the back motors
      T1.writeMicroseconds(1500 + t2);
      T4.writeMicroseconds(1500 - t2);
    }
  }

}

//Going downward
void goingDownward(){

  float mp = 3;
  PWM_Motors_Depth = -PWM_Motors_Depth;
  int depthPower = PWM_Motors_Depth * 4;
  int levelPower = ((400 + depthPower) / 45) * (-1) * mp;
  //int reversedLevelPower = ((-1) * PWM_Motors_Depth) / 45 * mp;
  int reversedLevelPower = -levelPower / 2;

  for (i = 0; (2 * i) < 90; i++){ //loop will start from 0 degrees -> 90 degrees
    float t1 = depthPower + i * levelPower;
    float t2 = depthPower + i * reversedLevelPower;
    if(t1 < -300) t1 = 300;
    if(t2 > 200) t2 = 200;
    //rolled left (positive value)
    if((roll > 2 * i) && (roll < (2 * i + 2))){
      //Boost the left motors
      //nh.loginfo("down   rolled left");
      T1.writeMicroseconds(1500 + t1);
      T2.writeMicroseconds(1500 - t1);
      //Downgrade the right motors
      T3.writeMicroseconds(1500 - t2);
      T4.writeMicroseconds(1500 - t2);
    }
    //rolled right (negative values)
    if((roll < -1 *( 2 * i)) && (roll > -1 * (2 * i + 2))){
      //Boost the right motors
      //nh.loginfo("down   rolled right");
      T3.writeMicroseconds(1500 - t1);
      T4.writeMicroseconds(1500 - t1);
      //Downgrade the left motors
      T1.writeMicroseconds(1500 + t2);
      T2.writeMicroseconds(1500 - t2);
    }
    //pitched back (positive value)
    if((pitch < -1 * (2 * i)) && (pitch > -1 * (2 * i + 2))){
      //Boost the back motors
      //nh.loginfo("down   pitch backward");
      T1.writeMicroseconds(1500 + t1);
      T4.writeMicroseconds(1500 - t1);
      //Downgrade the front motors
      T2.writeMicroseconds(1500 - t2);
      T3.writeMicroseconds(1500 - t2);
    }
    //pitched forward (negative value)
    if((pitch > 2 * i) && (pitch < (2 * i + 2))){
      //Boost the front motors
      //nh.loginfo("down   pitch forward");
      T2.writeMicroseconds(1500 - t1);
      T3.writeMicroseconds(1500 - t1);
      //Downgrade the back motors
      T1.writeMicroseconds(1500 + t2);
      T4.writeMicroseconds(1500 - t2);
    }
  }

}

void heightControl(){
  float heightError = 0.1;
//  char heightChar[11];
//  dtostrf(assignedDepth, 4, 2, heightChar);
//  nh.loginfo(heightChar);
  
  //Going down
  if (feetDepth_read < assignedDepth){
    goingDownward();
    //nh.loginfo("going down");

    //Testing--------------------------
    //feetDepth_read += 0.08;

  }
  //Going up
  else if (feetDepth_read > assignedDepth){
    goingUpward();
    //nh.loginfo("going up");

    //Testing---------------------------
    //feetDepth_read -= 0.08;

  }
  //Reach the height
  if (feetDepth_read < assignedDepth + heightError && feetDepth_read > assignedDepth - heightError) {
    if(isGoingUp || isGoingDown){
      isGoingUp = false;
      isGoingDown = false;
      nh.loginfo("Assigned depth reached.\n");
    }
    hControlStatus.state = 1;
    hControlStatus.depth = 0;
    hControlPublisher.publish(&hControlStatus);
  }

}

//read rotation is from -193.8 to 166.2
void rotationControl(){

  float delta = degreeToTurn();
  float rotationError = 3;
  int fixedPower = 60;

  //boundry from -245 to 245
  if(rControlMode4){
    if(frontCamHorizontalDistance != 999){
      float mode3Power = abs(frontCamHorizontalDistance) / 245 * 200 + 40;
      if(mode3Power > 300) mode3Power = 300;
      if(frontCamHorizontalDistance > 0){
        T5.writeMicroseconds(1500 - mode3Power);
        T7.writeMicroseconds(1500 + mode3Power);
        nh.loginfo("turn right");
      }
      else if(frontCamHorizontalDistance < 0){
        T5.writeMicroseconds(1500 + mode3Power);
        T7.writeMicroseconds(1500 - mode3Power);
        nh.loginfo("turn left");
      }
      assignedYaw = yaw;
    }
    else{
      nh.loginfo("Invalid frontCamHorizontalDistance value.");
    }
  }
  if(rControlMode3){
    
    if(frontCamHorizontalDistance != 999){
      float mode3Power = abs(frontCamHorizontalDistance) / 245 * 200 + 40;
      if(mode3Power > 300) mode3Power = 300;
//      char rChar[11];
//      dtostrf(rotationTimer, 4, 2, rChar);
//      nh.loginfo(rChar);
      if(frontCamHorizontalDistance > 0){
        T5.writeMicroseconds(1500 - mode3Power);
        T7.writeMicroseconds(1500 + mode3Power);
        nh.loginfo("turn right");
      }
      else if(frontCamHorizontalDistance < 0){
        T5.writeMicroseconds(1500 + mode3Power);
        T7.writeMicroseconds(1500 - mode3Power);
        nh.loginfo("turn left");
      }
      if(frontCamHorizontalDistance < 30 && frontCamHorizontalDistance > -30){
        rotationTimer += 0.05;
        if(rotationTimer >= rotationTime){
          nh.loginfo("Times up");
          rControlMode3 = false;
        }
      }
      else rotationTimer = 0;
      assignedYaw = yaw;
    }
    else{
      nh.loginfo("Invalid frontCamHorizontalDistance value.");
    }
  }
  else if(keepTurningLeft){
    //Turn on left rotation motor with fixed power
    T5.writeMicroseconds(1500 + fixedPower);
    T7.writeMicroseconds(1500 - fixedPower);
    assignedYaw = yaw;
    //Testing----------------------------
//    rotationTimer += 0.01;
//    if(rotationTimer > rotationTime)
//      keepTurningLeft = false;
//    yaw += 0.05;
//    if(yaw > rotationUpperBound)
//      yaw -= 360;
  }
  else if(keepTurningRight){
    //Turn on right rotation motor with fixed power
    T5.writeMicroseconds(1500 - fixedPower);
    T7.writeMicroseconds(1500 + fixedPower);
    assignedYaw = yaw;
    //Testing----------------------------
//    rotationTimer += 0.01;
//    if(rotationTimer > rotationTime)
//      keepTurningRight = false;
//    yaw -= 0.05;
//    if(yaw < rotationLowerBound)
//      yaw +=360;
  }
  // AutoRotation to the assignedYaw with 1 degree error tolerance
  else if(delta > 2){
    if(yaw + delta > rotationUpperBound){
      if(yaw - delta == assignedYaw)
        rotateLeftDynamically();
      else
        rotateRightDynamically();
    }
    else if(yaw - delta < rotationLowerBound){
      if(yaw + delta == assignedYaw)
        rotateRightDynamically();
      else
        rotateLeftDynamically();
    }
    else if(yaw < assignedYaw)
      rotateRightDynamically();
    else
      rotateLeftDynamically();
  }
  //No rotation
 if(!keepTurningRight && !keepTurningLeft && !rControlMode3 && !rControlMode4 && delta < rotationError){
    if(isTurningRight || isTurningLeft){
      isTurningRight = false;
      isTurningLeft = false;
      nh.loginfo("Assigned rotation reached.\n");
    }
    rControlStatus.state = 1;
    rControlStatus.rotation = 0;
    rControlPublisher.publish(&rControlStatus);
  }

}


void movementControl(){

  if(mControlMode1){
    if(keepMovingForward){
      T6.writeMicroseconds(1500 + mControlPower);
      T8.writeMicroseconds(1500 - mControlPower);
      //Testing-------------------
      positionY += 0.05;
      //nh.loginfo("moving forward...");
    }
    else if(keepMovingRight){
      T5.writeMicroseconds(1500 - mControlPower);
      T7.writeMicroseconds(1500 - mControlPower);
      //Testing-------------------
      positionX += 0.05;
      //nh.loginfo("moving right...");
    }
    else if(keepMovingBackward){
      T6.writeMicroseconds(1500 - mControlPower);
      T8.writeMicroseconds(1500 + mControlPower);
      //Testing-------------------
      positionY -= 0.05;
      //nh.loginfo("moving backward...");
    }
    else if(keepMovingLeft){
      T5.writeMicroseconds(1500 + mControlPower);
      T7.writeMicroseconds(1500 + mControlPower);
      //Testing-------------------
      positionX -= 0.05;
      //nh.loginfo("moving left...");
    }

    //Testing----------------------
    //movementTimer += 0.05;
//    char timerChar[11];
//    dtostrf(movementTimer, 4, 2, timerChar);
//    nh.loginfo(timerChar);
//    if(movementTimer >= movementTime){
//      T6.writeMicroseconds(1500);
//      T8.writeMicroseconds(1500);
//      T5.writeMicroseconds(1500);
//      T7.writeMicroseconds(1500);
//      mControlMode1 = false;
//      nh.loginfo("Mode 1 finished.\n");
//    }
  }
  else if(mControlMode2){
    float error = 0.5;
    float distanceToReach = frontCamForwardDistance - mControlDistance;
    if(distanceToReach > 0){
      //Turn on the front motors with a proportional speed to the distanceToReach
      //Greater the distanceToReach, greater the power

      //Testing-----------------------------
      //positionY += 0.01;
    }else if(distanceToReach < 0){
      //Turn on the back motors with a proportional speed to the distanceToReach
      //Smaller the distanceToReach, greater the power

      //Testing-----------------------------
      //positionY -= 0.01;
    }
    //Timer
    if(distanceToReach > error){
      centerTimer = 0;
    }
    else if(distanceToReach < error){
      centerTimer = 0;
    }
    else{
      centerTimer += 0.01;
      if(centerTimer >= 5){
        mControlMode2 = false;
        nh.loginfo("Target distance reached.\n");
      }
    }

  }
  else if(mControlMode3){
    float error = 0.5;
    if(frontCamHorizontalDistance > 0){
      //Turn on the right motors with a proportional speed to the frontCamHorizontalDistance
      //Greater the frontCamHorizontalDistance, greater the power

      //Testing-----------------------------
      //positionX += 0.1;
    }
    else if(frontCamHorizontalDistance < 0){
      //Turn on the left motors with a proportional speed to the frontCamHorizontalDistance
      //Smaller the frontCamHorizontalDistance, greater the power

      //Testing-----------------------------
      //positionX -= 0.1;
    }
    //Timer
    if(frontCamHorizontalDistance > error){
      centerTimer = 0;
    }
    else if(frontCamHorizontalDistance < -error){
      centerTimer = 0;
    }
    else{
      centerTimer += 0.05;
      if(centerTimer >= 5){
        mControlMode3 = false;
        nh.loginfo("Target center reached.\n");
      }
    }
  }
  else if(mControlMode4){
    float error = 0.5;
    if(bottomCamHorizontalDistance > 0){
      //Turn on the right motors with a proportional speed to the bottomCamHorizontalDistance
      //Greater the bottomCamHorizontalDistance, greater the power

      //Testing-----------------------------
      //positionX += 0.1;
    }
    else if(bottomCamHorizontalDistance < 0){
      //Turn on the left motors with a proportional speed to the bottomCamHorizontalDistance
      //Smaller the bottomCamHorizontalDistance, greater the power

      //Testing-----------------------------
      //positionX -= 0.1;
    }
    if(bottomCamHorizontalDistance > error){
      centerTimer = 0;
    }
    else if(bottomCamHorizontalDistance < -error){
      centerTimer = 0;
    }
    else{
      centerTimer += 0.05;
      if(centerTimer >= 5){
        mControlMode4 = false;
        nh.loginfo("Target center reached.\n");
      }
    }
    if(bottomCamVerticalDistance > 0){
      //Turn on the front motors with a proportional speed to the bottomCamVerticalDistance
      //Greater the bottomCamVerticalDistance, greater the power

      //Testing-----------------------------
      //positionY += 0.1;
    }
    else if(bottomCamVerticalDistance < 0){
      //Turn on the back motors with a proportional speed to the bottomCamVerticalDistance
      //Smaller the bottomCamVerticalDistance, greater the power

      //Testing-----------------------------
      //positionT -= 0.1;
    }
    if(bottomCamVerticalDistance > error){
      centerTimer = 0;
    }
    else if(bottomCamVerticalDistance < -error){
      centerTimer = 0;
    }
    else{
      centerTimer += 0.05;
      if(centerTimer >= 5){
        mControlMode4 = false;
        nh.loginfo("Target center reached.\n");
      }
    }
  }
  else if(mControlMode5){
    //forward
    if(mControlDirection == 1){
      T6.writeMicroseconds(1500 + mControlPower + 10);
      T8.writeMicroseconds(1500 - mControlPower);
      //Testing-------------------
      positionY += 0.05;
      nh.loginfo("moving forward...");
    }
    //right
    else if(mControlDirection == 2){
      T5.writeMicroseconds(1500 - mControlPower);
      T7.writeMicroseconds(1500 - mControlPower);
      //Testing-------------------
      positionX += 0.05;
      nh.loginfo("moving right...");
    }
    //backward
    else if(mControlDirection == 3){
      T6.writeMicroseconds(1500 - mControlPower);
      T8.writeMicroseconds(1500 + mControlPower);
      //Testing-------------------
      positionY -= 0.05;
      nh.loginfo("moving backward...");
    }
    //left
    else if(mControlDirection == 4){
      T5.writeMicroseconds(1500 + mControlPower);
      T7.writeMicroseconds(1500 + mControlPower);
      //Testing-------------------
      positionX -= 0.05;
      nh.loginfo("moving left...");
    }
    mControlMode5Timer += 0.05;
    char timerChar[11];
    dtostrf(mControlMode5Timer, 4, 2, timerChar);
    nh.loginfo(timerChar);
    if(mControlMode5Timer >= mControlRunningTime){
      T6.writeMicroseconds(1500);
      T8.writeMicroseconds(1500);
      T5.writeMicroseconds(1500);
      T7.writeMicroseconds(1500);
      mControlMode5 = false;
      nh.loginfo("Mode 5 finished.\n");
    }
  }
  else{
    centerTimer = 0;
    movementTimer = 0;
    mControlMode5Timer = 0;
    mControlDirection = 0;
    mControlRunningTime = 0;
    mControlPower = 0;
    mControlDistance = 0;
    mControlStatus.state = 0;
    mControlStatus.mDirection = 0;
    mControlStatus.power = 0;
    mControlStatus.distance = 0;
    mControlStatus.runningTime = 0;
    mControlPublisher.publish(&mControlStatus);
  }

}


void frontCamDistanceCallback(const auv_cal_state_la_2017::FrontCamDistance& frontCamDistance){
  frontCamForwardDistance = frontCamDistance.frontCamForwardDistance;
  frontCamHorizontalDistance = frontCamDistance.frontCamHorizontalDistance;
  frontCamVerticalDistance = frontCamDistance.frontCamVerticalDistance;
}


void bottomCamDistanceCallback(const auv_cal_state_la_2017::BottomCamDistance& bottomCamDistance){
  bottomCamForwardDistance = bottomCamDistance.bottomCamForwardDistance;
  bottomCamHorizontalDistance = bottomCamDistance.bottomCamHorizontalDistance;
  bottomCamVerticalDistance = bottomCamDistance.bottomCamVerticalDistance;
}


//&&&&&&&&&&&&&&&&&&&&&&&&&&
//NOTE: I FORGOT WHAT IS THE ROTATION ON THE MOTORS IN THE BOT TO SEE WHAT DIRECTION OF MOVEMENT
//WHEN I RETURN TO THE ROOM I CAN FIND OUT AND EDIT THIS CODE.
//  if (yaw < assignedYaw){
//    //turn on motos to go down
//    T5.writeMicroseconds(1500 + PWM_Motors);
//    T7.writeMicroseconds(1500 - PWM_Motors);
//  }
//  else if (yaw > assignedYaw){
//    //turn on motors to go up
//    T5.writeMicroseconds(1500 - PWM_Motors);
//    T7.writeMicroseconds(1500 + PWM_Motors);
//  }
void rotateLeftDynamically(){
  float rotatePower = PWM_Motors_orient * 4.5;
  if(rotatePower > 300) rotatePower = 300;
  if((mControlMode5 && (mControlDirection == 2 || mControlDirection == 4)) || keepMovingRight || keepMovingLeft){
    T6.writeMicroseconds(1500 + rotatePower);
    T8.writeMicroseconds(1500 + rotatePower);
  }
  else{
    T5.writeMicroseconds(1500 + rotatePower);
    T7.writeMicroseconds(1500 - rotatePower);
  }
  nh.loginfo("rotate left");
  //Testing----------------------------
  //yaw += 1;
  //if(yaw > rotationUpperBound) yaw -= 360;

}

void rotateRightDynamically(){
  float rotatePower = PWM_Motors_orient * 4.5;
  if(rotatePower > 300) rotatePower = 300;
  if((mControlMode5 && (mControlDirection == 2 || mControlDirection == 4)) || keepMovingRight || keepMovingLeft){
    T6.writeMicroseconds(1500 - rotatePower);
    T8.writeMicroseconds(1500 - rotatePower);
  }
  else{
    T5.writeMicroseconds(1500 - rotatePower);
    T7.writeMicroseconds(1500 + rotatePower);
  }
  nh.loginfo("rotate right");
  //Testing----------------------------
  //yaw -= 1;
  //if(yaw < rotationLowerBound) yaw +=360;

}

//void rotateRightDynamically(){
//  float rotatePower = PWM_Motors_orient * 1.25;
//  if(rotatePower > 400) rotatePower = 400;
//  //Rotate right with PWM_Motors_orient
//  T5.writeMicroseconds(1500 - rotatePower);
//  T7.writeMicroseconds(1500 + rotatePower);
//  //Testing----------------------------
//  //yaw -= 1;
//  //if(yaw < rotationLowerBound) yaw +=360;
//
//}

//Return 0 to 180
float degreeToTurn(){
  float difference = max(yaw, assignedYaw) - min(yaw, assignedYaw);
  if (difference > 180) return 360 - difference;
  else return difference;
}


//void gettingRawData(){
//
//  if(digitalRead(DRDYG)) {  // When new gyro data is ready
//  dof.readGyro();           // Read raw gyro data
//    gx = dof.calcGyro(dof.gx) - gbias[0];   // Convert to degrees per seconds, remove gyro biases
//    gy = dof.calcGyro(dof.gy) - gbias[1];
//    gz = dof.calcGyro(dof.gz) - gbias[2];
//  }
//
//  if(digitalRead(INT1XM)) {  // When new accelerometer data is ready
//    dof.readAccel();         // Read raw accelerometer data
//    ax = dof.calcAccel(dof.ax) - abias[0];   // Convert to g's, remove accelerometer biases
//    ay = dof.calcAccel(dof.ay) - abias[1];
//    az = dof.calcAccel(dof.az) - abias[2];
//  }
//
//  if(digitalRead(INT2XM)) {  // When new magnetometer data is ready
//    dof.readMag();           // Read raw magnetometer data
//    mx = dof.calcMag(dof.mx);     // Convert to Gauss and correct for calibration
//    my = dof.calcMag(dof.my);
//    mz = dof.calcMag(dof.mz);
//
//    dof.readTemp();
//    temperature = 21.0 + (float) dof.temperature/8.; // slope is 8 LSB per degree C, just guessing at the intercept
//  }
//
//}

//void initializeIMU(){
//
//  uint32_t status = dof.begin();
//  delay(2000);
//
//  // Set data output ranges; choose lowest ranges for maximum resolution
//  // Accelerometer scale can be: A_SCALE_2G, A_SCALE_4G, A_SCALE_6G, A_SCALE_8G, or A_SCALE_16G
//  dof.setAccelScale(dof.A_SCALE_2G);
//  // Gyro scale can be:  G_SCALE__245, G_SCALE__500, or G_SCALE__2000DPS
//  dof.setGyroScale(dof.G_SCALE_245DPS);
//  // Magnetometer scale can be: M_SCALE_2GS, M_SCALE_4GS, M_SCALE_8GS, M_SCALE_12GS
//  dof.setMagScale(dof.M_SCALE_2GS);
//
//  // Set output data rates
//  // Accelerometer output data rate (ODR) can be: A_ODR_3125 (3.225 Hz), A_ODR_625 (6.25 Hz), A_ODR_125 (12.5 Hz), A_ODR_25, A_ODR_50,
//  //                                              A_ODR_100,  A_ODR_200, A_ODR_400, A_ODR_800, A_ODR_1600 (1600 Hz)
//  dof.setAccelODR(dof.A_ODR_200); // Set accelerometer update rate at 100 Hz
//  // Accelerometer anti-aliasing filter rate can be 50, 194, 362, or 763 Hz
//  // Anti-aliasing acts like a low-pass filter allowing oversampling of accelerometer and rejection of high-frequency spurious noise.
//  // Strategy here is to effectively oversample accelerometer at 100 Hz and use a 50 Hz anti-aliasing (low-pass) filter frequency
//  // to get a smooth ~150 Hz filter update rate
//  dof.setAccelABW(dof.A_ABW_50); // Choose lowest filter setting for low noise
//
//  // Gyro output data rates can be: 95 Hz (bandwidth 12.5 or 25 Hz), 190 Hz (bandwidth 12.5, 25, 50, or 70 Hz)
//  //                                 380 Hz (bandwidth 20, 25, 50, 100 Hz), or 760 Hz (bandwidth 30, 35, 50, 100 Hz)
//  dof.setGyroODR(dof.G_ODR_190_BW_125);  // Set gyro update rate to 190 Hz with the smallest bandwidth for low noise
//
//  // Magnetometer output data rate can be: 3.125 (ODR_3125), 6.25 (ODR_625), 12.5 (ODR_125), 25, 50, or 100 Hz
//  dof.setMagODR(dof.M_ODR_125); // Set magnetometer to update every 80 ms
//
//  // Use the FIFO mode to average accelerometer and gyro readings to calculate the biases, which can then be removed from
//  // all subsequent measurements.
//  dof.calLSM9DS0(gbias, abias);
//
//}
//
void LcdWriteString(char *characters){
  while(*characters) LcdWriteCharacter(*characters++);
}

void LcdWriteCharacter(char character){
  for (int i = 0; i < 5; i++) 
  {
    LcdWriteData(ASCII[character - 0x20][i]);
  }
  LcdWriteData(0x00);
  }

void LcdWriteData(byte dat)
{
  digitalWrite(DC, HIGH);
  digitalWrite(CE, LOW);
  shiftOut(DIN, CLK, MSBFIRST, dat);
  digitalWrite(CE, HIGH);
}

void LcdWriteCmd(byte cmd)
{
  digitalWrite(DC, LOW);                 //DC pin is low for commands
  digitalWrite(CE, LOW);                                
  shiftOut(DIN, CLK, MSBFIRST, cmd);     //transmit serial data
  digitalWrite(CE, HIGH);
}

void LcdXY(int x, int y){
  LcdWriteCmd(0x80 | x);
  LcdWriteCmd(0x40 | y);
}

//void MahonyQuaternionUpdate(float ax, float ay, float az, float gx, float gy, float gz, float mx, float my, float mz){
//
//  float q1 = q[0], q2 = q[1], q3 = q[2], q4 = q[3];   // short name local variable for readability
//  float norm;
//  float hx, hy, bx, bz;
//  float vx, vy, vz, wx, wy, wz;
//  float ex, ey, ez;
//  float pa, pb, pc;
//
//  // Auxiliary variables to avoid repeated arithmetic
//  float q1q1 = q1 * q1;
//  float q1q2 = q1 * q2;
//  float q1q3 = q1 * q3;
//  float q1q4 = q1 * q4;
//  float q2q2 = q2 * q2;
//  float q2q3 = q2 * q3;
//  float q2q4 = q2 * q4;
//  float q3q3 = q3 * q3;
//  float q3q4 = q3 * q4;
//  float q4q4 = q4 * q4;
//
//  // Normalise accelerometer measurement
//  norm = sqrt(ax * ax + ay * ay + az * az);
//  if (norm == 0.0f) return; // handle NaN
//  norm = 1.0f / norm;        // use reciprocal for division
//  ax *= norm;
//  ay *= norm;
//  az *= norm;
//
//  // Normalise magnetometer measurement
//  norm = sqrt(mx * mx + my * my + mz * mz);
//  if (norm == 0.0f) return; // handle NaN
//  norm = 1.0f / norm;        // use reciprocal for division
//  mx *= norm;
//  my *= norm;
//  mz *= norm;
//
//  // Reference direction of Earth's magnetic field
//  hx = 2.0f * mx * (0.5f - q3q3 - q4q4) + 2.0f * my * (q2q3 - q1q4) + 2.0f * mz * (q2q4 + q1q3);
//  hy = 2.0f * mx * (q2q3 + q1q4) + 2.0f * my * (0.5f - q2q2 - q4q4) + 2.0f * mz * (q3q4 - q1q2);
//  bx = sqrt((hx * hx) + (hy * hy));
//  bz = 2.0f * mx * (q2q4 - q1q3) + 2.0f * my * (q3q4 + q1q2) + 2.0f * mz * (0.5f - q2q2 - q3q3);
//
//  // Estimated direction of gravity and magnetic field
//  vx = 2.0f * (q2q4 - q1q3);
//  vy = 2.0f * (q1q2 + q3q4);
//  vz = q1q1 - q2q2 - q3q3 + q4q4;
//  wx = 2.0f * bx * (0.5f - q3q3 - q4q4) + 2.0f * bz * (q2q4 - q1q3);
//  wy = 2.0f * bx * (q2q3 - q1q4) + 2.0f * bz * (q1q2 + q3q4);
//  wz = 2.0f * bx * (q1q3 + q2q4) + 2.0f * bz * (0.5f - q2q2 - q3q3);
//
//  // Error is cross product between estimated direction and measured direction of gravity
//  ex = (ay * vz - az * vy) + (my * wz - mz * wy);
//  ey = (az * vx - ax * vz) + (mz * wx - mx * wz);
//  ez = (ax * vy - ay * vx) + (mx * wy - my * wx);
//  if (Ki > 0.0f)
//  {
//    eInt[0] += ex;      // accumulate integral error
//    eInt[1] += ey;
//    eInt[2] += ez;
//  }
//  else
//  {
//    eInt[0] = 0.0f;     // prevent integral wind up
//    eInt[1] = 0.0f;
//    eInt[2] = 0.0f;
//  }
//
//  // Apply feedback terms
//  gx = gx + Kp * ex + Ki * eInt[0];
//  gy = gy + Kp * ey + Ki * eInt[1];
//  gz = gz + Kp * ez + Ki * eInt[2];
//
//  // Integrate rate of change of quaternion
//  pa = q2;
//  pb = q3;
//  pc = q4;
//  q1 = q1 + (-q2 * gx - q3 * gy - q4 * gz) * (0.5f * deltat);
//  q2 = pa + (q1 * gx + pb * gz - pc * gy) * (0.5f * deltat);
//  q3 = pb + (q1 * gy - pa * gz + pc * gx) * (0.5f * deltat);
//  q4 = pc + (q1 * gz + pa * gy - pb * gx) * (0.5f * deltat);
//
//  // Normalise quaternion
//  norm = sqrt(q1 * q1 + q2 * q2 + q3 * q3 + q4 * q4);
//  norm = 1.0f / norm;
//  q[0] = q1 * norm;
//  q[1] = q2 * norm;
//  q[2] = q3 * norm;
//  q[3] = q4 * norm;
//
//}
