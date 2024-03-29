#include <ros/ros.h>
#include <geometry_msgs/Twist.h>
#include <geometry_msgs/Pose.h>
#include <nav_msgs/Odometry.h>
#include <tf/transform_datatypes.h>
#include <std_msgs/Float32.h>
#include <std_msgs/Int32.h>
#include <vector>
#include <cmath>     /* abs */

/*
 * StateEstimation class (constructor)
 *
 * This constructor does all the initializations for the StateEstimation node. The private part for things that only this class will have access.
 *
 * @author  		Cees Trouwborst <ceestrouwborst@gmail.com>
 * @date    		JLY 2014
 */
class StateEstimation {
private:
	ros::NodeHandle node_handle_;
	ros::Subscriber pose_subscriber_;
	ros::Publisher pose_publisher_;
	ros::Publisher vel_publisher_;
	ros::Publisher odom_publisher_;
	ros::Publisher pitch_publisher_; // Just for testing!
	ros::Publisher foaw_publisher_;

	geometry_msgs::Pose prev_pose_;

	std::vector<geometry_msgs::Pose> poses_; // Previous poses
	std::vector<double> times_; // Corresponding times
	int pose_memory_; // Max poses stored
	double uncertainty_band_; // Max band around pose to find line fitting

	geometry_msgs::Pose pose_;
	geometry_msgs::Twist prev_vel_;
	geometry_msgs::Twist vel_;
	double prev_time_;
	double prev_yaw_; // State filter estimation

	double T_, dt_, K_, rate_x_, rate_y_, rate_z_, rate_yaw_;

	/*
	 * StateEstimation class (constructor)
	 *
	 * This constructor does all the initializations for the StateEstimation node. The public part for things that are accessible to all classes.
	 *
	 * @author  		Cees Trouwborst <ceestrouwborst@gmail.com>
	 * @date    		JLY 2014
	 */
public:
	StateEstimation() {
		ros::NodeHandle params("~");

		T_ = 0.2; //sec
		dt_ = 0.02; //ms
		K_ = 100;
		pose_memory_ = 15;
		uncertainty_band_ = 0.01;
		params.getParam("K", K_);
		params.getParam("pose_memory", pose_memory_);
		params.getParam("uncertainty_band", uncertainty_band_);

		// Set previous time to zero
		prev_time_ = 0;

		pose_subscriber_ = node_handle_.subscribe("/Apm/unfiltered_pose", 1,
				&StateEstimation::poseCallback, this);
		pose_publisher_ = node_handle_.advertise<geometry_msgs::Pose>(
				"filtered_pose", 1);
		vel_publisher_ = node_handle_.advertise<geometry_msgs::Twist>(
				"filtered_vel", 1);
		odom_publisher_ = node_handle_.advertise<nav_msgs::Odometry>(
				"filtered_state", 1);
		pitch_publisher_ = node_handle_.advertise<std_msgs::Float32>("pitch",
				1);
		foaw_publisher_ = node_handle_.advertise<std_msgs::Int32>("foaw", 1);
	}

	~StateEstimation() {
	}

