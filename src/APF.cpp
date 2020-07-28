#include <artificial_potential_fields/APF.h>

void odometryCallback(const nav_msgs::OdometryConstPtr& odometry_msg){
    //tf::Quaternion q(odometry_msg->pose.pose.orientation.x, odometry_msg->pose.pose.orientation.y, odometry_msg->pose.pose.orientation.z, odometry_msg->pose.pose.orientation.w);
    //tf::Matrix3x3 m(q);
    //double roll, pitch, yaw;
    //m.getRPY(roll, pitch, yaw);
    //pose << odometry_msg->pose.pose.position.x, odometry_msg->pose.pose.position.y, odometry_msg->pose.pose.position.z, yaw;

    speed = sqrt(pow(odometry_msg->twist.twist.linear.x, 2) + pow(odometry_msg->twist.twist.linear.y, 2));
    height = odometry_msg->pose.pose.position.z;

    Eigen::Quaternionf quaternion(odometry_msg->pose.pose.orientation.w, odometry_msg->pose.pose.orientation.x, odometry_msg->pose.pose.orientation.y, odometry_msg->pose.pose.orientation.z);
    R = quaternion.toRotationMatrix();

    /*Vector3f euler = quaternion.toRotationMatrix().eulerAngles(0, 1, 2);
    float roll = euler(0);
    float pitch = euler(1);
    yaw = euler(2);
    cout << "[APF] attitude = [" << roll << ", \t" << pitch << ", \t" << yaw << "]" << endl;*/
}

void attractiveVelocityCallback(const geometry_msgs::TwistStamped& command_msg){
    velocity_d << command_msg.twist.linear.x, command_msg.twist.linear.y, command_msg.twist.linear.z, command_msg.twist.angular.z;
}

void laserCallback(const sensor_msgs::LaserScan::ConstPtr& scan){
    sensor_msgs::PointCloud2 msg_cloud;
    projector.projectLaser(*scan, msg_cloud);
    pcl::fromROSMsg(msg_cloud, *cloud);
}

void dynamicReconfigureCallback(artificial_potential_fields::setAPFConfig &config, uint32_t level){
    k_repulsive = config.k_repulsive;
    eta_0 = config.eta_0;
}

// Constructor
APF::APF(int argc, char** argv){
    ros::init(argc, argv, "APF");
    ros::NodeHandle node_handle;

    // Subscribers
    odometry_subscriber = node_handle.subscribe("/uav/odometry", 1, odometryCallback);
    attractive_velocity_subscriber = node_handle.subscribe("/uav/attractive_velocity", 1, attractiveVelocityCallback);
    laser_subscriber = node_handle.subscribe("/laser/scan", 1, laserCallback);

    // Publishers
    force_publisher = node_handle.advertise<geometry_msgs::TwistStamped>("/potential_fields/velocity", 1);
    attractive_publisher = node_handle.advertise<geometry_msgs::Vector3>("/potential_fields/attractive", 1); // for debug
    repulsive_publisher = node_handle.advertise<geometry_msgs::Vector3>("/potential_fields/repulsive", 1); // for debug
    point_cloud_publisher = node_handle.advertise<pcl::PointCloud<pcl::PointXYZ>>("/point_cloud", 1); // for debug

    /*tf::TransformListener tfListener;
    tfListener.setExtrapolationLimit(ros::Duration(0.1));*/

    if(!node_handle.getParam("/potential_fields/UAV_radius", UAV_radius))
        ROS_WARN_STREAM("[APF] Parameter 'UAV_radius' not defined!");
    //ROS_INFO_STREAM("[APF] UAV_radius = " << UAV_radius);
}

// Destructor
APF::~APF(){
    ros::shutdown();
    exit(0);
}

void APF::run(){
    dynamic_reconfigure::Server<artificial_potential_fields::setAPFConfig> server;
    dynamic_reconfigure::Server<artificial_potential_fields::setAPFConfig>::CallbackType f;
    f = boost::bind(&dynamicReconfigureCallback, _1, _2);
    server.setCallback(f);

    Vector3f force;                                  //
    Vector3f repulsive_force;

    ros::Rate rate(100);
    while(ros::ok()){
        rate.sleep();
        ros::spinOnce();

        //tf_listener->waitForTransform("/world", (*pcl_in).header.frame_id, (*pcl_in).header.stamp, ros::Duration(5.0));
        //pcl_ros::transformPointCloud("/world", *pcl_in, pcl_out, *tf_listener);

        /*Matrix4f rotation;
        rotation << R(0, 0), R(0, 1), R(0, 2), 0, R(1, 0), R(1, 1), R(1, 2), 0, R(2, 0), R(2, 1), R(2, 2), 0, 0, 0, 0, 1;
        pcl::transformPointCloud(*cloud, *cloud, rotation);
        cloud->header.frame_id = "map";*/

        // Filter the point cloud
        pcl::PassThrough<pcl::PointXYZ> pass;
        pass.setInputCloud(cloud);
        pass.setFilterFieldName("x");
        pass.setFilterLimits(0.3, 100); // filter the quadrotor's body
        //pass.filter(*cloud);
        pass.setFilterFieldName("z");
        pass.setFilterLimits(-height + 0.2, 100); // filter the floor; 0.02 is the treshold
        //pass.filter(*cloud);

        // Compute the attractive forces
        repulsive_force << 0, 0, 0;
        int points = 0;
        for(int i = 0; i < cloud->size(); ++i){
            Vector3f obstacle(cloud->points[i].x, cloud->points[i].y, cloud->points[i].z); // point obstacle
            float eta = distance(obstacle);
            if(eta < eta_0){
                obstacle -= obstacle/eta*UAV_radius; // move the UAV's center to the point on the sfere surrounding UAV and closest to the obstacle
                //repulsive_force += k_repulsive*pow(1/eta - 1/eta_0, 2)/2*obstacle/eta;
                repulsive_force += (1/eta - 1/eta_0)/pow(eta, 2)*obstacle;
                ++points;
            }
        }
        if(points){ // normalise repulsive force
            //repulsive_force /= points;
        }

        //msg.twist.linear.x = -cos(pose(3))*potential_velocity(0) + sin(pose(3))*potential_velocity(1);
        //msg.twist.linear.y = -sin(pose(3))*potential_velocity(0) - cos(pose(3))*potential_velocity(1);

        repulsive_force *= k_repulsive;
        repulsive_force(2) = 0;
        //repulsive_force = R*repulsive_force; // TODO: instead use TF to transform from lidar frame to global frame

        force = velocity_d.head(3) - repulsive_force;

        geometry_msgs::TwistStamped msg;
        msg.twist.linear.x = force(0);
        msg.twist.linear.y = force(1);
        msg.twist.linear.z = velocity_d(2);
        msg.twist.angular.z = velocity_d(3);
        force_publisher.publish(msg);

        geometry_msgs::Vector3 debug_msg;
        debug_msg.x = velocity_d(0);
        debug_msg.y = velocity_d(1);
        debug_msg.z = velocity_d(2);
        attractive_publisher.publish(debug_msg);
        debug_msg.x = -repulsive_force(0);
        debug_msg.y = -repulsive_force(1);
        debug_msg.z = 0;
        repulsive_publisher.publish(debug_msg);

        point_cloud_publisher.publish(cloud);
    }
}

double APF::distance(Vector3f v){
    return sqrt(pow(v(0), 2) + pow(v(1), 2) + pow(v(2), 2));
}

int main(int argc, char** argv){
    cout << "[APF] Artificial potential fields running..." << endl;

    APF* apf = new APF(argc, argv);
    apf->run();
}
