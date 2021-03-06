#include <artificial_potential_fields/APF_aim.h>

void odometryCallback(const nav_msgs::OdometryConstPtr& odometry_msg){
    //tf::Quaternion q(odometry_msg->pose.pose.orientation.x, odometry_msg->pose.pose.orientation.y, odometry_msg->pose.pose.orientation.z, odometry_msg->pose.pose.orientation.w);
    //tf::Matrix3x3 m(q);
    //double roll, pitch, yaw;
    //m.getRPY(roll, pitch, yaw);
    //pose << odometry_msg->pose.pose.position.x, odometry_msg->pose.pose.position.y, odometry_msg->pose.pose.position.z, yaw;

    speed = sqrt(pow(odometry_msg->twist.twist.linear.x, 2) + pow(odometry_msg->twist.twist.linear.y, 2));
    position << odometry_msg->pose.pose.position.x, odometry_msg->pose.pose.position.y, odometry_msg->pose.pose.position.z; // use this!
    height = odometry_msg->pose.pose.position.z;

    Eigen::Quaternionf quaternion(odometry_msg->pose.pose.orientation.w, odometry_msg->pose.pose.orientation.x, odometry_msg->pose.pose.orientation.y, odometry_msg->pose.pose.orientation.z);
    R = quaternion.toRotationMatrix();
    //rotation << R(0, 0), R(0, 1), R(0, 2), 0, R(1, 0), R(1, 1), R(1, 2), 0, R(2, 0), R(2, 1), R(2, 2), odometry_msg->pose.pose.position.z, 0, 0, 0, 1;

    /*Vector3f euler = quaternion.toRotationMatrix().eulerAngles(0, 1, 2);
    float roll = euler(0);
    float pitch = euler(1);
    yaw = euler(2);
    cout << "[APF] attitude = [" << roll << ", \t" << pitch << ", \t" << yaw << "]" << endl;*/


}

//void attractiveVelocityCallback(const geometry_msgs::TwistStamped& command_msg){
//    velocity_d << command_msg.twist.linear.x, command_msg.twist.linear.y, command_msg.twist.linear.z, command_msg.twist.angular.z;
//}


//double distance(pcl::PointXYZ p){
//    return sqrt(pow(p.x, 2) + pow(p.y, 2) + pow(p.z, 2));
//}


// Newly added filter sphere from last time
//void filterSphere(pcl::PointCloud<pcl::PointXYZ>::Ptr cloud){
//    pcl::PointIndices::Ptr inliers(new pcl::PointIndices());
//    pcl::ExtractIndices<pcl::PointXYZ> extract;
    //ROS_INFO_STREAM("[APF] cloud->size() = " << cloud->size());
//    for(int i = 0; i < cloud->size(); ++i)
  //      if(distance(cloud->at(i)) <= UAV_radius)
    //        inliers->indices.push_back(i);
//    extract.setInputCloud(cloud);
//    extract.setIndices(inliers);
//    extract.setNegative(true);
//    extract.filter(*cloud);
    //ROS_INFO_STREAM("[APF] inliers->indices.size() = " << inliers->indices.size());
    //ROS_INFO_STREAM("[APF] cloud->size() = " << cloud->size());
//}


//void bodyrateCallback(const mavros_msgs::AttitudeTarget& command_msg){
//    attitude_d << command_msg.body_rate.x, command_msg.body_rate.y, command_msg.body_rate.z, command_msg.thrust;
//}

void blastCallback(const geometry_msgs::Vector3& command_msg){
    pose_d << command_msg.x, command_msg.y, command_msg.z;
}

void laserCallback(const sensor_msgs::LaserScan::ConstPtr& scan){
    sensor_msgs::PointCloud2 msg_cloud;
    projector.projectLaser(*scan, msg_cloud);
    pcl::fromROSMsg(msg_cloud, *cloud);

    //filterSphere(cloud); // filters out points inside the bounding sphere

    transformation << R(0, 0), R(0, 1), R(0, 2), position(0), R(1, 0), R(1, 1), R(1, 2), position(1), R(2, 0), R(2, 1), R(2, 2), height, 0, 0, 0, 1;
    pcl::transformPointCloud(*cloud, *cloud, transformation); // rotates the cloud the world frame
    cloud->header.frame_id = "map"; // pseudo map (x and y translation invariant)

    // Filter the point cloud
    pcl::PassThrough<pcl::PointXYZ> pass;
    pass.setInputCloud(cloud);
    pass.setFilterFieldName("z");
    pass.setFilterLimits(0.2, 2); // filter the floor; 0.1 is the treshold
    pass.filter(*cloud);
}

void dynamicReconfigureCallback(artificial_potential_fields::setAPFConfig &config, uint32_t level){
    k_repulsive = config.k_repulsive;
    eta_0 = config.eta_0;
}

