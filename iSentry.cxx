#include <cv.h>
#include <highgui.h>
#include <iostream>
#include <unistd.h>

#include "iSentryConfig.hxx"
#include "MotionDetector.hxx"
#include "MotionEstimator.hxx"
#include "ImageRecorder.hxx"
#include "VideoRecorder.hxx"
#include "cvplot.hxx"

using namespace cv;
using namespace std;
using namespace ISentry;

#define CONFIG_FILE_NAME "isentry.cfg"

class SimplePreview:public FrameProcessor
{

public:

    SimplePreview(MotionState *_ms, const libconfig::Setting& cfg) : ms(_ms)
    {
        nframes = (int)cfg["frames"];
        reset();
        namedWindow("preview",1);
        namedWindow("diff",1);
        namedWindow("With Contours", 1);
        CvPlot::Figure *f = CvPlot::getPlotManager()->AddFigure("motion");
        f->setCustomYRange(0,1);
        float threshold;
        if(cfg.lookupValue("threshold",threshold))
        {
            for(size_t i=0;i<nframes;i++)
                threshold_points.push_back(threshold);
        }
    }

    virtual void stop()
    {
        FrameProcessor::stop();
        reset();
    }

    virtual void addFrame(std::pair<cv::Mat,time_t> &m)
    {
        if(!ms || !running)
            return;
         
        std::vector<float> signals = ms->getSignals();
        std::vector<cv::Mat> frames = ms->getFrames();
        
        while(motion.size()>=nframes)
            motion.pop_back();
        motion.insert(motion.begin(),signals[0]);

        while(raw_motion.size()>=nframes)
            raw_motion.pop_back();
        raw_motion.insert(raw_motion.begin(),signals[1]);

        if(!motion.empty())
        {
            CvPlot::Figure *f = CvPlot::getPlotManager()->FindFigure("motion");
            f->Clear();
            CvPlot::plot("motion", &(*(motion.begin())), motion.size());
            CvPlot::plot("motion", &(*(raw_motion.begin())), raw_motion.size());
            if(!threshold_points.empty())
                CvPlot::plot("motion", &(*(threshold_points.begin())), threshold_points.size());
        }
        imshow("preview", frames[0]);
        if(signals[0]>0)
        {
            const Mat &diff = frames[1];
            Mat idiff;
            diff.convertTo(idiff,m.first.type());
            imshow("diff", idiff);
            /** add_contours */
            Mat frame_with_contours=frames[0];
            Mat canny_output;
            vector<vector<Point> > contours;
            vector<Vec4i> hierarchy;
            int thresh = 100;
            /// Detect edges using canny
            Canny( idiff, canny_output, thresh, thresh*2, 3 );
            /// Find contours
            findContours( canny_output, contours, hierarchy, CV_RETR_TREE, CV_CHAIN_APPROX_SIMPLE, Point(0, 0) );

            /// Draw contours
            Mat drawing = Mat::zeros( canny_output.size(), CV_8UC3 );
            for( unsigned int i = 0; i< contours.size(); i++ ) {
                Scalar color = Scalar( 0, 255, 0 );
                drawContours( frame_with_contours, contours, i, color, 2, 8, hierarchy, 0, Point() );
                /* bounding rectangle */
                Rect rect=boundingRect(contours[i]);
                rectangle(frame_with_contours,rect,CV_RGB(255,0,0),1,8,0);
            }
            imshow( "With Contours", frame_with_contours );
        }
    }

private:

    void reset()
    {
        motion.clear();
        raw_motion.clear();
        for(size_t i=0;i<nframes;i++)
        {
            motion.insert(motion.begin(),0);
            raw_motion.insert(raw_motion.begin(),0);
        }
    }

    vector<float> motion;
    vector<float> raw_motion;
    vector<float> threshold_points;
    MotionState *ms;    
    size_t nframes;
};

void usage()
{
    std::cerr << "Usage: ./iSentry [-c config_filename] [videofile]" << std::endl;
}

int main(int argc, char**argv)
{
    int ch;
    std::cerr<<"OPENCV version::"<<CV_VERSION<<std::endl;
    const char *cfile=CONFIG_FILE_NAME;
    while ((ch = getopt(argc, argv, "c:")) != -1)
    {
        switch (ch)
        {
        case 'c':
            cfile = strdup(optarg);
            break;
        case '?':
        default:
            usage();
            exit(2);
        }
    }
    argc -= optind;
    argv += optind;

    iSentryConfig *iconf = iSentryConfig::getInstance();
    try
    {
        iconf->readFile(cfile);
    } catch(const libconfig::FileIOException &fioex)
    {
        std::cerr << "Error reading config file." << std::endl;
        exit(2);
    }
    iconf->setAutoConvert(true);
    const libconfig::Setting& cfg = iconf->getRoot();

    VideoCapture cap;
    if(argc<=0)
    {
        std::cerr << "Reading camera " << std::endl;
        // reading from camera
        cap.open((int)(cfg["application"]["camera_device_id"]));
    } else
    {
        //reading from file
        std::cerr << "Reading file " << argv[0] << std::endl;
        cap.open(argv[0]);
    }
    if(!cap.isOpened())  // check if we succeeded
        exit(-1);

    MotionEstimator me(cfg["motion_detection"]);
    MotionDetector md(&me, cfg["motion_detection"]);

    ImageRecorder irec(cfg["image_recording"]);
    if(cfg["image_recording"]["enabled"])
        md.addChild(&irec);

    VideoRecorder vrec(cfg["video_recording"]);
    if(cfg["video_recording"]["enabled"])
        md.addChild(&vrec);

    FrameProcessor isentry;
    isentry.addChild(&me);
    isentry.addChild(&md);

    SimplePreview *preview;
    bool show_preview = false;
    cfg["application"].lookupValue("show_GUI",show_preview);
    if(show_preview)
    {
        preview = new SimplePreview(&me,cfg["preview"]);
        isentry.addChild(preview);
    } else
        preview = NULL;

    isentry.start();

    for(;;)
    {
        Mat frame;
        cap >> frame; // get a new frame from camera
        pair<cv::Mat,time_t> tframe = make_pair(frame,time(NULL));
        isentry.addFrame(tframe);

        int key = waitKey(1); // 30
        if(key >= 0)
        {
            if((char)key=='q') {
                std::cerr << "Stopping..."<<std::endl;
                isentry.stop();
            }
            else if((char)key=='a') {
                std::cerr << "Starting..."<<std::endl;
                isentry.start();
            }
            else {
                std::cerr << "Break..."<<std::endl;
                break;
            }
        }
    }

    isentry.clearChildren();
    delete preview;

    // the camera will be deinitialized automatically in VideoCapture destructor
    return 0;
}

