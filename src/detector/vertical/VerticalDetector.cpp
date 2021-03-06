#include <iostream>
#include "VerticalDetector.h"

#include <list>

#include <opencv_workbench/utils/ColorMaps.h>
#include <opencv_workbench/wb/WB.h>
#include <opencv_workbench/wb/Cluster.h>
#include <opencv_workbench/wb/ClusterProcess.h>
#include <opencv_workbench/wb/NDT.h>
#include <opencv_workbench/utils/OpenCV_Helpers.h>

using std::cout;
using std::endl;

VerticalDetector::VerticalDetector()
{
     cout << "VerticalDetector Constructor" << endl;
     thresh_value_ = 255;
     grad_thresh_value_ = 255;

     cluster_process_.set_threshold(0);
     cluster_process_.set_gate(25); // 15, 50, 100
     cluster_process_.set_min_cluster_size(30);     

     int erosionElem = cv::MORPH_ELLIPSE;
     int erosionSize = 1;
     int dilationElem = cv::MORPH_ELLIPSE; // MORPH_RECT, MORPH_CROSS, MORPH_ELLIPSE
     int dilationSize = 1;          

     erosionConfig_ = cv::getStructuringElement( erosionElem,
                                                 cv::Size(2*erosionSize+1, 2*erosionSize+1),
                                                 cv::Point(erosionSize, erosionSize) );
     
     dilationConfig_ = cv::getStructuringElement( dilationElem,
                                                  cv::Size(2*dilationSize+1, 2*dilationSize+1),
                                                  cv::Point(dilationSize, dilationSize) );
}

VerticalDetector::~VerticalDetector()
{
     cout << "VerticalDetector Destructor" << endl;
}

void VerticalDetector::print()
{
     cout << "I am the Vertical Detector" << endl;
}

void VerticalDetector::set_stream(syllo::Stream *stream)
{
     stream_ = stream;
}

double VerticalDetector::trajectory_diff(std::list<cv::Point2d> &t1,
                                         std::list<cv::Point2d> &t2)
{
     // Calculate differences in trajectories
     double total_diff = 0;
     std::vector<cv::Point2d> diffs;
     int frame = 0;
     std::list<cv::Point2d>::iterator it1 = t1.begin();
     std::list<cv::Point2d>::iterator it2 = t2.begin();
     for(; it1 != t1.end() && it2 != t2.end(); 
         it1++, it2++) {               
          
          double diff = pow(it1->x - it2->x, 2) + pow(it1->y - it2->y, 2);
          diffs.push_back(cv::Point2d(frame,diff));               
          total_diff += diff;
          frame++;               
     }
     //vectors.push_back(diffs);
     //labels.push_back("Diff");
     //styles.push_back("linespoints");

     //vectors.push_back(costs);
     //labels.push_back("Cost");
     //styles.push_back("linespoints");
     //plot.plot(vectors, title2, labels, styles);
          
     double mean = total_diff / (double)diffs.size();
     double RMSE = sqrt(mean);
     return RMSE;
}

void VerticalDetector::trajectory_polar_diff(std::list<wb::Entity> &traj,
                                             std::list<cv::Point2d> &diffs)
{
     std::list<wb::Entity>::reverse_iterator it = traj.rbegin();
     cv::Point2d prev;
     for(; it != traj.rend(); it++) {
          if (it == traj.rbegin()) {
               prev = it->centroid();
               continue;
          }
     
          cv::Point2d p = it->centroid();
     
          // Convert to polar
          double range = sqrt( pow(p.x,2) + pow(p.y,2) );
          double range_prev = sqrt( pow(prev.x,2) + pow(prev.y,2) );          
          double theta = atan(p.y / p.x);
          double theta_prev = atan(prev.y / prev.x);
          //
          cv::Point2d polar(range,theta);
          cv::Point2d polar_prev(range_prev, theta_prev);
          
          double range_diff = range - range_prev;
          double theta_diff = theta - theta_prev;
                    
          cv::Point2d temp;
          temp.x = range_diff;
          temp.y = theta_diff;                   
                   
          diffs.push_back(temp);          
               
          prev = it->centroid();
     }
}

void draw_line(cv::Mat &img, double rho, double theta, cv::Scalar color, 
               int thickness)
{
     cv::Point pt1, pt2;
     double a = cos(theta), b = sin(theta);
     double x0 = a*rho, y0 = b*rho;
     pt1.x = cvRound(x0 + 1000*(-b));
     pt1.y = cvRound(y0 + 1000*(a));
     pt2.x = cvRound(x0 - 1000*(-b));
     pt2.y = cvRound(y0 - 1000*(a));
     cv::line( img, pt1, pt2, color, thickness, CV_AA);
}