// Constructor
APF_aim::APF_aim(int argc, char** argv){
    ros::init(argc, argv, "APF");
    ros::NodeHandle node_handle;

    // Subscribers
    odometry_subscriber = node_handle.subscribe("/mavros/local_position/odom", 1, odometryCallback);
    //attractive_velocity_subscriber = node_handle.subscribe("mavros/local_position/velocity_body", 1, attractiveVelocityCallback);
    //attitude_subscriber = node_handle.subscribe("/geo_command/bodyrate_command", 1, bodyrateCallback);
    position_subscriber = node_handle.subscribe("/pose3dk", 1, blastCallback);
    laser_subscriber = node_handle.subscribe("/scan", 1, laserCallback); // changed from LaserScan to /scan

    // Publishers

    //force_publisher = node_handle.advertise<geometry_msgs::TwistStamped>("/potential_fields/velocity", 1);
    //attitude_force_publisher = node_handle.advertise<mavros_msgs::AttitudeTarget>("/mavros/setpoint_raw/attitude", 1);

    position_force_publisher = node_handle.advertise<geometry_msgs::PoseStamped>("/mavros/setpoint_position/local", 1);

    attractive_publisher = node_handle.advertise<geometry_msgs::Vector3>("/potential_fields/attractive", 1); // for debug //can just refer to /geo_command topic
    repulsive_publisher = node_handle.advertise<geometry_msgs::Vector3>("/potential_fields/repulsive", 1); // for debug
    point_cloud_publisher = node_handle.advertise<pcl::PointCloud<pcl::PointXYZ> >("/point_cloud", 1); // for debug

    if(!node_handle.getParam("/potential_fields/UAV_radius", UAV_radius))
        ROS_WARN_STREAM("[APF] Parameter 'UAV_radius' not defined!");
    //ROS_INFO_STREAM("[APF] UAV_radius = " << UAV_radius);

    //rotation = MatrixXf::Identity(4, 4);
    transformation = MatrixXf::Identity(4, 4);
}

// Destructor
APF_aim::~APF_aim(){
    ros::shutdown();
    exit(0);
}

void APF_aim::run(){
    dynamic_reconfigure::Server<artificial_potential_fields::setAPFConfig> server;
    dynamic_reconfigure::Server<artificial_potential_fields::setAPFConfig>::CallbackType f;
    f = boost::bind(&dynamicReconfigureCallback, _1, _2);
    server.setCallback(f);

    Vector3f resultant_rate; //[r p y]
    //Vector3f force;
    Vector3f repulsive_force;

    ros::Rate rate(100); // if rate increases, the a copy ofthe cloud has to made and process the copy (otherwise callback will overwrite the cloud)
    while(ros::ok()){
        rate.sleep();
        ros::spinOnce();

        // Compute the attractive forces
        repulsive_force << 0, 0, 0;
        int points = 0;
        for(int i = 0; i < cloud->size(); ++i){
            Vector3f obstacle(cloud->points[i].x - position(0), cloud->points[i].y - position(1), 0); // point obstacle
            float eta = distance(obstacle);
            if(eta < eta_0){// does not consider points further than eta_0 AND closer than UAV_radius (inside the bounding sphere)
                obstacle -= obstacle/eta*UAV_radius; // move the UAV's center to the point on the sphere surrounding UAV and closest to the obstacle
                //repulsive_force += pow(1/eta - 1/eta_0, 2)/2*obstacle/eta;
                repulsive_force += (1/eta - 1/eta_0)/pow(eta, 2)*obstacle;
                ++points;
            }
        }
        if(points){ // normalise repulsive force
                //   repulsive_force /= points;
        }
        repulsive_force *= k_repulsive;//*(1 + speed); // need to adjust until when influenced on rate, the change isnt too drastic
        //repulsive_force(2) = 0;
        //repulsive_force = R*repulsive_force; // TODO: instead use TF to transform from lidar frame to global frame

        //force = velocity_d.head(3) - repulsive_force;

     //   geometry_msgs::TwistStamped force_msg;
     //   force_msg.header.stamp = ros::Time::now();
     //   force_msg.twist.linear.x = force(0);
     //   force_msg.twist.linear.y = force(1);
     //   force_msg.twist.linear.z = velocity_d(2);
     //   force_msg.twist.angular.z = velocity_d(3);
     //   force_publisher.publish(force_msg);

        resultant_rate(0) = pose_d(1) + repulsive_force(1); // resultant roll
        resultant_rate(1) = pose_d(0) + repulsive_force(0); // resultant pitch

        geometry_msgs::PoseStamped pose_msg;
        pose_msg.header.stamp = ros::Time::now();
        pose_msg.header.frame_id= "map";
        pose_msg.pose.position.x = resultant_rate(1);
        pose_msg.pose.position.y = resultant_rate(0);
        pose_msg.pose.position.z = pose_d(2); //ignore z alt rate
        position_force_publisher.publish(pose_msg);

        geometry_msgs::Vector3 debug_msg;
        debug_msg.x = pose_d(0);
        debug_msg.y = pose_d(1);
        debug_msg.z = pose_d(2);
        attractive_publisher.publish(debug_msg);
        debug_msg.x = repulsive_force(0);
        debug_msg.y = repulsive_force(1);
        debug_msg.z = 0;
        repulsive_publisher.publish(debug_msg);

        point_cloud_publisher.publish(cloud);
    }
}

double APF_aim::distance(Vector3f v){
    return sqrt(pow(v(0), 2) + pow(v(1), 2) + pow(v(2), 2));
}

int main(int argc, char** argv){
    cout << "[APF] Artificial potential fields running..." << endl;

    APF_aim* apf_aim = new APF_aim(argc, argv);
    apf_aim->run();
}