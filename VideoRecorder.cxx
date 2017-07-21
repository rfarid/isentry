
#include <time.h>
#include <iostream>

#include <cv.h>
#include <highgui/highgui.hpp>

#include "VideoRecorder.hxx"

using namespace std;
using namespace cv;

std::string ISentry::VideoRecorder::ext("avi");

ISentry::VideoRecorder::VideoRecorder(const libconfig::Setting& cfg):MediaRecorder(),n(0), writer(NULL)
{
    enabled = cfg["enabled"];
    dir=(const char *)(cfg["dir"]);

    if(!cfg.lookupValue("max_width",max_width))
        max_width=-1;
    if(!cfg.lookupValue("max_videos",max_files))
        max_files=-1;
    if(!cfg.lookupValue("max_duration",max_duration))
        max_duration=-1;
    running = false;
}

void ISentry::VideoRecorder::addFrame(std::pair<cv::Mat,time_t> &m)
{
    if(!running || !enabled)
        return;

    cv::Mat m_s;
    rescale(m.first, m_s);
    if(n==0)
    {
        // first frame
        if(max_files > 0 && files_recorded++ >= max_files)
        {
            std::cerr << "NOT writing. Limit of " << max_files << " reached." << std::endl;
            enabled = false; // Disable self
        } else {
            string fname = getFilename(m.second);
            std::cerr << "VideoRecorder::Writing " << fname << std::endl;
            writer=new cv::VideoWriter(fname,CV_FOURCC('D','I','V','X'), 30, m.first.size(), false); // true
            //TODO: error handling
            /* added by RF */
            //--- INITIALIZE VIDEOWRITER
            // VideoWriter writer;
            // int codec = CV_FOURCC('M', 'J', 'P', 'G');  // select desired codec (must be available at runtime)
            // double fps = 30.0;                          // framerate of the created video stream
            // writer.open(fname, codec, fps, m.first.size(), false);
            // check if we succeeded
            if (!writer->isOpened()) {
                std::cerr << "Could not open the output video file for write\n"<<std::endl;
                return;
            }
            cout << "Writing videofile: " << fname << endl;
            writer->write(m_s);
            /* added by RF - end */
        }
    } 

    if(n++ == max_duration)
    {
        std::cerr << "Max video segment duration reached. Truncating." << std::endl;
        stop();
    } else
    {
        (*writer) << m.first;
        n++;
    }
}

void ISentry::VideoRecorder::start()
{
    n=0;
    FrameProcessor::start();
}

void ISentry::VideoRecorder::stop()
{
    std::cerr << "Closing video segment." << std::endl;
    n=0;
    FrameProcessor::stop();
    // writer->release(); // added by RF
    if(writer)
    {
        delete writer;
        writer = NULL;
    }
}


