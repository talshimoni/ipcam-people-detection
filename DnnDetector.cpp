#include "DnnDetector.h"


void DnnDetector::init(float confThreshold_, 
    std::vector<std::string> classes_, 
    Scalar mean_, 
    float scale_,
    bool swapRB_, 
    int inpWidth_, 
    int inpHeight_){
  confThreshold = confThreshold_;
  classes = classes_;
  scale = scale_;
  mean = mean_;
  swapRB = swapRB_;
  inpWidth = inpWidth_;
  inpHeight = inpHeight_;  
}



void DnnDetector::postprocess(Mat& frame, const Mat& out, Net& net)
{
    static std::vector<int> outLayers = net.getUnconnectedOutLayers();
    static std::string outLayerType = net.getLayer(outLayers[0])->type;

    float* data = (float*)out.data;
    if (net.getLayer(0)->outputNameToIndex("im_info") != -1)  // Faster-RCNN or R-FCN
    {
        // Network produces output blob with a shape 1x1xNx7 where N is a number of
        // detections and an every detection is a vector of values
        // [batchId, classId, confidence, left, top, right, bottom]
        for (size_t i = 0; i < out.total(); i += 7)
        {
            float confidence = data[i + 2];
            if (confidence > confThreshold)
            {
                int left = (int)data[i + 3];
                int top = (int)data[i + 4];
                int right = (int)data[i + 5];
                int bottom = (int)data[i + 6];
                int classId = (int)(data[i + 1]) - 1;  // Skip 0th background class id.
                drawPred(classId, confidence, left, top, right, bottom, frame);
            }
        }
    }
    else if (outLayerType == "DetectionOutput")
    {
        // Network produces output blob with a shape 1x1xNx7 where N is a number of
        // detections and an every detection is a vector of values
        // [batchId, classId, confidence, left, top, right, bottom]
        for (size_t i = 0; i < out.total(); i += 7)
        {
            float confidence = data[i + 2];
            if (confidence > confThreshold)
            {
                int left = (int)(data[i + 3] * frame.cols);
                int top = (int)(data[i + 4] * frame.rows);
                int right = (int)(data[i + 5] * frame.cols);
                int bottom = (int)(data[i + 6] * frame.rows);
                int classId = (int)(data[i + 1]) - 1;  // Skip 0th background class id.
                drawPred(classId, confidence, left, top, right, bottom, frame);
            }
        }
    }
    else if (outLayerType == "Region")
    {
        // Network produces output blob with a shape NxC where N is a number of
        // detected objects and C is a number of classes + 4 where the first 4
        // numbers are [center_x, center_y, width, height]
        for (int i = 0; i < out.rows; ++i, data += out.cols)
        {
            Mat confidences = out.row(i).colRange(5, out.cols);
            Point classIdPoint;
            double confidence;
            minMaxLoc(confidences, 0, &confidence, 0, &classIdPoint);
            if (confidence > confThreshold)
            {
                int classId = classIdPoint.x;
                int centerX = (int)(data[0] * frame.cols);
                int centerY = (int)(data[1] * frame.rows);
                int width = (int)(data[2] * frame.cols);
                int height = (int)(data[3] * frame.rows);
                int left = centerX - width / 2;
                int top = centerY - height / 2;
                drawPred(classId, (float)confidence, left, top, left + width, top + height, frame);
            }
        }
    }
    else
        CV_Error(Error::StsNotImplemented, "Unknown output layer type: " + outLayerType);
}

void DnnDetector::drawPred(int classId, float conf, int left, int top, int right, int bottom, Mat& frame)
{
    rectangle(frame, Point(left, top), Point(right, bottom), Scalar(0, 255, 0));

    std::string label = format("%.2f", conf);
    if (!classes.empty())
    {
        CV_Assert(classId < (int)classes.size());
        label = classes[classId] + ": " + label;
    }

    int baseLine;
    Size labelSize = getTextSize(label, FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);

    top = max(top, labelSize.height);
    rectangle(frame, Point(left, top - labelSize.height),
              Point(left + labelSize.width, top + baseLine), Scalar::all(255), FILLED);
    putText(frame, label, Point(left, top), FONT_HERSHEY_SIMPLEX, 0.5, Scalar());
}

void DnnDetector::callback(int pos, void*)
{
    confThreshold = pos * 0.01f;
}



static std::vector<String> getOutputsNames(const Net& net)
{
    static std::vector<String> names;
    if (names.empty())
    {
        std::vector<int> outLayers = net.getUnconnectedOutLayers();
        std::vector<String> layersNames = net.getLayerNames();
        names.resize(outLayers.size());
        for (size_t i = 0; i < outLayers.size(); ++i)
            names[i] = layersNames[outLayers[i] - 1];
    }
    return names;
}

Mat DnnDetector::run_dnn_detection(Mat frame){
  // Create a 4D blob from a frame.

  Mat blob;  
  Size inpSize(inpWidth > 0 ? inpWidth : frame.cols,
      inpHeight > 0 ? inpHeight : frame.rows);
  blobFromImage(frame, blob, scale, inpSize, mean, swapRB, false);
 // Run a model.
  net.setInput(blob);
  if (net.getLayer(0)->outputNameToIndex("im_info") != -1)  // Faster-RCNN or R-FCN
  {
      resize(frame, frame, inpSize);
      Mat imInfo = (Mat_<float>(1, 3) << inpSize.height, inpSize.width, 1.6f);
      net.setInput(imInfo, "im_info");
  }
  std::vector<Mat> outs;
  net.forward(outs, getOutputsNames(net));

  postprocess(frame, outs, net);

  // Put efficiency information.
  std::vector<double> layersTimes;
  double freq = getTickFrequency() / 1000;
  double t = net.getPerfProfile(layersTimes) / freq;
  std::string label = format("Inference time: %.2f ms", t);
  putText(frame, label, Point(0, 15), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(0, 255, 0));
  return frame;

}
