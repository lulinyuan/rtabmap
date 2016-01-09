/*
Copyright (c) 2010-2014, Mathieu Labbe - IntRoLab - Universite de Sherbrooke
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Universite de Sherbrooke nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <rtabmap/core/StereoCameraModel.h>
#include <rtabmap/utilite/ULogger.h>
#include <rtabmap/utilite/UDirectory.h>
#include <rtabmap/utilite/UFile.h>
#include <rtabmap/utilite/UConversion.h>
#include <opencv2/imgproc/imgproc.hpp>

namespace rtabmap {

void StereoCameraModel::setName(const std::string & name)
{
	name_=name;
	left_.setName(name_+"_left");
	right_.setName(name_+"_right");
}

bool StereoCameraModel::load(const std::string & directory, const std::string & cameraName, bool ignoreStereoTransform)
{
	name_ = cameraName;
	if(left_.load(directory, cameraName+"_left") && right_.load(directory, cameraName+"_right"))
	{
		if(ignoreStereoTransform)
		{
			return true;
		}
		//load rotation, translation
		R_ = cv::Mat();
		T_ = cv::Mat();

		std::string filePath = directory+"/"+cameraName+"_pose.yaml";
		if(UFile::exists(filePath))
		{
			UINFO("Reading stereo calibration file \"%s\"", filePath.c_str());
			cv::FileStorage fs(filePath, cv::FileStorage::READ);

			name_ = (int)fs["camera_name"];

			// import from ROS calibration format
			cv::FileNode n = fs["rotation_matrix"];
			int rows = (int)n["rows"];
			int cols = (int)n["cols"];
			std::vector<double> data;
			n["data"] >> data;
			UASSERT(rows*cols == (int)data.size());
			UASSERT(rows == 3 && cols == 3);
			R_ = cv::Mat(rows, cols, CV_64FC1, data.data()).clone();

			n = fs["translation_matrix"];
			rows = (int)n["rows"];
			cols = (int)n["cols"];
			data.clear();
			n["data"] >> data;
			UASSERT(rows*cols == (int)data.size());
			UASSERT(rows == 3 && cols == 1);
			T_ = cv::Mat(rows, cols, CV_64FC1, data.data()).clone();

			n = fs["essential_matrix"];
			rows = (int)n["rows"];
			cols = (int)n["cols"];
			data.clear();
			n["data"] >> data;
			UASSERT(rows*cols == (int)data.size());
			UASSERT(rows == 3 && cols == 3);
			E_ = cv::Mat(rows, cols, CV_64FC1, data.data()).clone();

			n = fs["fundamental_matrix"];
			rows = (int)n["rows"];
			cols = (int)n["cols"];
			data.clear();
			n["data"] >> data;
			UASSERT(rows*cols == (int)data.size());
			UASSERT(rows == 3 && cols == 3);
			F_ = cv::Mat(rows, cols, CV_64FC1, data.data()).clone();

			fs.release();

			return true;
		}
		else
		{
			UWARN("Could not load stereo calibration file \"%s\".", filePath.c_str());
		}
	}
	return false;
}
bool StereoCameraModel::save(const std::string & directory, bool ignoreStereoTransform) const
{
	if(left_.save(directory) && right_.save(directory))
	{
		if(ignoreStereoTransform)
		{
			return true;
		}
		std::string filePath = directory+"/"+name_+"_pose.yaml";
		if(!filePath.empty() && !name_.empty() && !R_.empty() && !T_.empty())
		{
			UINFO("Saving stereo calibration to file \"%s\"", filePath.c_str());
			cv::FileStorage fs(filePath, cv::FileStorage::WRITE);

			// export in ROS calibration format

			fs << "camera_name" << name_;

			fs << "rotation_matrix" << "{";
			fs << "rows" << R_.rows;
			fs << "cols" << R_.cols;
			fs << "data" << std::vector<double>((double*)R_.data, ((double*)R_.data)+(R_.rows*R_.cols));
			fs << "}";

			fs << "translation_matrix" << "{";
			fs << "rows" << T_.rows;
			fs << "cols" << T_.cols;
			fs << "data" << std::vector<double>((double*)T_.data, ((double*)T_.data)+(T_.rows*T_.cols));
			fs << "}";

			fs << "essential_matrix" << "{";
			fs << "rows" << E_.rows;
			fs << "cols" << E_.cols;
			fs << "data" << std::vector<double>((double*)E_.data, ((double*)E_.data)+(E_.rows*E_.cols));
			fs << "}";

			fs << "fundamental_matrix" << "{";
			fs << "rows" << F_.rows;
			fs << "cols" << F_.cols;
			fs << "data" << std::vector<double>((double*)F_.data, ((double*)F_.data)+(F_.rows*F_.cols));
			fs << "}";

			fs.release();

			return true;
		}
	}
	return false;
}

void StereoCameraModel::scale(double scale)
{
	left_ = left_.scaled(scale);
	right_ = right_.scaled(scale);
}

float StereoCameraModel::computeDepth(float disparity) const
{
	//depth = baseline * f / (disparity + cx1-cx0);
	UASSERT(this->isValid());
	if(disparity == 0.0f)
	{
		return 0.0f;
	}
	return baseline() * left().fx() / (disparity + right().cx() - left().cx());
}

float StereoCameraModel::computeDisparity(float depth) const
{
	// disparity = (baseline * fx / depth) - (cx1-cx0);
	UASSERT(this->isValid());
	if(depth == 0.0f)
	{
		return 0.0f;
	}
	return baseline() * left().fx() / depth - right().cx() + left().cx();
}

float StereoCameraModel::computeDisparity(unsigned short depth) const
{
	// disparity = (baseline * fx / depth) - (cx1-cx0);
	UASSERT(this->isValid());
	if(depth == 0)
	{
		return 0.0f;
	}
	return baseline() * left().fx() / (float(depth)/1000.0f) - right().cx() + left().cx();
}

Transform StereoCameraModel::stereoTransform() const
{
	if(!R_.empty() && !T_.empty())
	{
		return Transform(
				R_.at<double>(0,0), R_.at<double>(0,1), R_.at<double>(0,2), T_.at<double>(0),
				R_.at<double>(1,0), R_.at<double>(1,1), R_.at<double>(1,2), T_.at<double>(1),
				R_.at<double>(2,0), R_.at<double>(2,1), R_.at<double>(2,2), T_.at<double>(2));
	}
	return Transform();
}

} /* namespace rtabmap */