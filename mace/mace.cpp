#include <math.h>
#include <float.h>
#include <limits.h>
#include <stdio.h>
#include "opencv2/opencv.hpp"
#include "mace.h"

using namespace cv;

bool DBGDRAW=0;

//
//! Rearrange the quadrants of Fourier image
//!  so that the origin is at the image center
//
void shiftDFT(const Mat &src, Mat &dst) {
    Size size = src.size();

    if (dst.empty() || (dst.size().width != size.width || dst.size().height != size.height)) {
        dst.create(src.size(), src.type());
    }

    int cx = size.width/2;
    int cy = size.height/2; // image center

    Mat q1 = src(Rect(0, 0, cx,cy));
    Mat q2 = src(Rect(cx,0, cx,cy));
    Mat q3 = src(Rect(cx,cy,cx,cy));
    Mat q4 = src(Rect(0, cy,cx,cy));
    Mat d1 = dst(Rect(0, 0, cx,cy));
    Mat d2 = dst(Rect(cx,0, cx,cy));
    Mat d3 = dst(Rect(cx,cy,cx,cy));
    Mat d4 = dst(Rect(0, cy,cx,cy));

    if (src.data != dst.data){
        q3.copyTo(d1);
        q4.copyTo(d2);
        q1.copyTo(d3);
        q2.copyTo(d4);
    } else {
        Mat tmp;
        q3.copyTo(tmp);
        q1.copyTo(d3);
        tmp.copyTo(d1);
        q4.copyTo(tmp);
        q2.copyTo(d4);
        tmp.copyTo(d2);
    }
}



struct MACEImpl : MACE {
    int IMGSIZE;            // images will get resized to this
    Mat_<Vec2d> maceFilter; // filled from compute()
    Mat convFilter;         // optional random convolution (cancellable)
    double threshold;       // minimal "sameness" threshold from the train images


    MACEImpl(int siz) : IMGSIZE(siz), threshold(DBL_MAX) {
    }

    void salt(int salz) {
        if (!salz) return;
        theRNG().state = 5*salz<<7;
        convFilter.create(IMGSIZE, IMGSIZE, CV_64F);
        randn(convFilter, 0, 1.0/(IMGSIZE*IMGSIZE));
        if (DBGDRAW) {
            imshow("FIL", 0.5 + 1000*convFilter);
        }
    }

    Mat dftImage(Mat img) const {
        Mat gray;
        resize(img, gray, Size(IMGSIZE,IMGSIZE)) ;
        if (gray.channels() > 1)
            cvtColor(gray, gray, COLOR_BGR2GRAY);
        equalizeHist(gray, gray);
        gray.convertTo(gray, CV_64F);
        if (! convFilter.empty()) { // optional, but unfortunately, it has to happen after resize/equalize ops.
            filter2D(gray, gray, CV_64F, convFilter);
        }
        if (DBGDRAW) {
            imshow("ORG",gray*(convFilter.empty() ? 1 : 100));
        }
        Mat input[2] = {gray, Mat(gray.size(), gray.type(), 0.0)};
        Mat complexInput;
        merge(input, 2, complexInput);

        Mat_<Vec2d> dftImg(IMGSIZE*2, IMGSIZE*2, 0.0);
        complexInput.copyTo(dftImg(Rect(0,0,IMGSIZE,IMGSIZE)));

        dft(dftImg, dftImg);
        return dftImg;
    }


