#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <vector>

int main() {
    const cv::Mat identity = cv::Mat::eye(2, 2, CV_8UC1);
    cv::Mat resized;
    cv::resize(identity, resized, cv::Size(8, 8), 0.0, 0.0, cv::INTER_NEAREST);

    std::vector<unsigned char> encoded;
    const bool encoded_png = cv::imencode(".png", resized, encoded);
    return resized.rows == 8 && resized.cols == 8 && encoded_png && !encoded.empty()
               ? 0
               : 1;
}