double norm_pi(double x)
{
     while (x < 0)   x += 2*CV_PI;
     while (x > 2*CV_PI) x -= 2*CV_PI;
     return x;
}

double below_90(double x)
{
     while (x > CV_PI/2.0) {
          x -= CV_PI;
     }
     return x;
}

struct TrajRMSE {
     int ID1;
     int ID2;
     double RMSE;
};

void VerticalDetector::trajectory_similarity(int frame_number, cv::Mat &img)
{
     //cout << "==========================================" << endl;
     std::map<int, std::list<cv::Point2d> > trajectories;
     for (std::map<int, std::list<wb::Entity> >::iterator 
               it = tracks_history_.begin(); it != tracks_history_.end(); ) {
          if (it->second.back().frame() < frame_number) {
               // Clear out track IDs that are dead
               tracks_history_.erase(it++);
          } else {
               if (it->second.back().is_confirmed()) {
                    //cout << "Tracked: " << it->first << endl;
                    // Compute derivative of each current track's trajectory
                    trajectory_polar_diff(it->second, trajectories[it->first]);                    
               }
               it++;
          }
     }

     std::map<std::string, struct TrajRMSE> RMSEs;
     // Store RMSE between each trajectory in a map/hash. A string will be used
     // to hash the values. The string is of the form "n:m", where n and m are
     // the trajectory IDs and n is less than m.

     // Calculate differences between trajectory derivatives
     std::map<int, std::list<cv::Point2d> >::iterator it_1 = trajectories.begin();
     for(; it_1 != trajectories.end(); it_1++) {
          std::map<int, std::list<cv::Point2d> >::iterator it_2 = trajectories.begin();
          for(; it_2 != trajectories.end(); it_2++) {
               // Determine if this trajectory difference has already been
               // calculated
               std::string key;
               struct TrajRMSE traj;
               if (it_1->first == it_2->first) {
                    // Don't calculate trajectory diff between two trajectories
                    // with the same ID. This is the same trajectory.
                    continue;
               } else if (it_1->first < it_2->first) {
                    key = syllo::int2str(it_1->first) + ":" + syllo::int2str(it_2->first);
                    traj.ID1 = it_1->first;
                    traj.ID2 = it_2->first;
               } else {
                    key = syllo::int2str(it_2->first) + ":" + syllo::int2str(it_1->first);
                    traj.ID1 = it_2->first;
                    traj.ID2 = it_1->first;
               }

               if (RMSEs.count(key) == 0) {               
                    double RMSE = trajectory_diff(it_1->second, it_2->second);
                    traj.RMSE = RMSE;
                    RMSEs[key] = traj;
                    //cout << "--------------" << endl;
                    //cout << it_1->first << " - " << it_2->first << " : " << RMSE << endl;
               }
          }
     }

     // Print out the RMSEs for this frame:
     //cout << "-----------------" << endl;
     for(std::map<std::string, struct TrajRMSE>::iterator it = RMSEs.begin();
         it != RMSEs.end(); it++) {
          //cout << it->first << ": " << it->second.RMSE << endl;
     
          // If the RMSE between two trajectories is less than a threshold,
          // circle the trajectories
          if (it->second.RMSE < 0.017) {
               // Get the two IDs in the track history and circle them
               cv::Point p1 = tracks_history_[it->second.ID1].back().pixel_centroid();
               cv::Point p2 = tracks_history_[it->second.ID2].back().pixel_centroid();
               
               //cv::circle(img, p1, 10, cv::Scalar(0,255,255), 1, 8, 0);
               //cv::circle(img, p2, 10, cv::Scalar(0,255,255), 1, 8, 0);
               cv::line(img, p1, p2, cv::Scalar(0,255,255), 1, 8, 0);
          }          
     }

     cv::imshow("SimTraj", img);
}