    void compute(std::vector<Mat> images) {
        int size = images.size();
        int IMGSIZE_2X = IMGSIZE * 2;
        int TOTALPIXEL = IMGSIZE_2X * IMGSIZE_2X;

        Mat_<Vec2d> D(TOTALPIXEL, 1, 0.0);
        Mat_<Vec2d> S(TOTALPIXEL, size, 0.0);
        Mat_<Vec2d> SPLUS(size, TOTALPIXEL, 0.0);

        for (size_t i=0; i<size; i++) {
            Mat_<Vec2d> dftImg = dftImage(images[i]);
            for (int l=0; l<IMGSIZE_2X; l++) {
                for (int m=0; m<IMGSIZE_2X; m++) {
                    int j = l * IMGSIZE_2X + m;
                    Vec2d s = dftImg(l, m);
                    S(j, i) = s;
                    SPLUS(i, j) = Vec2d(s[0], -s[1]);

                    double val=((pow(s[0],2) + pow(s[1],2)));
                    D(j, 0)[0] += val;
                }
            }
        }

        Mat sq; cv::sqrt(D, sq);
        Mat_<Vec2d> DINV = TOTALPIXEL * size / sq;

        Mat_<Vec2d> DINV_S(TOTALPIXEL, size, 0.0);
        //Mat_<Vec2d> DINV_S = DINV * S;
        Mat_<Vec2d> SPLUS_DINV(size, TOTALPIXEL, 0.0);
        for (int l=0; l<size; l++) {
            for (int m=0; m<TOTALPIXEL; m++) {
                SPLUS_DINV(l, m)[0] = SPLUS(l,m)[0] * DINV(m,0)[0];
                SPLUS_DINV(l, m)[1] = SPLUS(l,m)[1] * DINV(m,0)[1];
                DINV_S(m, l)[0] = S(m,l)[0] * DINV(m,0)[0];
                DINV_S(m, l)[1] = S(m,l)[1] * DINV(m,0)[1];
            }
        }

        Mat_<Vec2d> SPLUS_DINV_S = SPLUS_DINV * S;
        Mat_<Vec2d> SPLUS_DINV_S_INV(size, size);
        Mat_<double> SPLUS_DINV_S_INV_1(2*size, 2*size);
        for (size_t l=0; l<size; l++) {
            for (size_t m=0; m<size; m++) {
                Vec2d s = SPLUS_DINV_S(l, m);
                SPLUS_DINV_S_INV_1(l,      m)      = s[0];
                SPLUS_DINV_S_INV_1(l+size, m+size) = s[0];
                SPLUS_DINV_S_INV_1(l,      m+size) = s[1];
                SPLUS_DINV_S_INV_1(l+size, m)     = -s[1];
            }
        }
        invert(SPLUS_DINV_S_INV_1, SPLUS_DINV_S_INV_1);

        for (int l=0; l<size; l++) {
            for (int m=0; m<size; m++) {
                SPLUS_DINV_S_INV(l, m) = Vec2d(SPLUS_DINV_S_INV_1(l,m), SPLUS_DINV_S_INV_1(l,m+size));
            }
        }

        Mat_<Vec2d> Hmace = DINV_S * SPLUS_DINV_S_INV;
        Mat_<Vec2d> C(size,1, Vec2d(1,0));
        maceFilter = Mat(Hmace * C).reshape(2,IMGSIZE_2X);
    }


    double computeThreshold(const std::vector<Mat> &images) const {
        double best=DBL_MAX;
        for (size_t i=0; i<images.size(); i++) {
            double d = correlate(images[i]);
            if (d < best) {
                best = d;
            }
        }
        if (best>9999999) {
            best = 1.0;
        }
        return best;
    }


    double correlate(const Mat &img) const {
        //CV_Assert(! maceFilter.empty()); // not trained.
        if (maceFilter.empty()) return -1; // not trained.
        int  IMGSIZE_2X = IMGSIZE * 2;
        Mat dftImg = dftImage(img);
        mulSpectrums(dftImg, maceFilter, dftImg, CV_DXT_MUL_CONJ);
        dft(dftImg, dftImg, CV_DXT_INV_SCALE, 0);
        Mat chn[2];
        split(dftImg, chn);
        Mat_<double> re;
        shiftDFT(chn[0], re);
        double m1,M1;
        minMaxLoc(re, &m1, &M1, 0, 0);
        double peakCorrPlaneEnergy = M1 / sqrt(sum(re)[0]);
        re -= m1;
        if (DBGDRAW) {
            imshow("RE",re*10000);
            int k = waitKey(50);
            if (k=='d') { DBGDRAW = false; destroyAllWindows(); }
        }
        double value=0;
        double num=0;
        int rad1=int(floor((double)(45.0/64.0)*(double)IMGSIZE));
        int rad2=int(floor((double)(27.0/64.0)*(double)IMGSIZE));
        std::vector<float> r2(IMGSIZE_2X); // cache a few pow's
        for (int l=0; l<IMGSIZE_2X; l++) {
            r2[l] = (l-IMGSIZE) * (l-IMGSIZE);
        }
        for (int l=0; l<IMGSIZE_2X; l++) {
            for (int m=0; m<IMGSIZE_2X; m++) {
                double rad=sqrt(r2[m] + r2[l]);
                if (rad < rad1) {
                    if (rad > rad2) {
                        value += re(l,m);
                        num++;
                    }
                }
            }
        }
        value /= num;

        double std2=0;
        for (int l=0; l<IMGSIZE_2X; l++) {
            for (int m=0; m<IMGSIZE_2X; m++) {
                double rad=sqrt(r2[m] + r2[l]);
                if (rad < rad1) {
                    if (rad > rad2) {
                        double d = (value - re(l,m));
                        std2 += d * d;
                    }
                }
            }
        }
        std2 /= num;
        std2 = sqrt(std2);
        double sca = re(IMGSIZE, IMGSIZE);
        double peakToSideLobeRatio = (sca - value) / std2;

        return 100.0 * peakToSideLobeRatio * peakCorrPlaneEnergy;
    }

