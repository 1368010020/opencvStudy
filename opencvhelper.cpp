#include "opencvhelper.h"

QImage OpenCVHelper::matToQImage(const cv::Mat &mat)
{
    if (mat.empty())
        return QImage();

    cv::Mat rgb;

    if (mat.channels() == 1) {
        cv::cvtColor(mat, rgb, cv::COLOR_GRAY2RGB);

    } else if (mat.channels() == 3) {
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);

    } else {
        return QImage();
    }

    return QImage(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888).copy();
}

cv::Mat OpenCVHelper::qImageToMat(const QImage &image)
{
    if (image.isNull())
        return cv::Mat();

    QImage rgb = image.convertToFormat(QImage::Format_RGB888);

    cv::Mat mat(rgb.height(), rgb.width(), CV_8UC3, rgb.bits(), rgb.bytesPerLine());

    return mat.clone();
}
