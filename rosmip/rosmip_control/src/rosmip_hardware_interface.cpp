#include <rosmip_control/rosmip_hardware_interface.h>


#define __NAME "rosmip_hardware_interface"
const std::string joint_name_[NB_JOINTS] = {"left_wheel_joint","right_wheel_joint"};



// mechanics
#define GEARBOX                         75.81  //35.577
#define ENCODER_RES                     12     //60
#define WHEEL_RADIUS_M                  0.03   //0.034
#define TRACK_WIDTH_M                   0.0415 //0.035

// electrical hookups
#define MOTOR_CHANNEL_L                 2
#define MOTOR_CHANNEL_R                 1
#define MOTOR_POLARITY_L               -1
#define MOTOR_POLARITY_R                1
#define ENCODER_CHANNEL_L               2
#define ENCODER_CHANNEL_R               1
#define ENCODER_POLARITY_L             -1
#define ENCODER_POLARITY_R              1

// IMU
#define SAMPLE_RATE_HZ 100
#define DT 0.01	

void test_imu_interrupt() {
  //printf("blaaa\n");
}





RosMipHardwareInterface::RosMipHardwareInterface()
{
  ROS_INFO_STREAM_NAMED(__NAME, "in RosMipHardwareInterface::RosMipHardwareInterface...");

  ROS_INFO_STREAM_NAMED(__NAME, "Registering interfaces");
  // register joints
  for (int i=0; i<NB_JOINTS; i++) {
    joint_position_[i] = 0.;
    joint_velocity_[i] = 0.;
    joint_effort_[i] = 0.;
    joint_effort_command_[i] = 0.;
    js_interface_.registerHandle(hardware_interface::JointStateHandle(
        joint_name_[i], &joint_position_[i], &joint_velocity_[i], &joint_effort_[i]));
    ej_interface_.registerHandle(hardware_interface::JointHandle(
        js_interface_.getHandle(joint_name_[i]), &joint_effort_command_[i]));
  }
  registerInterface(&js_interface_);
  registerInterface(&ej_interface_);

  // register IMU
  imu_data_.name = "imu";
  imu_data_.frame_id = "imu_link";
  imu_data_.orientation = imu_orientation_;
  imu_data_.angular_velocity = imu_angular_velocity_;
  imu_data_.linear_acceleration = imu_linear_acceleration_;
  hardware_interface::ImuSensorHandle imu_sensor_handle(imu_data_);
  imu_sensor_interface_.registerHandle(imu_sensor_handle);
  registerInterface(&imu_sensor_interface_);
 
}

/*******************************************************************************
 *
 *
 *******************************************************************************/
bool RosMipHardwareInterface::start() {

  if(rc_initialize()){
    ROS_ERROR("in RosMipHardwareInterface::start: failed to initialize robotics cape");
    return false;
  }

  rc_imu_config_t imu_config = rc_default_imu_config();
  imu_config.dmp_sample_rate = SAMPLE_RATE_HZ;
  // ORIENTATION_Z_UP
  // ORIENTATION_Z_DOWN
  // ORIENTATION_X_UP
  // ORIENTATION_X_DOWN
  // ORIENTATION_Y_UP
  // ORIENTATION_Y_DOWN
  // ORIENTATION_X_FORWARD
  // ORIENTATION_X_BACK
  imu_config.orientation = ORIENTATION_Y_UP; // WTF!!! Not sure what that means...
  if(rc_initialize_imu_dmp(&rc_imu_data_, imu_config)){
    ROS_ERROR("in RosMipHardwareInterface::start: can't talk to IMU, all hope is lost\n");
    //rc_blink_led(RED, 5, 5);
    return false;
  }

  // start dsm listener
  rc_initialize_dsm();
  
  //rc_set_imu_interrupt_func(&test_imu_interrupt);
  rc_set_state(RUNNING);
  //rc_enable_motors();
  return true;
}