int VerticalDetector::set_frame(int frame_number, const cv::Mat &original)
{               
      cv::Mat original_w_tracks, original_copy;          
      original_w_tracks = original;
      original_copy = original;
      
      //cv::cvtColor(original_copy, original_copy, CV_RGBA2BGR);
      //wb::show_nonzero(original_copy);
      
      //cv::Mat bad_gray;
      //cv::applyColorMap(original, bad_gray, CV_BGR2GRAY);
      //cv::cvtColor(original, bad_gray, CV_BGR2GRAY);
      //cv::imshow("bad", bad_gray);
      
      cv::Mat mask;
      wb::get_sonar_mask(original, mask);
      cv::imshow("Sonar Mask", mask*255);
      
      cv::Mat gray;
      if (original.channels() != 1) {           
           Jet2Gray_matlab(original,gray);           
      } else {
           gray = original.clone();
      }
      cv::imshow("Gray", gray);      
      
      //cv::Mat ndt_img;
      //ndt_.set_frame(gray, ndt_img);
      //cv::imshow("ndt", ndt_img);
      
      // Compute median
      cv::Mat median;
      cv::medianBlur(gray, median,5);
      cv::imshow("median", median);          
      
      /// // Compute estimated gradient
      /// cv::Mat grad;     
      /// wb::gradient_sobel(median, grad);
      /// cv::imshow("gradient", grad);
      /// 
      ///cv::Mat thresh_grad;
      ///wb::adaptive_threshold(grad, thresh_grad, grad_thresh_value_, 0.001, 0.002, 10, 5);
      /// cv::imshow("grad thresh", thresh_grad);

      cv::Mat thresh_amp;      
#if 1    
      wb::adaptive_threshold(median, thresh_amp, thresh_value_, 0.001, 0.002, 10, 5);
      //wb::adaptive_threshold(median, thresh_amp, thresh_value_, 0.004, 5, mask);      
#else
      // TODO: For some reason, using this static threshold kills regular sonar
      // images, (vs. simulated sonar images), it might have to do with the
      // number of detected blobs Used for thresholding simulated sonar data
      // from Gazebo
      cv::threshold(median, thresh_amp, 100, 255, cv::THRESH_TOZERO);
#endif
      cv::imshow("thresh amp", thresh_amp);
      
      /// cv::Mat thresh_and_grad = thresh_amp + thresh_grad;
      /// cv::imshow("thresh and grad", thresh_and_grad);     
      cv::Mat erode;
      cv::erode(thresh_amp, erode, erosionConfig_);
      //cv::imshow("erode", erode);
      
      cv::Mat dilate;
      cv::dilate(erode, dilate, dilationConfig_);
      cv::imshow("Erode/Dilate", dilate);      
      
      blob_process_.process_frame(dilate, median, thresh_value_);
      
      cv::Mat blob_img;
      blob_process_.overlay_blobs(gray, blob_img);      
      
      blob_process_.overlay_tracks(blob_img, blob_img);
      cv::imshow("Blobs", blob_img);        
      
      cv::Mat blob_consolidate;
      blob_process_.consolidate_tracks(gray, blob_consolidate);
      cv::imshow("Consolidate", blob_consolidate); 

      //////////////////////////////////////////////////////////////
      // Ground Plane Detection
      //////////////////////////////////////////////////////////////
      cv::Mat thresh_ground, color_dst;
      cv::threshold(gray, thresh_ground, 100, 255, cv::THRESH_TOZERO);
      cv::cvtColor(thresh_ground, color_dst, CV_GRAY2BGR);          

      std::vector<cv::Vec2f> lines;
      cv::HoughLines(thresh_ground, lines, 1, CV_PI/180, 100, 0, 0 );
      
      double theta_sum = 0;
      double rho_sum = 0;
      std::vector<cv::Vec2f> samples;
      int count = 0;
          
      //cout<< "-----------" << endl;
      for (size_t i = 0; i < lines.size(); i++ ) {          
           float rho = lines[i][0], theta = norm_pi(lines[i][1]);
           //cout << "rho: " << rho << endl;
           //cout << "theta: " << lines[i][1]*180.0/CV_PI << " -> " << theta*180.0/CV_PI << endl;                              
           if ( (theta > (160.0*CV_PI/180.0) && theta <= (180.0*CV_PI/180.0)) || (theta >= 0 && theta < (20*CV_PI/180.0))) {
      
                //double theta_diff;
                //if (theta > 90*3.14159265359/180.0) {
                //     theta_diff = 3.14159265359 - theta;
                //} else {
                //     theta_diff = theta;
                //}
                    
                //theta_sum += theta_diff; // difference from 180 degrees
                //cout << "theta: " << theta*CV_PI/180.0 << " -> " << below_90(theta)*CV_PI/180 << endl;
                    
                theta_sum += below_90(theta);
                rho_sum += abs(rho);
                samples.push_back(cv::Vec2f(abs(rho), below_90(theta)));
                count++;
      
                draw_line(color_dst, rho, theta, cv::Scalar(0,0,255), 1);
           } else {
                draw_line(color_dst, rho, theta, cv::Scalar(255,0,0), 1);
           }                              
      }      
          
      cv::Mat blob_consolidate_ground = blob_consolidate.clone();
      if (count != 0) {
           // Draw "averaged" ground plane equation
           double theta_avg = theta_sum / (double)count;
           double rho_avg = rho_sum / (double)count;
      
           double theta_diff_sum = 0;
           double rho_diff_sum = 0;
               
           std::vector<cv::Vec2f>::iterator it;               
           for(it = samples.begin(); it != samples.end(); it++) {
                float rho = (*it)[0], theta = (*it)[1];
                rho_diff_sum += pow(rho-rho_avg,2);
                theta_diff_sum += pow(theta-theta_avg,2);
      
                //cout << "rho: " << rho << endl;
           }
      
           double rho_std = sqrt( rho_diff_sum / (double)(samples.size()));
           //double theta_std = sqrt( theta_diff_sum / (double)(samples.size()));                              
      
           draw_line(color_dst, rho_avg, theta_avg, cv::Scalar(255,0,255), 2);
           draw_line(blob_consolidate_ground, rho_avg, theta_avg, cv::Scalar(0,0,255), 2);               
      
           double rho_plus = rho_avg + rho_std/2;
           double rho_minus = rho_avg - rho_std/2;
      
           // Draw error bars
           draw_line(blob_consolidate_ground, rho_plus, theta_avg, cv::Scalar(0,255,0), 2);               
           draw_line(blob_consolidate_ground, rho_minus, theta_avg, cv::Scalar(0,255,0), 2);               
      
           //cout << "R    : Avg=" << rho_avg << "\tstd=" << rho_std << endl;
           //cout << "Theta: Avg=" << theta_avg << "\tstd=" << theta_std << endl;               
      }
          
      cv::imshow("detected lines", color_dst);
      cv::imshow("blob_consolidate_ground", blob_consolidate_ground);
          
      
      //////////////////////////////////////////////////////////////
      /// Tracking     
      ////////////////////////////////////////////////////
      tracks_.clear(); // clear out the tracks from previous loop
      
      std::vector<wb::Blob> blobs = blob_process_.blobs();
      std::vector<wb::Blob>::iterator it = blobs.begin();
      for (; it != blobs.end(); it++) {

           // Have to transform tracks from distorted cartesian
           // to polar, then to undistorted cartesian
           cv::Point p = it->pixel_centroid();

           double x, y;
           if (stream_->type() == syllo::SonarType) {           
                double range = stream_->pixel_range(p.y, p.x); //row, col
                double bearing = stream_->pixel_bearing(p.y, p.x) * 0.017453293; // pi/180
                y = -range*cos(bearing);
                x = range*sin(bearing);
           } else {
                x = p.x;
                y = p.y;
           }
           
           it->set_centroid(cv::Point2d(x,y));
           it->set_frame(frame_number);
           
           //tracks_history_[it->id()].push_back((wb::Entity)*it);
           
           tracks_.push_back((wb::Entity)*it);
      }

      // Calculate Similarity between current tracks
      //this->trajectory_similarity(frame_number, blob_img);
      
      ///////////////////////////////////////////////////
      // Display images
      ///////////////////////////////////////////////////
      if (!hide_windows_) {          
      }
     
     return 0;
}

extern "C" {
     Detector *maker(){
          return new VerticalDetector;
     }

     class proxy {
     public:
          proxy(){
               // register the maker with the factory
               // causes static initialization error plugin_manager_.factory["blank_detector"] = maker;
               plugin_manager_.factory["vertical_detector"] = maker;
          }
     };
     // our one instance of the proxy
     proxy p;
}


/// Color map tests
///cv::Mat grad;
///create_gradient(grad, 100, 255);
///cv::imshow("gradient", grad);
///
///cv::Mat jet_matlab;
///Gray2Jet_matlab(grad,jet_matlab);
///cv::imshow("jet matlab", jet_matlab);
///
///cv::Mat gray_2;
///Jet2Gray_matlab(jet_matlab,gray_2);
///cv::imshow("gray_2",gray_2);
///
///cv::Mat diff = gray_2 - grad;
///cv::imshow("diff",diff);
///
///if (!equal(gray_2, grad)) {
///     cout << "Grays are unequal" << endl;
///}     