	/*
	 * setpointCallback
	 *
	 * Function called when the topic /Apm/unfiltered_pose (actually the
	 * name of the topic that you set under mocap_optitrack config file)
	 * changes. This function gives an estimation of the current state of
	 * the vehicle.
	 *
	 * @param			<geometry_msgs::Pose> pose from topic /Apm/unfiltered_pose.
	 * @author  		Cees Trouwborst <ceestrouwborst@gmail.com>
	 * @date    		JLY 2014
	 */
	void poseCallback(const geometry_msgs::Pose pose) {
		nav_msgs::Odometry odom;
		odom.header.stamp = ros::Time::now();
		odom.header.frame_id = "filtered_state";
		// Do not filter pose for now
		odom.pose.pose = pose;

		/* PSEUDO CODE FOAW */

		// Get time
		double current_time;
		current_time = ros::Time::now().toSec();

		// Store the latest pose in a poses vector
		poses_.push_back(pose);
		times_.push_back(current_time);

		if (poses_.size() > 1) {
			// We can calculate a speed!
			// We are never going to use more poses than pose_memory, so check for that
			if (poses_.size() > pose_memory_) {
				poses_.erase(poses_.begin());
				times_.erase(times_.begin());
			}

			// At this point, we know the final number of poses available
			int n;
			n = poses_.size();

			// FOAW outer loop
			// In this loop, we check if, given a certain beginning of the window, all points fit the line.
			// Loops over a set of possible steps back.
			int samplesBack;
			bool optimalSampleReached;
			double windowSpeed_x, windowSpeed_y, windowSpeed_z;
			optimalSampleReached = false;
			samplesBack = 1;
			while (!optimalSampleReached) {
				windowSpeed_x = (poses_[n - 1].position.x
						- poses_[n - 1 - samplesBack].position.x)
						/ (times_[n - 1] - times_[n - 1 - samplesBack]);
				windowSpeed_y = (poses_[n - 1].position.y
						- poses_[n - 1 - samplesBack].position.y)
						/ (times_[n - 1] - times_[n - 1 - samplesBack]);
				windowSpeed_z = (poses_[n - 1].position.z
						- poses_[n - 1 - samplesBack].position.z)
						/ (times_[n - 1] - times_[n - 1 - samplesBack]);

				// At this point, we have a line through the two poins we know (at last and last - steps back). We do not have to check those. Check all points inbetween
				bool allInBand;
				int i;
				allInBand = true;
				for (i = n - 1 - samplesBack + 1; i < n - 1; i++) {
					if ((std::abs(
							poses_[n - 1 - samplesBack].position.x
									+ windowSpeed_x
											* (times_[i]
													- times_[n - 1 - samplesBack]))
							> std::abs(poses_[i].position.x + uncertainty_band_))
							&& (std::abs(
									poses_[n - 1 - samplesBack].position.y
											+ windowSpeed_y
													* (times_[i]
															- times_[n - 1
																	- samplesBack]))
									> std::abs(
											poses_[i].position.y
													+ uncertainty_band_))
							&& (std::abs(
									poses_[n - 1 - samplesBack].position.z
											+ windowSpeed_z
													* (times_[i]
															- times_[n - 1
																	- samplesBack]))
									> std::abs(
											poses_[i].position.z
													+ uncertainty_band_))) {
						// If this happens, we can state that we cannot use this frame. End loop
						allInBand = false;
						break;
					}
				}

				// this many steps back still worked. Awesome! Try one more if possible
				if (allInBand && n > samplesBack + 1) {
					samplesBack++;
					if (samplesBack > pose_memory_) {
						optimalSampleReached = true;
					}
				} else {
					// Too bad, we have to use this many steps back. Exit loop!
					optimalSampleReached = true;
				}
			}
			if (n > 10) {
				samplesBack = 10;
			}
			// At this point, we now how many points we can look back. Calculate final speed
			vel_.linear.x = (poses_[n - 1].position.x
					- poses_[n - 1 - samplesBack + 1].position.x)
					/ (times_[n - 1] - times_[n - 1 - samplesBack + 1]);
			vel_.linear.y = (poses_[n - 1].position.y
					- poses_[n - 1 - samplesBack + 1].position.y)
					/ (times_[n - 1] - times_[n - 1 - samplesBack + 1]);
			vel_.linear.z = (poses_[n - 1].position.z
					- poses_[n - 1 - samplesBack + 1].position.z)
					/ (times_[n - 1] - times_[n - 1 - samplesBack + 1]);

			std_msgs::Int32 fb;
			fb.data = samplesBack + 1;
			foaw_publisher_.publish(fb);

			// Publish.
			odom.twist.twist = vel_;
			vel_publisher_.publish(vel_);
			pose_publisher_.publish(pose);
			odom_publisher_.publish(odom);
		} else {
			// No speed known. Do not publish anything.
		}
	}

	/*
	 * lowPassFilter
	 *
	 * Taken from http://en.wikipedia.org/wiki/Low-pass_filter
	 *
	 * @author  		Cees Trouwborst <ceestrouwborst@gmail.com>
	 * @date    		JLY 2014
	 */
	double lowPassFilter(double x, double y0, double dt, double T)
			{
		double res = y0 + (x - y0) * (dt_ / (dt_ + T_));
		return res;
	}
};

int main(int argc, char **argv) {
	ros::init(argc, argv, "state_estimation");
	StateEstimation state_estimation;
	ros::spin();

	return 0;
}