    // MACE interface
    void train(InputArrayOfArrays input) {
        std::vector<Mat> images;
        input.getMatVector(images);
        compute(images);
        threshold = computeThreshold(images); // todo: cache dft images
    }
    bool same(InputArray img) const {
        return correlate(img.getMat()) >= threshold;
    }

    // cv::Algorithm:
    void clear() {
        maceFilter.release();
        convFilter.release();
    }
    void write(cv::FileStorage &fs) const {
        fs << "mace" << maceFilter;
        fs << "conv" << convFilter;
        fs << "threshold" << threshold;
    }
    void read(const cv::FileNode &fn) {
        fn["mace"] >> maceFilter;
        fn["conv"] >> convFilter;
        fn["threshold"] >> threshold;
        IMGSIZE = maceFilter.cols/2;
    }
};


cv::Ptr<MACE> MACE::create(int siz) {
    return makePtr<MACEImpl>(siz);
}




//
// for even more accuracy, use several mace filters
//   (it might miss some positives, but it must **never** have false positives !)
//
struct MaceSampler : MACE {
    struct Sampler {
        Ptr<MACE> mace;
        Rect2f r; // [0..1]
        Mat sample(const Mat &img) const {
            return img(Rect(r.x*img.cols, r.y*img.rows, r.width*img.cols, r.height*img.rows));
        }
    };
    int siz;
    std::vector<Sampler> samp;

    MaceSampler(int siz, const std::vector<Rect2f> &rects) : siz(siz) {
        for (size_t i=0; i<rects.size(); i++) {
            samp.push_back({
                MACE::create(siz * rects[i].width),
                rects[i]
            });
        }
    }
    void salt(int salz) {
        for (size_t s=0; s<samp.size(); s++) {
            samp[s].mace->salt(salz);
        }
    }
    void train(cv::InputArrayOfArrays input) {
        std::vector<Mat> images;
        input.getMatVector(images);
        for (size_t s=0; s<samp.size(); s++) {
            if (samp[s].r.width < 1.0f) { // we need patches
                std::vector<Mat> vm;
                for (size_t i=0; i<images.size(); i++) {
                    vm.push_back(samp[s].sample(images[i]));
                }
                samp[s].mace->train(vm);
            } else {
                samp[s].mace->train(images);
            }
        }
    }
    bool same(InputArray _img) const { // all must agree.
        Mat img = _img.getMat();
        for (size_t s=0; s<samp.size(); s++) {
            Mat roi = samp[s].sample(img);
            if (! samp[s].mace->same(roi)) {
                return false;
            }
        }
        return true;
    }
    void write(cv::FileStorage &fs) const { // make it a "list of objects"
        fs << "filters" << "[";
        for (size_t i=0; i<samp.size(); i++) {
            fs << "{:";
            samp[i].mace->write(fs);
            fs << "roi" << samp[i].r;
            fs << "}";
        }
        fs << "]";
    }
    void read(const cv::FileNode &fn) {
        FileNode n = fn["filters"];
        int i=0;
        for (FileNodeIterator it=n.begin(); it!=n.end(); ++it,++i) {
            samp[i].mace->read(*it);
            (*it)["roi"] >> samp[i].r;
        }
    }
};

cv::Ptr<MACE> MACE::createSampler(int siz, cv::InputArray r) {
    std::vector<cv::Rect2f> rects = r.getMat();
    return cv::makePtr<MaceSampler>(siz, rects);
}
