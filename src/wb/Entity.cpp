#include <iostream>

#include <boost/tokenizer.hpp>

#include <opencv_workbench/syllo/syllo.h>

#include "Entity.h"

using std::cout;
using std::endl;

#define CONFIRMED_AGE 3
#define DEAD_OCCLUDED_AGE 5

namespace wb {

     Entity::Entity() : id_(-1), name_("unknown:-1"), 
                        type_(Unknown), age_(0), occluded_age_(0), 
                        occluded_(false), is_confirmed_(false), visited_(false), 
                        cluster_id_(0)
     {
          matched_ = false;
          //KF_ = cv::KalmanFilter(4, 2, 0);
          //transition_matrix_ = cv::Mat_<float>(4,4);
          //transition_matrix_  << 1,0,1,0,   0,1,0,1,  0,0,1,0,  0,0,0,1;
          //KF_.transitionMatrix = transition_matrix_;

          // Kalman Filter State Description:
          // X-position : 0
          // Y-position : 1
          // X-velocity : 2
          // Y-velocity : 3

          A.resize(4,4);     // State transition
          B.resize(4,2);     // Input matrix
          H.resize(2,4);     // Measurement matrix
          Q.resize(4,4);     // Process noise
          R.resize(2,2);     // Measurement noise
          x0.resize(4,1);    // Initial state
          covar.resize(4,4); // Covariance matrix

          double T = 0.066666667;//; dt
          //double T = 1;//; dt
          //double T = 0.05;
          A << 1, 0, T, 0,
               0, 1, 0, T,
               0, 0, 1, 0,
               0, 0, 0, 1;

          // Inputs are from velocity
          B << 0, 0,
               0, 0,
               1, 0,
               0, 1;

          // Measurements are from x/y positions
          H << 1, 0, 0, 0,
               0, 1, 0, 0;

          double q = 1 * T;
          Q = Eigen::MatrixXf::Identity(A.rows(), A.cols()) * q;

          //double r = 0.04;
          //double r = 50;
          //double r = 10;
          ///double r = 0.04;
          double r = 10;
          R << r, 0,
               0, r;
          
          //double v = 0.01;
          //double v = 10;
          double v = 100;
          //double v = 2;
          covar << v, 0, 0, 0,
                   0, v, 0, 0,
                   0, 0, v, 0,
                   0, 0, 0, v;

          z.resize(2,1);
          u.resize(2,1);

          kf_.setModel(A,B,H,Q,R);
     }

     void Entity::set_R(double r)
     {
          Eigen::MatrixXf R;
          R.resize(2,2);
          
          R << r, 0,
               0, r;
          kf_.set_R(R);
     }

     void Entity::set_P(double p)
     {
          Eigen::MatrixXf covar;
          covar.resize(4,4);
          
          covar << p, 0, 0, 0,
                   0, p, 0, 0,
                   0, 0, p, 0,
                   0, 0, 0, p;
          kf_.set_P(covar);
     }

     void Entity::init()
     {
          compute_metrics();

          x0 << pixel_centroid_.x,
                pixel_centroid_.y,
                0,
                0;

          start_pixel_centroid_ = pixel_centroid_;

          kf_.init(x0, covar);
     }

     cv::Point Entity::trail_history(int past)
     {
          cv::Point result;
          int count = 0;
          for (std::list<cv::Point2f>::reverse_iterator rit = trail_.rbegin();
               rit != trail_.rend(); ++rit) {
               result = *rit;
               if (count >= past) {
                    return result;
               }
               count++;
          }
          return result;
     }

     cv::Point Entity::estimated_pixel_centroid()
     {
          // the Kalman filter's estimate centroid. Otherwise, use the entity's
          // current centroid if it isn't confirmed yet
          if (this->is_confirmed()) {               
               return est_pixel_centroid_;
          } else {
               return pixel_centroid_;
          }
     }

     cv::Point2d Entity::estimated_centroid()
     {
          // the Kalman filter's estimate centroid. Otherwise, use the entity's
          // current centroid if it isn't confirmed yet
          if (this->is_confirmed()) {               
               return est_centroid_;
          } else {
               return centroid_;
          }
     }

     Ellipse Entity::error_ellipse(double confidence)
     {
          return kf_.error_ellipse(confidence);
     }

     void Entity::predict_tracker()
     {
          //cv::Mat prediction = KF_.predict();
          //est_centroid_ = cv::Point(prediction.at<float>(0),prediction.at<float>(1));

          // TODO: better input velocity?
          u << 0,0;
          kf_.predict(u);
          state = kf_.state();
          est_pixel_centroid_ = cv::Point(cvRound(state(0,0)),cvRound(state(1,0)));
     }

     void Entity::correct_tracker()
     {          
          // correct tracker update using the newly computed centroid.
          z << pixel_centroid_.x, pixel_centroid_.y;
          kf_.update(z);
          state = kf_.state();
          est_pixel_centroid_ = cv::Point(round(state(0,0)),round(state(1,0)));
     }

     void Entity::update_trail()
     {
          trail_.push_back(estimated_pixel_centroid());
     }

