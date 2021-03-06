#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <algorithm>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"
#include "spline.h"


using namespace std;

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
int ClosestWaypoint(double x, double y, const vector<double> &maps_x, const vector<double> &maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}

int NextWaypoint(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2((map_y-y),(map_x-x));

	double angle = fabs(theta-heading);
  angle = min(2*pi() - angle, angle);

  if(angle > pi()/4)
  {
    closestWaypoint++;
  if (closestWaypoint == maps_x.size())
  {
    closestWaypoint = 0;
  }
  }

  return closestWaypoint;
}

// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, const vector<double> &maps_s, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int prev_wp = -1;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_waypoints_x.push_back(x);
  	map_waypoints_y.push_back(y);
  	map_waypoints_s.push_back(s);
  	map_waypoints_dx.push_back(d_x);
  	map_waypoints_dy.push_back(d_y);
  }

  int lane = 1; //0,1,2
  double lane_width =4.; // meters
  double vref = 0.; // mph, initialization
  double acc = 0.; // mph/s, initialization
  double max_acc=.224; //mph/s
  double max_speed=49.5; //mph
  double react_time = 0.5; //s
  
  h.onMessage([&lane,&lane_width,&vref,&acc,&max_acc,&max_speed,&react_time,&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
        	// Main car's localization Data
          	double car_x = j[1]["x"];
          	double car_y = j[1]["y"];
          	double car_s = j[1]["s"];
          	double car_d = j[1]["d"];
          	double car_yaw = j[1]["yaw"];
          	double car_speed = j[1]["speed"];

          	// Previous path data given to the Planner
          	auto previous_path_x = j[1]["previous_path_x"];
          	auto previous_path_y = j[1]["previous_path_y"];
          	// Previous path's end s and d values 
          	double end_path_s = j[1]["end_path_s"];
          	double end_path_d = j[1]["end_path_d"];

          	// Sensor Fusion Data, a list of all other cars on the same side of the road.
          	auto sensor_fusion = j[1]["sensor_fusion"];

          	json msgJson;
						
			int prev_npts = previous_path_x.size();
			
			double maneuver_distance=fmax(15,car_speed*.447*1.75); // empirically set
			
			vector<double> cost_traj{1e-5,0.,1e-5}; //slightly penalizing side lanes
			vector<double> max_speed_new = {max_speed,max_speed,max_speed};
			
			
			if(prev_npts>0)
			{
				car_s=end_path_s;
			}
			
			for(int i=0;i<sensor_fusion.size();i++)
			{
				float d = sensor_fusion[i][6];
				
				double vx = sensor_fusion[i][3];
				double vy = sensor_fusion[i][4];
				double check_speed = sqrt(vx*vx+vy*vy);
				double check_car_s = sensor_fusion[i][5];
					
				check_car_s += ((double)prev_npts*.02*check_speed);
				double dist=check_car_s-car_s;
				double delta_speed = check_speed-car_speed;
				double braking_dist = (pow(car_speed*.447,2)-pow(check_speed*.447,2))/(2*5); //5 m/s^2 = max braking acceleration
				
				double cost_dist_tmp = 0;
				double cost_speed_tmp = 0;
				double max_speed_tmp = max_speed;
				
				if(dist>0)
				{
					double min_dist_front = fmax(15,car_speed*.447*react_time+fmax(0,braking_dist)); //minimum+reaction space+braking distance
					cost_dist_tmp = sqrt(fmax(0,1-dist/min_dist_front));
					if(dist<min_dist_front)
					{
						cost_speed_tmp = fmax(0,1-check_speed/max_speed);  //penalizing speed<max_speed
						max_speed_tmp = fmin(max_speed,check_speed);
					}
				}
				else
				{
					double min_dist_rear = fmax(15,check_speed*.447*react_time+fmax(0,-braking_dist)); 
					cost_dist_tmp=sqrt(fmax(0,1+dist/min_dist_rear));
					if(abs(dist)<min_dist_rear)
					{
						cost_speed_tmp = fmin(1,fmax(0,(check_speed-car_speed)/10));  //penalizing speed<<check_speed
					}
				}
				
				double cost_tmp=fmax(cost_dist_tmp,cost_speed_tmp);
			
				
				//trajectory cost
			
				if(d<(lane_width*(1+lane)) && d>(lane_width*lane))
				{
					if(cost_tmp>cost_traj[lane])
					{
						cost_traj[lane]=cost_tmp;
						max_speed_new[lane]=max_speed_tmp;
					}
				}
				else if(d<(lane_width*lane) && d>(lane_width*(lane-1) && lane>0))
				{					
					if(cost_tmp>cost_traj[lane-1])
					{
						cost_traj[lane-1]=cost_tmp;
						max_speed_new[lane-1]=max_speed_tmp;
					}
				}
				else if(d<(lane_width*(lane+2)) && d>(lane_width*(lane+1) && lane<2))
				{
					if(cost_tmp>cost_traj[lane+1])
					{
						cost_traj[lane+1]=cost_tmp;
						max_speed_new[lane+1]=max_speed_tmp;
					}
				}
			}
			
			// excluding too far lanes
			if(lane==0)
			{
				cost_traj[2]=1.;
			}
			else if(lane==2)
			{
				cost_traj[0]=1.;
			}
			
			// choosing lane
			lane = std::distance(cost_traj.begin(),std::min_element( cost_traj.begin(), cost_traj.end() ));  //argmin
			int actual_lane = floor(car_d/lane_width);
			if(abs(actual_lane-lane)==2)
			{
				lane = 1; //don't look too far!
			}
			else if(car_speed<15)
			{
				lane = actual_lane; //keep lane if too slow
			}
			double target_speed = max_speed_new[lane];
			
			// assigning acceleration and speed
			double cost_acc = -2*fmin(.5,fmax(0,car_speed-target_speed))+fmax(0,1-car_speed/target_speed);
			acc=cost_acc*max_acc;  
			
			// updating reference speed
			vref+=acc;
			
			
			vector<double> ptsx;
          	vector<double> ptsy;
			
			double ref_x = car_x;
			double ref_y = car_y;
			double ref_yaw = deg2rad(car_yaw);
			
			
			if (prev_npts<2)
			{
			    // if not enough previous pts, ad at least 1
				double prev_x = car_x-cos(car_yaw);
				double prev_y = car_y-sin(car_yaw);
				
				ptsx.push_back(prev_x);
				ptsx.push_back(car_x);
				
				ptsy.push_back(prev_y);
				ptsy.push_back(car_y);
			} 
			else 
			{
				ref_x = previous_path_x[prev_npts-1];
				ref_y = previous_path_y[prev_npts-1];
				
				double prev_x = previous_path_x[prev_npts-2];
				double prev_y = previous_path_y[prev_npts-2];
				
				ref_yaw = atan2(ref_y-prev_y,ref_x-prev_x);
				
				ptsx.push_back(prev_x);
				ptsx.push_back(ref_x);
				
				ptsy.push_back(prev_y);
				ptsy.push_back(ref_y);
				
			}
			
			vector<double> wp0 = getXY(car_s+maneuver_distance, (lane+0.5)*lane_width, map_waypoints_s, map_waypoints_x, map_waypoints_y);
			vector<double> wp1 = getXY(car_s+maneuver_distance*2, (lane+0.5)*lane_width, map_waypoints_s, map_waypoints_x, map_waypoints_y);
			vector<double> wp2 = getXY(car_s+maneuver_distance*3, (lane+0.5)*lane_width, map_waypoints_s, map_waypoints_x, map_waypoints_y);
		
			ptsx.push_back(wp0[0]);
			ptsx.push_back(wp1[0]);
			ptsx.push_back(wp2[0]);
			
			ptsy.push_back(wp0[1]);
			ptsy.push_back(wp1[1]);
			ptsy.push_back(wp2[1]);
			
			for(int i=0;i<ptsx.size();i++){
				
				// to car coordinates
				double shift_x = ptsx[i]-ref_x;
				double shift_y = ptsy[i]-ref_y;
				
				ptsx[i]=(shift_x*cos(-ref_yaw)-shift_y*sin(-ref_yaw));
				ptsy[i]=(shift_x*sin(-ref_yaw)+shift_y*cos(-ref_yaw));
				
			}

			tk::spline s;
			
			s.set_points(ptsx,ptsy);
			
			vector<double> next_x_vals;
          	vector<double> next_y_vals;
			
			for(int i=0;i< prev_npts;i++)
			{
				next_x_vals.push_back(previous_path_x[i]);
				next_y_vals.push_back(previous_path_y[i]);
			}	
			
			double target_x = maneuver_distance;
			double target_y = s(target_x);
			double target_dist = sqrt(target_x*target_x+target_y*target_y);
			
			double x_add_on = 0;
						
			for(int i=1;i<=50-prev_npts;i++)
			{
				double N = (target_dist/(0.02*vref/2.24));
				double x_point = x_add_on+target_x/N;
				double y_point = s(x_point);
				
				x_add_on = x_point;
				
				double x_ref = x_point;
				double y_ref = y_point;
				
				x_point = (x_ref*cos(ref_yaw)-y_ref*sin(ref_yaw));
				y_point = (x_ref*sin(ref_yaw)+y_ref*cos(ref_yaw));
				
				x_point+=ref_x;
				y_point+=ref_y;
				
				next_x_vals.push_back(x_point);
				next_y_vals.push_back(y_point);
			}
          	// TODO: define a path made up of (x,y) points that the car will visit sequentially every .02 seconds
          	msgJson["next_x"] = next_x_vals;
          	msgJson["next_y"] = next_y_vals;

          	auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
