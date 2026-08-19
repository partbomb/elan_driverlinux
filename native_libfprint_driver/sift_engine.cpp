#include "sift_engine.h"
#include <opencv2/opencv.hpp>
#include <libusb-1.0/libusb.h>
#include <vector>
#include <thread>
#include <chrono>

using namespace std;
using namespace cv;

Mat process_image(const Mat& img) {
    Mat blur, out;
    GaussianBlur(img, blur, Size(5, 5), 0);
    Ptr<CLAHE> clahe = createCLAHE(2.0, Size(8, 8));
    clahe->apply(blur, out);
    return out;
}

bool capture_usb_images(vector<Mat>& out_images, int required_touches, int timeout_sec) {
    libusb_context *ctx = nullptr;
    if (libusb_init(&ctx) < 0) return false;

    libusb_device_handle *dev = libusb_open_device_with_vid_pid(ctx, 0x04f3, 0x0c4f);
    if (!dev) {
        libusb_exit(ctx);
        return false;
    }

    if (libusb_kernel_driver_active(dev, 0) == 1) {
        libusb_detach_kernel_driver(dev, 0);
    }
    libusb_set_configuration(dev, 1);
    libusb_claim_interface(dev, 0);

    int transferred = 0;
    unsigned char wake_cmd[] = {0x40, 0x31};
    libusb_bulk_transfer(dev, 0x01, wake_cmd, sizeof(wake_cmd), &transferred, 1000);
    this_thread::sleep_for(chrono::milliseconds(300));

    unsigned char req1[] = {0x40, 0x3f};
    unsigned char req2[] = {0x00, 0x09};
    unsigned char buffer[12800];
    
    int touches = 0;
    bool waiting_for_off = false;
    auto start_time = chrono::steady_clock::now();

    while (touches < required_touches && 
           chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - start_time).count() < timeout_sec) {
        
        libusb_bulk_transfer(dev, 0x01, req1, sizeof(req1), &transferred, 1000);
        libusb_bulk_transfer(dev, 0x01, req2, sizeof(req2), &transferred, 1000);
        
        int res = libusb_bulk_transfer(dev, 0x82, buffer, sizeof(buffer), &transferred, 1000);
        if (res == 0 && transferred == 12800) {
            Mat raw(80, 80, CV_16UC1, buffer);
            Scalar mean, stddev;
            meanStdDev(raw, mean, stddev);
            
            if (waiting_for_off) {
                if (stddev.val[0] < 150.0) {
                    waiting_for_off = false;
                    this_thread::sleep_for(chrono::milliseconds(200));
                }
            } else {
                if (stddev.val[0] > 300.0) {
                    Mat norm_img;
                    normalize(raw, norm_img, 0, 255, NORM_MINMAX, CV_8UC1);
                    out_images.push_back(norm_img);
                    touches++;
                    waiting_for_off = true;
                }
            }
        }
        this_thread::sleep_for(chrono::milliseconds(50));
    }

    libusb_release_interface(dev, 0);
    libusb_close(dev);
    libusb_exit(ctx);
    
    return touches > 0;
}

extern "C" {

int sift_engine_init(void) { return 1; }

int sift_engine_enroll(unsigned char** out_data) {
    vector<Mat> images;
    if (!capture_usb_images(images, 5, 30)) return 0;

    Ptr<SIFT> sift = SIFT::create();
    Mat super_des;

    for (const auto& img : images) {
        Mat clean_img = process_image(img);
        vector<KeyPoint> kp;
        Mat des;
        sift->detectAndCompute(clean_img, noArray(), kp, des);

        if (!des.empty() && kp.size() >= 5) {
            if (super_des.empty()) {
                super_des = des;
            } else {
                vconcat(super_des, des, super_des);
            }
        }
    }

    if (super_des.empty()) return 0;

    int size = super_des.total() * super_des.elemSize();
    int header_size = 3 * sizeof(int);
    int total_size = header_size + size;
    
    *out_data = (unsigned char*)malloc(total_size);
    int* header = (int*)(*out_data);
    header[0] = super_des.rows;
    header[1] = super_des.cols;
    header[2] = super_des.type();
    
    memcpy(*out_data + header_size, super_des.data, size);
    return total_size;
}

int sift_engine_verify(const unsigned char* saved_data, int data_size) {
    if (data_size < 12) return 0;

    const int* header = (const int*)saved_data;
    int rows = header[0], cols = header[1], type = header[2];
    
    if (12 + (rows * cols * CV_ELEM_SIZE(type)) != data_size) return 0;
    Mat saved_des(rows, cols, type);
    memcpy(saved_des.data, saved_data + 12, data_size - 12);

    vector<Mat> images;
    if (!capture_usb_images(images, 1, 10)) return 0;

    Mat clean_img = process_image(images[0]);
    Ptr<SIFT> sift = SIFT::create();
    vector<KeyPoint> kp;
    Mat current_des;
    sift->detectAndCompute(clean_img, noArray(), kp, current_des);

    if (current_des.empty() || kp.size() < 5) return 0;

    BFMatcher matcher(NORM_L2);
    vector<vector<DMatch>> knn_matches;
    matcher.knnMatch(saved_des, current_des, knn_matches, 2);

    int good_matches = 0;
    for (size_t i = 0; i < knn_matches.size(); i++) {
        // УЖЕСТОЧЕНИЕ: Точка считается уникальной, только если она на 40% лучше всех остальных вариантов
        if (knn_matches[i][0].distance < 0.60f * knn_matches[i][1].distance) { 
            good_matches++;
        }
    }

    // УЖЕСТОЧЕНИЕ: Требуем минимум 15 абсолютно уникальных совпадений
    return (good_matches >= 15) ? 1 : 0;
}

}