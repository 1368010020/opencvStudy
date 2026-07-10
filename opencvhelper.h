#ifndef OPENCVHELPER_H
#define OPENCVHELPER_H

#include <QImage>

#include <opencv2/opencv.hpp>

class OpenCVHelper
{
public:
    // Mat 转 QImage
    static QImage matToQImage(const cv::Mat &mat);

    // QImage 转 Mat
    static cv::Mat qImageToMat(const QImage &image);
};

#endif // OPENCVHELPER_H