     void Entity::compute_metrics()
     {
          ////////////////////////////////////////////////////////
          // Compute Centroid and Bounding Box Rectangle
          ////////////////////////////////////////////////////////
          int sum_x = 0, sum_y = 0, sum_value = 0;
          int xmin = 999999999, ymin = 999999999, xmax = -999, ymax = -999;
          std::vector<Point>::iterator it = points_.begin();
          for (; it != points_.end(); it++) {
               int x = it->position().x;
               int y = it->position().y;

               int value = it->value();
               sum_value += value;

               sum_x += x * value;
               sum_y += y * value;

               if (x < xmin) {
                    xmin = x;
               }
               if (x > xmax) {
                    xmax = x;
               }
               if (y < ymin) {
                    ymin = y;
               }
               if (y > ymax) {
                    ymax = y;
               }
          }

          //double avg_x = (double)sum_x / points_.size();
          //double avg_y = (double)sum_y / points_.size();
          double avg_x = (double)sum_x / (double)sum_value;
          double avg_y = (double)sum_y / (double)sum_value;

          pixel_centroid_ = cv::Point(round(avg_x), round(avg_y));
          est_pixel_centroid_ = pixel_centroid_;

          // +1's to account for slight error in rectangle drawing?
          //bbox_ = cv::Rect rect(xmin, ymin, xmax-xmin+1, ymax-ymin+1);
          bbox_.set_box(xmin, xmax+1, ymin, ymax+1);          
     }

     cv::Rect Entity::rectangle()
     {
          return bbox_.rectangle();
     }     

     bool Entity::is_confirmed()
     {
          if (is_confirmed_) return true;

          if (age_ >= CONFIRMED_AGE) {
               is_confirmed_ = true;
               return true;
          } else {
               is_confirmed_ = false;
               return false;
          }
     }

     bool Entity::is_dead()
     {
          if (occluded_age_ >= DEAD_OCCLUDED_AGE) {
               return true;
          } else {
               return false;
          }          
     }

     void Entity::inc_age()
     {
          age_++;          
     }

     void Entity::set_age(int age)
     {
          age_ = age;          
     }

     void Entity::dec_age()
     {
          age_--;
     }

     void Entity::missed_track()
     {
          this->set_occluded(true);
          this->inc_occluded_age();
     }

     void Entity::detected_track()
     {
          this->inc_age();
          this->set_occluded(false);

          // Update the Kalman Filter Tracker
          this->correct_tracker();
     }

     void Entity::new_track(int id)
     {
          this->set_id(id);
          this->set_age(1);
          this->set_occluded(false);
     }

     void Entity::set_occluded(bool occluded) 
     {
          occluded_ = occluded; 
          if (!occluded_) {
               this->set_occluded_age(0);
          }
     }

     void Entity::copy_track_info(Entity &other)
     {
          this->set_id(other.id());
          this->set_age(other.age());          
          this->set_estimated_pixel_centroid(other.estimated_pixel_centroid());
          this->set_estimated_centroid(other.estimated_centroid());
          this->set_start_centroid(other.start_centroid());
          this->set_start_pixel_centroid(other.start_pixel_centroid());
          this->set_tracker(other.tracker());
          this->trail_ = other.trail_;
     }

     void Entity::copy_meas_info(Entity &other)
     {
          this->centroid_ = other.centroid();
          this->pixel_centroid_ = other.pixel_centroid();
          this->bbox_ = other.bbox();
     }

     void Entity::matched_track(Entity &match)
     {
          // Update the current entity with the track info from "match"
          this->copy_track_info(match);

          // Update track variables
          this->detected_track();
     }

     void Entity::remove_point(wb::Point &p)
     {
          std::vector<Point>::iterator it = points_.begin();
          for (; it != points_.end(); it++) {
               if (it->position() == p.position()) {
                    points_.erase(it);
                    break;
               }
          }
     }

     void Entity::add_points(std::vector<wb::Point> &points)
     {
          std::vector<wb::Point>::iterator it = points.begin();
          for (; it != points.end(); it++) {
               points_.push_back(*it);
          }
     }

     std::string Entity::name()
     {
          std::ostringstream convert;
          convert << id_;

          switch (type_) {
          case Unknown:
               name_ = "unknown:" + convert.str();
               break;
          case Diver:
               name_ = "diver:" + convert.str();
               break;
          case Clutter:
               name_ = "clutter:" + convert.str();
               break;
          case APoint:
               name_ = "point:" + convert.str();
               break;
          default:
               name_ = "unknown:" + convert.str();
          }

          return name_;
     }

     std::string Entity::type_str()
     {
          switch (type_) {
          case Unknown:
               return "unknown";
               break;
          case Diver:
               return "diver";
               break;
          case Clutter:
               return "clutter";
               break;
          case APoint:
               return "point";
               break;
          default:
               return "unknown";
          }
     }

     wb::Entity::EntityType_t Entity::str_2_type(std::string str)
     {
          if (str == "unknown") {
               return Unknown;
          } else if (str == "diver") {
               return Diver;
          } else if (str == "clutter") {
               return Clutter;
          } else if (str == "point") {
               return APoint;
          } else {
               return Unknown;
          }
     }

     void Entity::set_name(std::string name)
     {
          boost::char_separator<char> sep(":");
          boost::tokenizer<boost::char_separator<char> > tokens(name, sep);
          int iter = 0;
          std::string type_str = "unknown";
          id_ = -2;
          for (boost::tokenizer<boost::char_separator<char> >::iterator tok_iter = tokens.begin();
               tok_iter != tokens.end(); ++tok_iter) {

               if (iter == 0) {
                    type_str = *tok_iter;
               } else if (iter == 1) {
                    id_  = syllo::str2int(std::string(*tok_iter));
               }
               iter++;
          }

          type_ = Entity::str_2_type(type_str);
     }
}