bool RosMipHardwareInterface::shutdown() {
  ROS_INFO("in RosMipHardwareInterface::shutdown");
  rc_power_off_imu();
  rc_cleanup();

  return true;
}


RosMipHardwareInterface::~RosMipHardwareInterface() {
  ROS_INFO(" ~RosMipHardwareInterface");
  // TODO make sure this is called
}

void RosMipHardwareInterface::read() {
  //ROS_INFO(" read HW");
  double left_wheel_angle = rc_get_encoder_pos(ENCODER_CHANNEL_L) * 2 * M_PI / (ENCODER_POLARITY_L * GEARBOX * ENCODER_RES);
  double right_wheel_angle = rc_get_encoder_pos(ENCODER_CHANNEL_R) * 2 * M_PI / (ENCODER_POLARITY_R * GEARBOX * ENCODER_RES);
  joint_velocity_[0] = (left_wheel_angle - joint_position_[0]) / DT;
  joint_velocity_[1] = (right_wheel_angle - joint_position_[1]) / DT;
  joint_position_[0] = left_wheel_angle;
  joint_position_[1] = right_wheel_angle;

  // TODO acquire mutex
  // imu_orientation is in the order of geometry_msg, ie x, y, z, w
  // wheras dmp_quat is w, x, y, z
  imu_orientation_[0] = rc_imu_data_.dmp_quat[1];
  imu_orientation_[1] = rc_imu_data_.dmp_quat[2];
  imu_orientation_[2] = rc_imu_data_.dmp_quat[3];
  imu_orientation_[3] = rc_imu_data_.dmp_quat[0];

  imu_angular_velocity_[0] = rc_imu_data_.gyro[0]/180.*M_PI; // WTF are those units !!!
  imu_angular_velocity_[1] = rc_imu_data_.gyro[1]/180.*M_PI;
  imu_angular_velocity_[2] = rc_imu_data_.gyro[2]/180.*M_PI; 

  imu_linear_acceleration_[0] = rc_imu_data_.accel[0];
  imu_linear_acceleration_[1] = rc_imu_data_.accel[1];
  imu_linear_acceleration_[2] = rc_imu_data_.accel[2];
  
  //ROS_INFO(" read HW %f %f %f %f", imu_orientation_[0], imu_orientation_[1], imu_orientation_[2], imu_orientation_[3]);
  //ROS_INFO(" read HW %f", rc_get_dsm_ch_normalized(1));
  
}

void RosMipHardwareInterface::write() {
  //ROS_INFO(" write HW");
  //const float gain = 100.;
  float dutyL =  joint_effort_command_[0];
  float dutyR =  joint_effort_command_[1];
  //ROS_INFO(" write HW %f %f %f", joint_effort_command_[0], dutyL, dutyR);
  
  rc_set_motor(MOTOR_CHANNEL_L, MOTOR_POLARITY_L * dutyL);
  rc_set_motor(MOTOR_CHANNEL_R, MOTOR_POLARITY_R * dutyR);
  
}

#include <controller_manager/controller_manager.h>

int main(int argc, char** argv)
{
  ros::init(argc, argv, __NAME);
  ROS_INFO_STREAM_NAMED(__NAME, "ROSMIP Hardware node starting...");
  ros::AsyncSpinner spinner(1);
  spinner.start();
 
  RosMipHardwareInterface hw;
  if (!hw.start()) {
    ROS_ERROR_STREAM_NAMED(__NAME, "Failed to initialize hardware. bailling out...");
    return -1;
  }

  ros::NodeHandle nh;
  controller_manager::ControllerManager cm(&hw, nh);
  ros::Duration period(DT);
  while (ros::ok() && rc_get_state()!=EXITING)
  {
    //ROS_INFO("loop");
    hw.read();
    cm.update(ros::Time::now(), period);
    hw.write();
    period.sleep();
  }
  hw.shutdown();
  ROS_INFO_STREAM_NAMED(__NAME, "ROSMIP Hardware node exiting...");
  return 0;
}
