#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextBrowser>

#include "fluentstyle.h"
#include "opencvhelper.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <algorithm>
#include <functional>

namespace {
// OperationType 存在 navList 每个条目的 Qt::UserRole 里
constexpr int kOpRole = Qt::UserRole;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setStyleSheet(FluentStyle::styleSheet());

    // 使用程序目录下的 config.ini
    m_settings = new QSettings("./config.ini", QSettings::IniFormat, this);

    // ---- 左侧导航 ----
    buildNavList();
    connect(ui->navList, &QListWidget::currentRowChanged, this, &MainWindow::onNavRowChanged);

    connect(ui->openImageButton, &QPushButton::clicked, this, &MainWindow::openImage);

    // ---- 保存结果按钮：插在"打开图片"下面、导航列表上面 ----
    m_saveResultButton = new QPushButton("保存结果...", ui->leftPanel);
    m_saveResultButton->setProperty("variant", "secondary");
    connect(m_saveResultButton, &QPushButton::clicked, this, &MainWindow::saveResult);
    ui->leftPanelLayout->insertWidget(2, m_saveResultButton);

    // ---- 术语手册按钮：插在"保存结果"下面 ----
    m_glossaryButton = new QPushButton("术语手册", ui->leftPanel);
    m_glossaryButton->setProperty("variant", "secondary");
    connect(m_glossaryButton, &QPushButton::clicked, this, &MainWindow::showTermsGlossary);
    ui->leftPanelLayout->insertWidget(3, m_glossaryButton);

    // ---- 图片来源：静态图片 / 摄像头实时 / 视频文件 ----
    m_staticImageRadio = ui->staticImageRadio;
    m_cameraRadio = ui->cameraRadio;
    m_videoFileRadio = new QRadioButton("视频文件", ui->leftPanel);
    ui->sourceModeLayout->addWidget(m_videoFileRadio);
    connect(m_staticImageRadio, &QRadioButton::toggled, this, &MainWindow::onSourceModeChanged);
    connect(m_cameraRadio, &QRadioButton::toggled, this, &MainWindow::onSourceModeChanged);
    connect(m_videoFileRadio, &QRadioButton::toggled, this, &MainWindow::onSourceModeChanged);

    m_cameraTimer = new QTimer(this);
    connect(m_cameraTimer, &QTimer::timeout, this, &MainWindow::onCameraFrame);

    // 图片显示区域设置
    ui->originalImageLabel->setAlignment(Qt::AlignCenter);
    ui->originalImageLabel->setScaledContents(false);
    ui->originalImageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    ui->processedImageLabel->setAlignment(Qt::AlignCenter);
    ui->processedImageLabel->setScaledContents(false);
    ui->processedImageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // 取色器：给两个图片 label 开鼠标追踪，交给 eventFilter 处理移动/离开事件
    ui->originalImageLabel->setMouseTracking(true);
    ui->processedImageLabel->setMouseTracking(true);
    ui->originalImageLabel->installEventFilter(this);
    ui->processedImageLabel->installEventFilter(this);

    buildParamPanels();

    // ---- 常驻代码面板：显示当前算子对应的真实 OpenCV 调用 ----
    QLabel *codeSnippetTitle = new QLabel("对应代码:", ui->rightPanel);
    codeSnippetTitle->setProperty("role", "hint");
    ui->rightPanelLayout->addWidget(codeSnippetTitle);

    m_codeSnippetText = new QPlainTextEdit(ui->rightPanel);
    m_codeSnippetText->setObjectName("codeSnippetText");
    m_codeSnippetText->setReadOnly(true);
    m_codeSnippetText->setMaximumHeight(110);
    m_codeSnippetText->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    ui->rightPanelLayout->addWidget(m_codeSnippetText);

    // 三栏比例：左侧导航固定较窄，中间对比区自适应拉伸，右侧参数面板固定较窄
    ui->leftPanel->setMinimumWidth(190);
    ui->rightPanel->setMinimumWidth(260);
    ui->mainSplitter->setStretchFactor(0, 0);
    ui->mainSplitter->setStretchFactor(1, 1);
    ui->mainSplitter->setStretchFactor(2, 0);
    ui->mainSplitter->setSizes({210, 700, 280});

    // 读取上一次图片目录
    m_lastOpenDir = m_settings->value("File/LastImageDirectory", QDir::homePath()).toString();
    ui->infoText->appendPlainText("OpenCV学习程序启动");
    ui->infoText->appendPlainText("提示：选中左侧算子后，参数面板顶部会推荐一张最适合它的测试图，点一下就能加载");

    resize(m_settings->value("Window/Width", 1360).toInt(),
           m_settings->value("Window/Height", 860).toInt());

    // 默认选中第一个可选条目（原图），触发一次面板/显示同步
    for (int row = 0; row < ui->navList->count(); ++row) {
        if (ui->navList->item(row)->flags() & Qt::ItemIsSelectable) {
            ui->navList->setCurrentRow(row);
            break;
        }
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::openImage()
{
    if (!m_staticImageRadio->isChecked())
        return;

    QString filename = QFileDialog::getOpenFileName(this,
                                                    "选择图片",
                                                    m_lastOpenDir,
                                                    "Images (*.png *.jpg *.jpeg *.bmp)");

    if (filename.isEmpty()) {
        return;
    }
    // 保存当前目录
    QFileInfo fileInfo(filename);

    m_lastOpenDir = fileInfo.absolutePath();

    // 保存到ini
    m_settings->setValue("File/LastImageDirectory", m_lastOpenDir);
    m_settings->setValue("File/LastImagePath", filename);

    showImage(filename);
}

void MainWindow::selectTemplateImage()
{
    QString filename = QFileDialog::getOpenFileName(this,
                                                    "选择模板图片",
                                                    m_lastOpenDir,
                                                    "Images (*.png *.jpg *.jpeg *.bmp)");
    if (filename.isEmpty())
        return;

    cv::Mat tmpl = cv::imread(filename.toLocal8Bit().toStdString());
    if (tmpl.empty()) {
        ui->infoText->appendPlainText("模板图片读取失败: " + filename);
        return;
    }

    m_templateMat = tmpl;
    m_templateStatusLabel->setText("当前模板: " + QFileInfo(filename).fileName());
    ui->infoText->appendPlainText("已加载模板图片: " + filename);

    applyCurrentOperation();
}

QString MainWindow::resolveTestDataPath(const QString &filename) const
{
    QDir dir(QCoreApplication::applicationDirPath());

    for (int i = 0; i < 5; ++i) {
        QString candidate = dir.filePath("test_data/" + filename);
        if (QFile::exists(candidate))
            return candidate;
        if (!dir.cdUp())
            break;
    }

    return QString();
}

void MainWindow::loadRecommendedImage(const QString &filename)
{
    if (m_cameraRadio->isChecked()) {
        ui->infoText->appendPlainText("摄像头模式下无法加载图片文件，请先切回静态图片模式");
        return;
    }

    QString path = resolveTestDataPath(filename);
    if (path.isEmpty()) {
        ui->infoText->appendPlainText(
            QString("未找到推荐素材 %1，请确认 test_data 目录已随程序分发，或用『打开图片』手动选图")
                .arg(filename));
        return;
    }

    showImage(path);
}

void MainWindow::buildNavList()
{
    ui->navList->clear();

    auto addHeader = [this](const QString &text) {
        QListWidgetItem *item = new QListWidgetItem(text, ui->navList);
        item->setFlags(Qt::NoItemFlags);
    };

    auto addOp = [this](const QString &text, OperationType op) {
        QListWidgetItem *item = new QListWidgetItem(text, ui->navList);
        item->setData(kOpRole, static_cast<int>(op));
    };

    addHeader("基础点运算");
    addOp("原图", OperationType::Original);
    addOp("反转", OperationType::Invert);
    addOp("灰度化", OperationType::Gray);
    addOp("二值化", OperationType::Threshold);

    addHeader("滤波与降噪");
    addOp("高斯模糊", OperationType::GaussianBlur);
    addOp("中值滤波", OperationType::MedianBlur);
    addOp("双边滤波", OperationType::BilateralFilter);

    addHeader("形态学");
    addOp("腐蚀", OperationType::Erode);
    addOp("膨胀", OperationType::Dilate);
    addOp("开运算", OperationType::MorphOpen);
    addOp("闭运算", OperationType::MorphClose);
    addOp("形态学梯度", OperationType::MorphGradient);
    addOp("顶帽", OperationType::TopHat);
    addOp("黑帽", OperationType::BlackHat);

    addHeader("颜色空间");
    addOp("HSV伪彩", OperationType::HsvView);
    addOp("颜色分割", OperationType::ColorRange);

    addHeader("几何变换");
    addOp("旋转缩放", OperationType::RotateScale);

    addHeader("直方图");
    addOp("直方图可视化", OperationType::HistogramView);
    addOp("直方图均衡化", OperationType::EqualizeHist);
    addOp("CLAHE自适应均衡", OperationType::CLAHE);

    addHeader("边缘与梯度");
    addOp("Sobel梯度", OperationType::Sobel);
    addOp("Laplacian", OperationType::Laplacian);
    addOp("Canny边缘", OperationType::Canny);

    addHeader("霍夫变换");
    addOp("霍夫直线", OperationType::HoughLines);
    addOp("霍夫圆", OperationType::HoughCircles);

    addHeader("阈值进阶");
    addOp("自适应阈值", OperationType::AdaptiveThreshold);
    addOp("Otsu自动阈值", OperationType::OtsuThreshold);

    addHeader("特征与匹配");
    addOp("ORB关键点", OperationType::OrbKeypoints);
    addOp("模板匹配", OperationType::TemplateMatch);
    addOp("图像金字塔", OperationType::Pyramid);

    addHeader("轮廓");
    addOp("轮廓检测", OperationType::Contours);

    addHeader("视频分析");
    addOp("背景建模/运动检测", OperationType::BackgroundSubtraction);

    addHeader("目标检测");
    addOp("人脸检测", OperationType::FaceDetection);
}

void MainWindow::onNavRowChanged(int row)
{
    if (row < 0 || row >= ui->navList->count())
        return;

    QListWidgetItem *item = ui->navList->item(row);

    // 跳过分组标题行：往下找第一个可选条目并跳转过去
    if (!(item->flags() & Qt::ItemIsSelectable)) {
        for (int r = row + 1; r < ui->navList->count(); ++r) {
            if (ui->navList->item(r)->flags() & Qt::ItemIsSelectable) {
                ui->navList->setCurrentRow(r);
                return;
            }
        }
        return;
    }

    int opIndex = item->data(kOpRole).toInt();
    m_currentOp = static_cast<OperationType>(opIndex);
    m_currentOpName = item->text();
    ui->paramsStack->setCurrentIndex(opIndex);
    ui->infoText->appendPlainText("切换算子: " + item->text());

    if (m_currentOp == OperationType::BackgroundSubtraction)
        resetBackgroundSubtractor();

    applyCurrentOperation();
}

void MainWindow::applyCurrentOperation()
{
    if (m_originalMat.empty())
        return;

    QElapsedTimer timer;
    timer.start();

    cv::Mat result;
    cv::Mat gray;
    QString code;

    switch (m_currentOp) {
    case OperationType::Original:
        result = m_originalMat.clone();
        code = "// 原图，未做任何处理";
        break;

    case OperationType::Invert:
        cv::bitwise_not(m_originalMat, result);
        code = "cv::bitwise_not(src, dst);";
        break;

    case OperationType::Gray:
        cv::cvtColor(m_originalMat, result, cv::COLOR_BGR2GRAY);
        code = "cv::cvtColor(src, dst, cv::COLOR_BGR2GRAY);";
        break;

    case OperationType::Threshold:
        cv::cvtColor(m_originalMat, gray, cv::COLOR_BGR2GRAY);
        cv::threshold(gray, result, m_thresholdSlider->value(), 255, cv::THRESH_BINARY);
        code = QString("cv::threshold(gray, dst, %1, 255, cv::THRESH_BINARY);")
                   .arg(m_thresholdSlider->value());
        break;

    case OperationType::GaussianBlur: {
        int kernel = 2 * m_blurKernelSlider->value() + 1;
        double sigma = m_blurSigmaSlider->value();
        cv::GaussianBlur(m_originalMat, result, cv::Size(kernel, kernel), sigma);
        code = QString("cv::GaussianBlur(src, dst, cv::Size(%1,%1), %2);").arg(kernel).arg(sigma);
        break;
    }

    case OperationType::MedianBlur: {
        int kernel = 2 * m_medianKernelSlider->value() + 1;
        cv::medianBlur(m_originalMat, result, kernel);
        code = QString("cv::medianBlur(src, dst, %1);").arg(kernel);
        break;
    }

    case OperationType::BilateralFilter:
        cv::bilateralFilter(m_originalMat, result, m_bilateralDiameterSlider->value(),
                             m_bilateralSigmaColorSlider->value(),
                             m_bilateralSigmaSpaceSlider->value());
        code = QString("cv::bilateralFilter(src, dst, %1, %2, %3);")
                   .arg(m_bilateralDiameterSlider->value())
                   .arg(m_bilateralSigmaColorSlider->value())
                   .arg(m_bilateralSigmaSpaceSlider->value());
        break;

    case OperationType::Erode: {
        cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_RECT, cv::Size(m_erodeKernelSlider->value(), m_erodeKernelSlider->value()));
        cv::erode(m_originalMat, result, kernel, cv::Point(-1, -1), m_erodeIterSlider->value());
        code = QString("cv::erode(src, dst, kernel(%1x%1), Point(-1,-1), %2);")
                   .arg(m_erodeKernelSlider->value())
                   .arg(m_erodeIterSlider->value());
        break;
    }

    case OperationType::Dilate: {
        cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_RECT, cv::Size(m_dilateKernelSlider->value(), m_dilateKernelSlider->value()));
        cv::dilate(m_originalMat, result, kernel, cv::Point(-1, -1), m_dilateIterSlider->value());
        code = QString("cv::dilate(src, dst, kernel(%1x%1), Point(-1,-1), %2);")
                   .arg(m_dilateKernelSlider->value())
                   .arg(m_dilateIterSlider->value());
        break;
    }

    case OperationType::MorphOpen: {
        cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_RECT,
            cv::Size(m_morphOpenKernelSlider->value(), m_morphOpenKernelSlider->value()));
        cv::morphologyEx(m_originalMat, result, cv::MORPH_OPEN, kernel, cv::Point(-1, -1),
                          m_morphOpenIterSlider->value());
        code = QString("cv::morphologyEx(src, dst, cv::MORPH_OPEN, kernel(%1x%1), Point(-1,-1), %2);")
                   .arg(m_morphOpenKernelSlider->value())
                   .arg(m_morphOpenIterSlider->value());
        break;
    }

    case OperationType::MorphClose: {
        cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_RECT,
            cv::Size(m_morphCloseKernelSlider->value(), m_morphCloseKernelSlider->value()));
        cv::morphologyEx(m_originalMat, result, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1),
                          m_morphCloseIterSlider->value());
        code = QString("cv::morphologyEx(src, dst, cv::MORPH_CLOSE, kernel(%1x%1), Point(-1,-1), %2);")
                   .arg(m_morphCloseKernelSlider->value())
                   .arg(m_morphCloseIterSlider->value());
        break;
    }

    case OperationType::MorphGradient: {
        cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_RECT,
            cv::Size(m_morphGradientKernelSlider->value(), m_morphGradientKernelSlider->value()));
        cv::morphologyEx(m_originalMat, result, cv::MORPH_GRADIENT, kernel);
        code = QString("cv::morphologyEx(src, dst, cv::MORPH_GRADIENT, kernel(%1x%1));")
                   .arg(m_morphGradientKernelSlider->value());
        break;
    }

    case OperationType::TopHat: {
        cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_RECT, cv::Size(m_topHatKernelSlider->value(), m_topHatKernelSlider->value()));
        cv::morphologyEx(m_originalMat, result, cv::MORPH_TOPHAT, kernel);
        code = QString("cv::morphologyEx(src, dst, cv::MORPH_TOPHAT, kernel(%1x%1));")
                   .arg(m_topHatKernelSlider->value());
        break;
    }

    case OperationType::BlackHat: {
        cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_RECT,
            cv::Size(m_blackHatKernelSlider->value(), m_blackHatKernelSlider->value()));
        cv::morphologyEx(m_originalMat, result, cv::MORPH_BLACKHAT, kernel);
        code = QString("cv::morphologyEx(src, dst, cv::MORPH_BLACKHAT, kernel(%1x%1));")
                   .arg(m_blackHatKernelSlider->value());
        break;
    }

    case OperationType::HsvView:
        // 故意不转回 BGR：直接把 HSV 三个通道当成 RGB 显示，
        // 让用户直观看到这是完全不同的一套三通道坐标系。
        cv::cvtColor(m_originalMat, result, cv::COLOR_BGR2HSV);
        code = "cv::cvtColor(src, dst, cv::COLOR_BGR2HSV); // 故意不转回BGR，直接显示";
        break;

    case OperationType::ColorRange: {
        cv::Mat hsv;
        cv::cvtColor(m_originalMat, hsv, cv::COLOR_BGR2HSV);
        cv::Mat mask;
        cv::inRange(hsv,
                    cv::Scalar(m_colorRangeHLowSlider->value(), m_colorRangeSLowSlider->value(),
                               m_colorRangeVLowSlider->value()),
                    cv::Scalar(m_colorRangeHHighSlider->value(), m_colorRangeSHighSlider->value(),
                               m_colorRangeVHighSlider->value()),
                    mask);
        result = cv::Mat::zeros(m_originalMat.size(), m_originalMat.type());
        m_originalMat.copyTo(result, mask);
        code = QString("cv::inRange(hsv, Scalar(%1,%2,%3), Scalar(%4,%5,%6), mask);\n"
                        "src.copyTo(dst, mask);")
                   .arg(m_colorRangeHLowSlider->value())
                   .arg(m_colorRangeSLowSlider->value())
                   .arg(m_colorRangeVLowSlider->value())
                   .arg(m_colorRangeHHighSlider->value())
                   .arg(m_colorRangeSHighSlider->value())
                   .arg(m_colorRangeVHighSlider->value());
        break;
    }

    case OperationType::RotateScale: {
        cv::Point2f center(m_originalMat.cols / 2.0f, m_originalMat.rows / 2.0f);
        double angle = m_rotateAngleSlider->value();
        double scale = m_rotateScaleSlider->value() / 100.0;
        cv::Mat rotMat = cv::getRotationMatrix2D(center, angle, scale);
        cv::warpAffine(m_originalMat, result, rotMat, m_originalMat.size(), cv::INTER_LINEAR,
                        cv::BORDER_CONSTANT, cv::Scalar(240, 240, 240));
        code = QString("Mat rot = cv::getRotationMatrix2D(center, %1, %2);\n"
                        "cv::warpAffine(src, dst, rot, src.size());")
                   .arg(angle)
                   .arg(scale);
        break;
    }

    case OperationType::HistogramView: {
        // 分别统计 B/G/R 三个通道的直方图，归一化后画成三条折线，
        // 用来直观看清"直方图均衡化/CLAHE"到底在拉伸什么。
        std::vector<cv::Mat> channels;
        cv::split(m_originalMat, channels);

        const int histSize = 256;
        float range[] = {0, 256};
        const float *histRange = {range};
        const int canvasW = 512, canvasH = 300;
        result = cv::Mat(canvasH, canvasW, CV_8UC3, cv::Scalar(30, 30, 30));

        cv::Scalar colors[3] = {cv::Scalar(255, 90, 90), cv::Scalar(90, 220, 90),
                                 cv::Scalar(90, 90, 255)}; // 分别对应 B/G/R
        int binWidth = cvRound(static_cast<double>(canvasW) / histSize);
        int channelIdx[] = {0}; // channels[c] 本身已经是单通道 Mat，固定取第0通道
        for (int c = 0; c < 3; ++c) {
            cv::Mat hist;
            cv::calcHist(&channels[c], 1, channelIdx, cv::Mat(), hist, 1, &histSize, &histRange);
            cv::normalize(hist, hist, 0, canvasH, cv::NORM_MINMAX);
            for (int i = 1; i < histSize; ++i) {
                cv::line(result,
                         cv::Point(binWidth * (i - 1), canvasH - cvRound(hist.at<float>(i - 1))),
                         cv::Point(binWidth * i, canvasH - cvRound(hist.at<float>(i))), colors[c], 2);
            }
        }
        code = "cv::calcHist(&channel, 1, 0, Mat(), hist, 1, &histSize, &range);\n"
               "cv::normalize(hist, hist, 0, canvasH, cv::NORM_MINMAX); // 对 B/G/R 各做一次";
        break;
    }

    case OperationType::EqualizeHist:
        cv::cvtColor(m_originalMat, gray, cv::COLOR_BGR2GRAY);
        cv::equalizeHist(gray, result);
        code = "cv::equalizeHist(gray, dst);";
        break;

    case OperationType::CLAHE: {
        cv::cvtColor(m_originalMat, gray, cv::COLOR_BGR2GRAY);
        double clipLimit = m_claheClipSlider->value() / 10.0;
        int tile = m_claheTileSlider->value();
        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(clipLimit, cv::Size(tile, tile));
        clahe->apply(gray, result);
        code = QString("auto clahe = cv::createCLAHE(%1, cv::Size(%2,%2));\nclahe->apply(gray, dst);")
                   .arg(clipLimit)
                   .arg(tile);
        break;
    }

    case OperationType::Sobel: {
        cv::cvtColor(m_originalMat, gray, cv::COLOR_BGR2GRAY);
        int ksize = 2 * m_sobelKernelSlider->value() + 1;
        cv::Mat gradX, gradY, absX, absY;
        cv::Sobel(gray, gradX, CV_16S, 1, 0, ksize);
        cv::Sobel(gray, gradY, CV_16S, 0, 1, ksize);
        cv::convertScaleAbs(gradX, absX);
        cv::convertScaleAbs(gradY, absY);
        cv::addWeighted(absX, 0.5, absY, 0.5, 0, result);
        code = QString("cv::Sobel(gray, gradX, CV_16S, 1, 0, %1);\n"
                        "cv::Sobel(gray, gradY, CV_16S, 0, 1, %1);\n"
                        "cv::addWeighted(absX, 0.5, absY, 0.5, 0, dst);")
                   .arg(ksize);
        break;
    }

    case OperationType::Laplacian: {
        cv::cvtColor(m_originalMat, gray, cv::COLOR_BGR2GRAY);
        int ksize = 2 * m_laplacianKernelSlider->value() + 1;
        cv::Mat lap;
        cv::Laplacian(gray, lap, CV_16S, ksize);
        cv::convertScaleAbs(lap, result);
        code = QString("cv::Laplacian(gray, lap, CV_16S, %1);\ncv::convertScaleAbs(lap, dst);")
                   .arg(ksize);
        break;
    }

    case OperationType::Canny:
        cv::cvtColor(m_originalMat, gray, cv::COLOR_BGR2GRAY);
        cv::Canny(gray, result, m_cannyLowSlider->value(), m_cannyHighSlider->value());
        code = QString("cv::Canny(gray, dst, %1, %2);")
                   .arg(m_cannyLowSlider->value())
                   .arg(m_cannyHighSlider->value());
        break;

    case OperationType::HoughLines: {
        cv::cvtColor(m_originalMat, gray, cv::COLOR_BGR2GRAY);
        cv::Mat edges;
        cv::Canny(gray, edges, 50, 150);

        std::vector<cv::Vec4i> lines;
        cv::HoughLinesP(edges, lines, 1, CV_PI / 180, m_houghLinesThresholdSlider->value(),
                         m_houghLinesMinLengthSlider->value(), m_houghLinesMaxGapSlider->value());

        result = m_originalMat.clone();
        for (const cv::Vec4i &l : lines) {
            cv::line(result, cv::Point(l[0], l[1]), cv::Point(l[2], l[3]), cv::Scalar(0, 220, 0), 2);
        }
        code = QString("cv::HoughLinesP(edges, lines, 1, CV_PI/180, %1, %2, %3);")
                   .arg(m_houghLinesThresholdSlider->value())
                   .arg(m_houghLinesMinLengthSlider->value())
                   .arg(m_houghLinesMaxGapSlider->value());
        break;
    }

    case OperationType::HoughCircles: {
        cv::cvtColor(m_originalMat, gray, cv::COLOR_BGR2GRAY);
        cv::GaussianBlur(gray, gray, cv::Size(9, 9), 2);

        std::vector<cv::Vec3f> circles;
        int maxRadius = m_houghCirclesMaxRadiusSlider->value();
        cv::HoughCircles(gray, circles, cv::HOUGH_GRADIENT, 1, m_houghCirclesMinDistSlider->value(),
                          m_houghCirclesParam1Slider->value(), m_houghCirclesParam2Slider->value(),
                          m_houghCirclesMinRadiusSlider->value(), maxRadius == 0 ? 0 : maxRadius);

        result = m_originalMat.clone();
        for (const cv::Vec3f &c : circles) {
            cv::Point center(cvRound(c[0]), cvRound(c[1]));
            int radius = cvRound(c[2]);
            cv::circle(result, center, radius, cv::Scalar(0, 140, 255), 2);
            cv::circle(result, center, 2, cv::Scalar(0, 0, 220), 3);
        }
        code = QString("cv::HoughCircles(gray, circles, HOUGH_GRADIENT, 1, %1, %2, %3, %4, %5);")
                   .arg(m_houghCirclesMinDistSlider->value())
                   .arg(m_houghCirclesParam1Slider->value())
                   .arg(m_houghCirclesParam2Slider->value())
                   .arg(m_houghCirclesMinRadiusSlider->value())
                   .arg(maxRadius);
        break;
    }

    case OperationType::AdaptiveThreshold: {
        cv::cvtColor(m_originalMat, gray, cv::COLOR_BGR2GRAY);
        int blockSize = 2 * m_adaptiveBlockSlider->value() + 3; // 至少为3，且为奇数
        cv::adaptiveThreshold(gray, result, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                               cv::THRESH_BINARY, blockSize, m_adaptiveCSlider->value());
        code = QString("cv::adaptiveThreshold(gray, dst, 255, ADAPTIVE_THRESH_GAUSSIAN_C,\n"
                        "                      THRESH_BINARY, %1, %2);")
                   .arg(blockSize)
                   .arg(m_adaptiveCSlider->value());
        break;
    }

    case OperationType::OtsuThreshold:
        cv::cvtColor(m_originalMat, gray, cv::COLOR_BGR2GRAY);
        cv::threshold(gray, result, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
        code = "cv::threshold(gray, dst, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);";
        break;

    case OperationType::OrbKeypoints: {
        cv::Ptr<cv::ORB> orb = cv::ORB::create(m_orbFeaturesSlider->value());
        std::vector<cv::KeyPoint> keypoints;
        orb->detect(m_originalMat, keypoints);
        cv::drawKeypoints(m_originalMat, keypoints, result, cv::Scalar(0, 220, 0),
                           cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);
        code = QString("auto orb = cv::ORB::create(%1);\norb->detect(src, keypoints);")
                   .arg(m_orbFeaturesSlider->value());
        break;
    }

    case OperationType::TemplateMatch: {
        cv::Mat tmpl;
        bool autoCrop = m_templateMat.empty();
        if (!autoCrop) {
            tmpl = m_templateMat;
        } else {
            // 没手动选模板时，自动取原图中心一块当模板，保证一打开就有效果
            double percent = m_templateCropPercentSlider->value() / 100.0;
            int tw = std::max(1, static_cast<int>(m_originalMat.cols * percent));
            int th = std::max(1, static_cast<int>(m_originalMat.rows * percent));
            cv::Rect roi((m_originalMat.cols - tw) / 2, (m_originalMat.rows - th) / 2, tw, th);
            tmpl = m_originalMat(roi).clone();
        }

        result = m_originalMat.clone();

        if (!tmpl.empty() && tmpl.cols <= m_originalMat.cols && tmpl.rows <= m_originalMat.rows) {
            cv::Mat scoreMap;
            cv::matchTemplate(m_originalMat, tmpl, scoreMap, cv::TM_CCOEFF_NORMED);
            double minVal, maxVal;
            cv::Point minLoc, maxLoc;
            cv::minMaxLoc(scoreMap, &minVal, &maxVal, &minLoc, &maxLoc);
            cv::rectangle(result, cv::Rect(maxLoc, tmpl.size()), cv::Scalar(0, 140, 255), 2);
        }
        code = QString("cv::matchTemplate(src, tmpl, score, cv::TM_CCOEFF_NORMED);\n"
                        "cv::minMaxLoc(score, ..., &maxLoc); // tmpl%1")
                   .arg(autoCrop ? "=原图中心裁剪" : "=手动选择的图片");
        break;
    }

    case OperationType::Pyramid: {
        int levels = m_pyramidLevelSlider->value();
        cv::Mat pyr = m_originalMat.clone();
        for (int i = 0; i < levels; ++i)
            cv::pyrDown(pyr, pyr);
        for (int i = 0; i < levels; ++i)
            cv::pyrUp(pyr, pyr);
        // 尺寸可能因奇偶取整产生1px误差，缩放回原图大小方便和原图对比
        if (pyr.size() != m_originalMat.size())
            cv::resize(pyr, pyr, m_originalMat.size());
        result = pyr;
        code = QString("for (i < %1) cv::pyrDown(img, img);\nfor (i < %1) cv::pyrUp(img, img);")
                   .arg(levels);
        break;
    }

    case OperationType::Contours: {
        cv::cvtColor(m_originalMat, gray, cv::COLOR_BGR2GRAY);
        cv::Mat binary;
        cv::threshold(gray, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        result = m_originalMat.clone();
        double minArea = m_contourMinAreaSlider->value();
        for (size_t i = 0; i < contours.size(); ++i) {
            if (cv::contourArea(contours[i]) < minArea)
                continue;
            cv::drawContours(result, contours, static_cast<int>(i), cv::Scalar(0, 220, 0), 2);
            cv::Rect box = cv::boundingRect(contours[i]);
            cv::rectangle(result, box, cv::Scalar(0, 140, 255), 1);
        }
        code = QString("cv::findContours(binary, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);\n"
                        "// 面积 < %1 的轮廓被过滤掉")
                   .arg(minArea);
        break;
    }

    case OperationType::BackgroundSubtraction: {
        if (!m_bgSubtractor)
            resetBackgroundSubtractor();
        m_bgSubtractor->setVarThreshold(m_bgVarThresholdSlider->value());
        double learningRate = m_bgLearningRateSlider->value() / 1000.0;
        m_bgSubtractor->apply(m_originalMat, result, learningRate);
        code = QString("static auto mog2 = cv::createBackgroundSubtractorMOG2();\n"
                        "mog2->setVarThreshold(%1);\n"
                        "mog2->apply(frame, fgMask, %2); // 白=前景(运动) 灰=阴影 黑=背景")
                   .arg(m_bgVarThresholdSlider->value())
                   .arg(learningRate, 0, 'f', 3);
        break;
    }

    case OperationType::FaceDetection: {
        result = m_originalMat.clone();
        if (!ensureFaceCascadesLoaded()) {
            code = "// 级联文件加载失败，看信息日志";
            break;
        }

        cv::cvtColor(m_originalMat, gray, cv::COLOR_BGR2GRAY);
        cv::equalizeHist(gray, gray);

        double scaleFactor = m_faceScaleSlider->value() / 100.0;
        int minNeighbors = m_faceMinNeighborsSlider->value();

        std::vector<cv::Rect> faces;
        m_faceCascade.detectMultiScale(gray, faces, scaleFactor, minNeighbors, 0, cv::Size(30, 30));

        for (const cv::Rect &face : faces) {
            cv::rectangle(result, face, cv::Scalar(0, 220, 0), 2);

            cv::Mat faceRoiGray = gray(face);
            std::vector<cv::Rect> eyes;
            m_eyeCascade.detectMultiScale(faceRoiGray, eyes, 1.1, 6, 0, cv::Size(15, 15));
            for (const cv::Rect &eye : eyes) {
                cv::Rect eyeInFull(face.x + eye.x, face.y + eye.y, eye.width, eye.height);
                cv::rectangle(result, eyeInFull, cv::Scalar(0, 220, 220), 2);
            }
        }

        code = QString("cv::CascadeClassifier face(\"haarcascade_frontalface_default.xml\");\n"
                        "face.detectMultiScale(gray, faces, %1, %2);\n"
                        "// 每个 face 区域内再跑一次 eye.detectMultiScale(gray(face), eyes, ...)")
                   .arg(scaleFactor, 0, 'f', 2)
                   .arg(minNeighbors);
        break;
    }

    case OperationType::Count:
        return;
    }

    m_lastResultMat = result;
    m_processedDisplayImage = OpenCVHelper::matToQImage(result);
    updateImageDisplay();

    double elapsedMs = timer.nsecsElapsed() / 1e6;
    ui->processedGroupBox->setTitle(
        QString("处理结果 - %1 (%2 ms)").arg(m_currentOpName).arg(elapsedMs, 0, 'f', 2));
    if (m_codeSnippetText)
        m_codeSnippetText->setPlainText(code);
}

QSlider *MainWindow::addSliderRow(QWidget *page, QVBoxLayout *layout, const QString &labelText,
                                   int minValue, int maxValue, int defaultValue,
                                   QLabel **outValueLabel)
{
    QHBoxLayout *row = new QHBoxLayout();

    QLabel *nameLabel = new QLabel(labelText, page);

    QLabel *valueLabel = new QLabel(QString::number(defaultValue), page);
    valueLabel->setProperty("role", "value");
    valueLabel->setMinimumWidth(36);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    QSlider *slider = new QSlider(Qt::Horizontal, page);
    slider->setRange(minValue, maxValue);
    slider->setValue(defaultValue);

    row->addWidget(nameLabel);
    row->addWidget(slider, 1);
    row->addWidget(valueLabel);
    layout->addLayout(row);

    connect(slider, &QSlider::valueChanged, this, [this, valueLabel](int v) {
        valueLabel->setText(QString::number(v));
        applyCurrentOperation();
    });

    if (outValueLabel)
        *outValueLabel = valueLabel;

    return slider;
}

void MainWindow::buildParamPanels()
{
    // principle：原理说明（复用之前的文案）；scenario：现实应用场景，一句话；
    // recommendedImage：test_data 里最适合演示这个算子的素材文件名，页面顶部会生成一个可点击的加载按钮。
    auto makePage = [this](const QString &principle, const QString &scenario,
                            const QString &recommendedImage) -> QWidget * {
        QWidget *page = new QWidget(ui->paramsStack);
        QVBoxLayout *layout = new QVBoxLayout(page);

        if (!recommendedImage.isEmpty()) {
            QPushButton *recommendButton = new QPushButton(
                QString("加载推荐图片: %1").arg(recommendedImage), page);
            recommendButton->setProperty("variant", "secondary");
            connect(recommendButton, &QPushButton::clicked, this,
                    [this, recommendedImage] { loadRecommendedImage(recommendedImage); });
            layout->addWidget(recommendButton);
        }

        QLabel *principleLabel = new QLabel(principle, page);
        principleLabel->setProperty("role", "hint");
        principleLabel->setWordWrap(true);
        layout->addWidget(principleLabel);

        QLabel *scenarioLabel = new QLabel("应用场景：" + scenario, page);
        scenarioLabel->setProperty("role", "scenario");
        scenarioLabel->setWordWrap(true);
        layout->addWidget(scenarioLabel);

        return page;
    };

    // Original 原图
    {
        QWidget *page = makePage("显示未经任何处理的原始图像，用作对比基准。",
                                  "任何图像处理流程的起点，用来和后续每一步效果做对比。",
                                  "pexels-724211268-34685018.jpg");
        static_cast<QVBoxLayout *>(page->layout())->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // Invert 反转
    {
        QWidget *page = makePage(
            "cv::bitwise_not：对每个像素按位取反，即 255 - pixel，得到底片效果，无可调参数。",
            "医学影像底片阅读、扫描文档负片校正、复古/艺术风格滤镜。",
            "pexels-724211268-34685018.jpg");
        static_cast<QVBoxLayout *>(page->layout())->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // Gray 灰度化
    {
        QWidget *page = makePage(
            "cv::cvtColor(COLOR_BGR2GRAY)：按加权公式把 BGR 三通道合成单通道灰度值，无可调参数。",
            "几乎所有图像算法的第一步预处理，减少数据量、加快后续计算（阈值/边缘/特征检测都基于灰度图）。",
            "pexels-724211268-34685018.jpg");
        static_cast<QVBoxLayout *>(page->layout())->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // Threshold 二值化
    {
        QWidget *page = makePage(
            "cv::threshold：先转灰度，再按阈值把像素分成纯黑/纯白两类，用于分割前景背景。",
            "文档扫描二值化、验证码字符分割、工业零件轮廓提取前的预处理。",
            "text_binary.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *valueLabel = nullptr;
        m_thresholdSlider = addSliderRow(page, layout, "阈值", 0, 255, 128, &valueLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // GaussianBlur 高斯模糊
    {
        QWidget *page = makePage(
            "cv::GaussianBlur：用高斯核对邻域像素加权平均，核越大/Sigma越大越模糊，常用于降噪、预处理。",
            "拍照虚化背景、边缘检测/特征提取前的降噪预处理、简单的隐私打码。",
            "noisy_saltpepper_color.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *kernelValueLabel = nullptr;
        // 滑块范围 0-15，映射为奇数核大小 1-31
        m_blurKernelSlider = addSliderRow(page, layout, "核大小", 0, 15, 2, &kernelValueLabel);
        connect(m_blurKernelSlider, &QSlider::valueChanged, this, [kernelValueLabel](int v) {
            kernelValueLabel->setText(QString::number(2 * v + 1));
        });
        kernelValueLabel->setText(QString::number(2 * m_blurKernelSlider->value() + 1));

        QLabel *sigmaValueLabel = nullptr;
        m_blurSigmaSlider = addSliderRow(page, layout, "Sigma", 0, 20, 0, &sigmaValueLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // MedianBlur 中值滤波
    {
        QWidget *page = makePage(
            "cv::medianBlur：取邻域像素的中位数作为输出，对椒盐噪声（黑白噪点）特别有效，且能保留边缘。",
            "老照片/扫描件的噪点修复、传感器椒盐噪声清理，比高斯模糊更能保住边缘细节。",
            "noisy_saltpepper_color.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *kernelValueLabel = nullptr;
        // 滑块范围 0-15，映射为奇数核大小 1-31
        m_medianKernelSlider = addSliderRow(page, layout, "核大小", 0, 15, 2, &kernelValueLabel);
        connect(m_medianKernelSlider, &QSlider::valueChanged, this, [kernelValueLabel](int v) {
            kernelValueLabel->setText(QString::number(2 * v + 1));
        });
        kernelValueLabel->setText(QString::number(2 * m_medianKernelSlider->value() + 1));
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // BilateralFilter 双边滤波
    {
        QWidget *page = makePage(
            "cv::bilateralFilter：同时考虑空间距离和像素值差异做加权平均，能在去噪的同时较好地保留边缘，速度比高斯慢。",
            "人像磨皮美颜（保留五官边缘不糊）、卫星/遥感图像降噪。",
            "noisy_saltpepper_color.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *diameterValueLabel = nullptr;
        QLabel *sigmaColorValueLabel = nullptr;
        QLabel *sigmaSpaceValueLabel = nullptr;
        m_bilateralDiameterSlider = addSliderRow(page, layout, "邻域直径", 1, 15, 5, &diameterValueLabel);
        m_bilateralSigmaColorSlider = addSliderRow(page, layout, "颜色Sigma", 10, 200, 75, &sigmaColorValueLabel);
        m_bilateralSigmaSpaceSlider = addSliderRow(page, layout, "空间Sigma", 10, 200, 75, &sigmaSpaceValueLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // Erode 腐蚀
    {
        QWidget *page = makePage(
            "cv::erode：用结构元素在图像上滑动，取邻域最小值，使前景区域收缩，可去除小噪点。",
            "OCR 前清理文字笔画上的毛刺噪点、指纹/条码图像的细化预处理。",
            "text_binary.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *kernelValueLabel = nullptr;
        QLabel *iterValueLabel = nullptr;
        m_erodeKernelSlider = addSliderRow(page, layout, "核大小", 1, 15, 3, &kernelValueLabel);
        m_erodeIterSlider = addSliderRow(page, layout, "迭代次数", 1, 10, 1, &iterValueLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // Dilate 膨胀
    {
        QWidget *page = makePage(
            "cv::dilate：与腐蚀相反，取邻域最大值，使前景区域扩张，可填补小孔洞、连接断裂区域。",
            "连接断裂的文字笔画/条形码竖线、让二值化后的目标区域更完整方便后续轮廓提取。",
            "text_binary.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *kernelValueLabel = nullptr;
        QLabel *iterValueLabel = nullptr;
        m_dilateKernelSlider = addSliderRow(page, layout, "核大小", 1, 15, 3, &kernelValueLabel);
        m_dilateIterSlider = addSliderRow(page, layout, "迭代次数", 1, 10, 1, &iterValueLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // MorphOpen 开运算
    {
        QWidget *page = makePage(
            "cv::morphologyEx(MORPH_OPEN)：先腐蚀后膨胀，能去掉前景上的小噪点，同时比单独腐蚀更好地保持整体形状。",
            "工业质检里去掉图像上的细小毛刺/噪点，同时不破坏被检测零件的整体轮廓。",
            "text_binary.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *kernelValueLabel = nullptr;
        QLabel *iterValueLabel = nullptr;
        m_morphOpenKernelSlider = addSliderRow(page, layout, "核大小", 1, 15, 3, &kernelValueLabel);
        m_morphOpenIterSlider = addSliderRow(page, layout, "迭代次数", 1, 10, 1, &iterValueLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // MorphClose 闭运算
    {
        QWidget *page = makePage(
            "cv::morphologyEx(MORPH_CLOSE)：先膨胀后腐蚀，能填补前景内部的小孔洞、连接断裂的相邻区域。",
            "补全扫描文档里断掉的笔画/表格线、修复分割结果里的小空洞。",
            "text_binary.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *kernelValueLabel = nullptr;
        QLabel *iterValueLabel = nullptr;
        m_morphCloseKernelSlider = addSliderRow(page, layout, "核大小", 1, 15, 3, &kernelValueLabel);
        m_morphCloseIterSlider = addSliderRow(page, layout, "迭代次数", 1, 10, 1, &iterValueLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // MorphGradient 形态学梯度
    {
        QWidget *page = makePage(
            "cv::morphologyEx(MORPH_GRADIENT)：膨胀结果减去腐蚀结果，恰好勾勒出前景物体的边界轮廓线。",
            "快速提取二值图像的物体轮廓线，常作为轮廓检测前的一步快速预览。",
            "text_binary.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *kernelValueLabel = nullptr;
        m_morphGradientKernelSlider = addSliderRow(page, layout, "核大小", 1, 15, 3, &kernelValueLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // TopHat 顶帽
    {
        QWidget *page = makePage(
            "cv::morphologyEx(MORPH_TOPHAT)：原图减去开运算结果，突出比周围背景更亮、比结构元素更小的细节。",
            "不均匀光照下提取小的高亮瑕疵，比如印刷电路板上的亮点缺陷检测。",
            "text_binary.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *kernelValueLabel = nullptr;
        m_topHatKernelSlider = addSliderRow(page, layout, "核大小", 1, 15, 9, &kernelValueLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // BlackHat 黑帽
    {
        QWidget *page = makePage(
            "cv::morphologyEx(MORPH_BLACKHAT)：闭运算结果减去原图，突出比周围背景更暗、比结构元素更小的细节。",
            "提取比背景暗的小瑕疵，比如布料/纸张表面的小污渍、裂纹检测。",
            "text_binary.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *kernelValueLabel = nullptr;
        m_blackHatKernelSlider = addSliderRow(page, layout, "核大小", 1, 15, 9, &kernelValueLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // HsvView HSV伪彩
    {
        QWidget *page = makePage(
            "cv::cvtColor(BGR2HSV)：色相(H)/饱和度(S)/明度(V) 是另一套三通道坐标系。这里故意不转回BGR，"
            "直接把 H/S/V 当成 R/G/B 显示成伪彩图，帮助建立“HSV 不是普通颜色”的直觉，无可调参数。",
            "几乎所有基于颜色做识别/追踪的项目（分拣、追踪、抠图）都先转到 HSV 空间再处理，比直接用 BGR 更稳定。",
            "shapes_color.png");
        static_cast<QVBoxLayout *>(page->layout())->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // ColorRange 颜色分割
    {
        QWidget *page = makePage(
            "cv::inRange：在 HSV 空间按 H/S/V 范围筛选像素做掩码，再和原图做按位与，只保留命中颜色范围的区域，"
            "其余变黑。默认范围圈的是绿色，加载推荐图片后能直接看到绿色多边形被抠出来。",
            "颜色识别与追踪（红绿灯识别、流水线色块分拣、绿幕抠图、循迹机器人找色块路径）。",
            "shapes_color.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *hLowLabel = nullptr;
        QLabel *hHighLabel = nullptr;
        QLabel *sLowLabel = nullptr;
        QLabel *sHighLabel = nullptr;
        QLabel *vLowLabel = nullptr;
        QLabel *vHighLabel = nullptr;
        m_colorRangeHLowSlider = addSliderRow(page, layout, "H低", 0, 179, 35, &hLowLabel);
        m_colorRangeHHighSlider = addSliderRow(page, layout, "H高", 0, 179, 85, &hHighLabel);
        m_colorRangeSLowSlider = addSliderRow(page, layout, "S低", 0, 255, 60, &sLowLabel);
        m_colorRangeSHighSlider = addSliderRow(page, layout, "S高", 0, 255, 255, &sHighLabel);
        m_colorRangeVLowSlider = addSliderRow(page, layout, "V低", 0, 255, 60, &vLowLabel);
        m_colorRangeVHighSlider = addSliderRow(page, layout, "V高", 0, 255, 255, &vHighLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // RotateScale 旋转缩放
    {
        QWidget *page = makePage(
            "cv::getRotationMatrix2D + cv::warpAffine：绕图像中心生成旋转+缩放的仿射矩阵，再做仿射变换，是几何变换的基础套路。",
            "深度学习训练集数据增强（旋转扩充样本）、扫描文档纠偏、图像拼贴排版。",
            "shapes_color.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *angleValueLabel = nullptr;
        QLabel *scaleValueLabel = nullptr;
        m_rotateAngleSlider = addSliderRow(page, layout, "角度", -180, 180, 0, &angleValueLabel);
        m_rotateScaleSlider = addSliderRow(page, layout, "缩放%", 50, 200, 100, &scaleValueLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // HistogramView 直方图可视化
    {
        QWidget *page = makePage(
            "cv::calcHist：分别统计 B/G/R 三个通道里每个亮度值(0-255)出现的像素数量，画成三条折线——"
            "横轴是亮度、纵轴是像素数量。这是理解下面均衡化/CLAHE两个算子的基础，无可调参数。",
            "调色、曝光分析类软件（比如 Photoshop/Lightroom 的直方图面板）的底层原理，"
            "拍照时判断画面是否过曝/欠曝也是看直方图。",
            "low_contrast_scene.png");
        static_cast<QVBoxLayout *>(page->layout())->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // EqualizeHist 直方图均衡化
    {
        QWidget *page = makePage(
            "cv::equalizeHist：把灰度直方图拉伸到均匀分布，增强全局对比度，适合整体偏暗/偏亮的图片，无可调参数。",
            "逆光/雾天照片增强、老旧监控画面提升可视性、医学X光片对比度增强。",
            "low_contrast_scene.png");
        static_cast<QVBoxLayout *>(page->layout())->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // CLAHE 自适应均衡
    {
        QWidget *page = makePage(
            "cv::CLAHE：分块做直方图均衡并限制对比度放大倍数，避免普通均衡化放大噪声，比 equalizeHist 更精细。",
            "医学影像（CT/眼底照片）增强、夜视监控画面增强，比全局均衡化更不容易引入噪声。",
            "low_contrast_scene.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *clipValueLabel = nullptr;
        QLabel *tileValueLabel = nullptr;
        // 滑块 10-400 表示 clipLimit 1.0-40.0
        m_claheClipSlider = addSliderRow(page, layout, "对比度限制", 10, 400, 20, &clipValueLabel);
        connect(m_claheClipSlider, &QSlider::valueChanged, this, [clipValueLabel](int v) {
            clipValueLabel->setText(QString::number(v / 10.0, 'f', 1));
        });
        clipValueLabel->setText(QString::number(m_claheClipSlider->value() / 10.0, 'f', 1));

        m_claheTileSlider = addSliderRow(page, layout, "分块大小", 2, 16, 8, &tileValueLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // Sobel 梯度
    {
        QWidget *page = makePage(
            "cv::Sobel：分别求 x、y 方向的一阶梯度并合成，凸显亮度变化剧烈的区域，是很多边缘算法的基础。",
            "工业零件边缘检测、图像锐化前的梯度分析、车道线检测的前置步骤。",
            "edge_pattern.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *kernelValueLabel = nullptr;
        // 滑块 0-3，映射为核大小 1/3/5/7
        m_sobelKernelSlider = addSliderRow(page, layout, "核大小", 0, 3, 1, &kernelValueLabel);
        connect(m_sobelKernelSlider, &QSlider::valueChanged, this, [kernelValueLabel](int v) {
            kernelValueLabel->setText(QString::number(2 * v + 1));
        });
        kernelValueLabel->setText(QString::number(2 * m_sobelKernelSlider->value() + 1));
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // Laplacian
    {
        QWidget *page = makePage(
            "cv::Laplacian：二阶导数算子，对孤立点和细线更敏感，常用于图像锐化和边缘检测。",
            "图像锐化（拉普拉斯锐化）、显微图像里的细小结构/裂纹检测。",
            "edge_pattern.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *kernelValueLabel = nullptr;
        m_laplacianKernelSlider = addSliderRow(page, layout, "核大小", 0, 3, 1, &kernelValueLabel);
        connect(m_laplacianKernelSlider, &QSlider::valueChanged, this, [kernelValueLabel](int v) {
            kernelValueLabel->setText(QString::number(2 * v + 1));
        });
        kernelValueLabel->setText(QString::number(2 * m_laplacianKernelSlider->value() + 1));
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // Canny 边缘
    {
        QWidget *page = makePage(
            "cv::Canny：基于梯度的边缘检测，双阈值做滞后阈值处理，高于高阈值判定为边缘，低于低阈值舍弃。",
            "自动驾驶车道线/障碍物边缘提取、OCR前的文字边界定位、工业零件尺寸检测。",
            "edge_pattern.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *lowValueLabel = nullptr;
        QLabel *highValueLabel = nullptr;
        m_cannyLowSlider = addSliderRow(page, layout, "低阈值", 0, 255, 50, &lowValueLabel);
        m_cannyHighSlider = addSliderRow(page, layout, "高阈值", 0, 255, 150, &highValueLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // HoughLines 霍夫直线
    {
        QWidget *page = makePage(
            "cv::HoughLinesP：先 Canny 提取边缘，再用概率霍夫变换在边缘点里投票找直线段，绿色线即检测结果。",
            "车道线检测、文档/表格扫描件里提取表格线、建筑图纸线条提取。",
            "checkerboard.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *thresholdLabel = nullptr;
        QLabel *minLengthLabel = nullptr;
        QLabel *maxGapLabel = nullptr;
        m_houghLinesThresholdSlider = addSliderRow(page, layout, "累加阈值", 10, 200, 80, &thresholdLabel);
        m_houghLinesMinLengthSlider = addSliderRow(page, layout, "最小线长", 0, 200, 50, &minLengthLabel);
        m_houghLinesMaxGapSlider = addSliderRow(page, layout, "最大间隙", 0, 50, 10, &maxGapLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // HoughCircles 霍夫圆
    {
        QWidget *page = makePage(
            "cv::HoughCircles：基于梯度信息的霍夫圆变换，在灰度图上直接检测圆形，橙色圆周+红色圆心为检测结果。",
            "硬币/瞳孔/圆形零件的计数与定位、井盖/圆形交通标志检测。",
            "shapes_color.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *minDistLabel = nullptr;
        QLabel *param1Label = nullptr;
        QLabel *param2Label = nullptr;
        QLabel *minRadiusLabel = nullptr;
        QLabel *maxRadiusLabel = nullptr;
        m_houghCirclesMinDistSlider = addSliderRow(page, layout, "最小圆心距", 10, 200, 50, &minDistLabel);
        m_houghCirclesParam1Slider = addSliderRow(page, layout, "Canny高阈值", 50, 200, 100, &param1Label);
        m_houghCirclesParam2Slider = addSliderRow(page, layout, "累加器阈值", 10, 100, 30, &param2Label);
        m_houghCirclesMinRadiusSlider = addSliderRow(page, layout, "最小半径", 0, 100, 0, &minRadiusLabel);
        m_houghCirclesMaxRadiusSlider = addSliderRow(page, layout, "最大半径(0=不限)", 0, 300, 0, &maxRadiusLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // AdaptiveThreshold 自适应阈值
    {
        QWidget *page = makePage(
            "cv::adaptiveThreshold：每个像素的阈值由其邻域计算得出，而不是整张图用同一个阈值，适合光照不均匀的图片。",
            "手机拍文档时光照不均的场景（比如一角有阴影），比全局阈值稳定得多，是移动扫描类App的核心算法之一。",
            "low_contrast_scene.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *blockValueLabel = nullptr;
        QLabel *cValueLabel = nullptr;
        // 滑块 0-24，映射为邻域块大小 3-51（奇数）
        m_adaptiveBlockSlider = addSliderRow(page, layout, "邻域块大小", 0, 24, 4, &blockValueLabel);
        connect(m_adaptiveBlockSlider, &QSlider::valueChanged, this, [blockValueLabel](int v) {
            blockValueLabel->setText(QString::number(2 * v + 3));
        });
        blockValueLabel->setText(QString::number(2 * m_adaptiveBlockSlider->value() + 3));

        m_adaptiveCSlider = addSliderRow(page, layout, "常数C", -20, 20, 2, &cValueLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // OtsuThreshold Otsu自动阈值
    {
        QWidget *page = makePage(
            "cv::threshold + THRESH_OTSU：根据灰度直方图自动计算全局最优分割阈值，无需手动指定阈值，适合双峰分布明显的图片。",
            "医学细胞/血液图像的前景背景快速分割、批量处理图片时省去人工调阈值的成本。",
            "text_binary.png");
        static_cast<QVBoxLayout *>(page->layout())->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // OrbKeypoints ORB关键点
    {
        QWidget *page = makePage(
            "cv::ORB：结合 FAST 角点检测 + BRIEF 描述子的快速特征点算法，圆圈大小反映特征尺度、"
            "线段方向反映主方向，常用于图像匹配、拼接、SLAM。",
            "全景图拼接、以图搜图、AR标记识别、机器人/无人机视觉SLAM定位，都以关键点+描述子为基础。",
            "pexels-724211268-34685018.jpg");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *featuresLabel = nullptr;
        m_orbFeaturesSlider = addSliderRow(page, layout, "特征点数量", 50, 2000, 500, &featuresLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // TemplateMatch 模板匹配
    {
        QWidget *page = makePage(
            "cv::matchTemplate：在原图上滑动模板计算相似度，取最高分位置画框。未手动选模板时，自动取原图"
            "中心一块裁剪当模板，方便直接看到效果；点下面按钮可以换成任意一张图片作为模板。",
            "工业产线上定位固定形状的零件位置、UI自动化测试里的\"找图点击\"、简单场景下的单目标跟踪。",
            "shapes_color.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *cropLabel = nullptr;
        m_templateCropPercentSlider = addSliderRow(page, layout, "自动裁剪比例%", 10, 50, 25, &cropLabel);

        m_selectTemplateButton = new QPushButton("选择模板图片...", page);
        connect(m_selectTemplateButton, &QPushButton::clicked, this, &MainWindow::selectTemplateImage);
        layout->addWidget(m_selectTemplateButton);

        m_templateStatusLabel = new QLabel("当前：自动取原图中心裁剪", page);
        m_templateStatusLabel->setProperty("role", "hint");
        m_templateStatusLabel->setWordWrap(true);
        layout->addWidget(m_templateStatusLabel);

        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // Pyramid 图像金字塔
    {
        QWidget *page = makePage(
            "cv::pyrDown / cv::pyrUp：反复降采样再升采样回原尺寸，层数越多损失的细节越多、图像越模糊，"
            "这是构建多分辨率图像金字塔（用于图像缩放、特征匹配、图像融合等）的基本操作。",
            "多尺度目标检测（先在小图上粗筛再回原图精定位）、图像压缩传输、拉普拉斯金字塔图像融合。",
            "pexels-724211268-34685018.jpg");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *levelLabel = nullptr;
        m_pyramidLevelSlider = addSliderRow(page, layout, "层数", 0, 4, 2, &levelLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // Contours 轮廓检测
    {
        QWidget *page = makePage(
            "cv::findContours：先用 Otsu 自动二值化，再提取外部轮廓；绿色线为轮廓，橙色框为外接矩形，可用最小面积过滤掉噪点小轮廓。",
            "物体计数（比如统计零件/细胞数量）、形状识别、OCR前的字符分割、产线缺陷检测。",
            "shapes_color.png");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *minAreaValueLabel = nullptr;
        m_contourMinAreaSlider = addSliderRow(page, layout, "最小面积", 0, 2000, 100, &minAreaValueLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // BackgroundSubtraction 背景建模/运动检测
    {
        QWidget *page = makePage(
            "cv::BackgroundSubtractorMOG2：用高斯混合模型持续学习“背景长什么样”，和当前帧差别大的像素判定为前景(白)。"
            "在单张静态图上意义有限——第一次调用时还没学到背景，整张图都会先被当成前景；"
            "请切到左上角「摄像头实时」，对着场景保持几秒不动让它学会背景，再让人或物体入镜，才能看到真正的运动检测效果。",
            "监控/安防的移动侦测报警、智能门铃的人体感应、客流统计，都是背景建模的直接应用。",
            "");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *varLabel = nullptr;
        QLabel *rateLabel = nullptr;
        m_bgVarThresholdSlider = addSliderRow(page, layout, "方差阈值", 4, 100, 16, &varLabel);
        m_bgLearningRateSlider = addSliderRow(page, layout, "学习速度‰", 0, 100, 10, &rateLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }

    // FaceDetection 人脸检测
    {
        QWidget *page = makePage(
            "cv::CascadeClassifier + Haar级联：用大量正/负样本训练出的一系列简单矩形特征分类器级联而成，"
            "在图像上多尺度滑动窗口快速排除非人脸区域。检测到人脸(绿框)后，会在人脸区域内再跑一次"
            "眼睛级联(黄框)——这是目标检测里常见的\"先粗定位、再在ROI里精检测\"套路。",
            "门禁/考勤的人脸打卡、相机的自动对焦选人脸、相册的人脸分组，早期方案基本都是这一套 Haar 级联。",
            "pexels-wenchengphoto-7161260.jpg");
        QVBoxLayout *layout = static_cast<QVBoxLayout *>(page->layout());
        QLabel *scaleLabel = nullptr;
        QLabel *neighborsLabel = nullptr;
        // 滑块 105-140 表示 scaleFactor 1.05-1.40
        m_faceScaleSlider = addSliderRow(page, layout, "缩放步长‰", 105, 140, 110, &scaleLabel);
        m_faceMinNeighborsSlider = addSliderRow(page, layout, "最小邻居数", 1, 10, 5, &neighborsLabel);
        layout->addStretch();
        ui->paramsStack->addWidget(page);
    }
}

void MainWindow::showImage(const QString &path)
{
    QByteArray ba = path.toLocal8Bit();
    std::string filename = ba.toStdString();

    cv::Mat image = cv::imread(filename);

    if (image.empty()) {
        ui->infoText->appendPlainText("图片读取失败: " + path);
        return;
    }
    m_originalMat = image.clone();
    m_lastImagePath = path;
    ui->infoText->appendPlainText("图片读取成功: " + path);
    ui->infoText->appendPlainText(
        QString("宽:%1 高:%2 通道:%3").arg(image.cols).arg(image.rows).arg(image.channels()));

    m_originalDisplayImage = OpenCVHelper::matToQImage(image);
    resetBackgroundSubtractor(); // 换了新图片/新的一段视频源，背景模型要重新学

    applyCurrentOperation();
}

void MainWindow::resetBackgroundSubtractor()
{
    m_bgSubtractor = cv::createBackgroundSubtractorMOG2();
}

bool MainWindow::ensureFaceCascadesLoaded()
{
    if (m_faceCascadeLoadAttempted)
        return m_faceCascadeLoadOk;

    m_faceCascadeLoadAttempted = true;

    QString facePath = resolveTestDataPath("haarcascade_frontalface_default.xml");
    QString eyePath = resolveTestDataPath("haarcascade_eye.xml");

    if (facePath.isEmpty() || eyePath.isEmpty()) {
        ui->infoText->appendPlainText("找不到人脸/眼睛级联文件，请确认 test_data 目录里有对应的 xml");
        return false;
    }

    bool faceOk = m_faceCascade.load(facePath.toLocal8Bit().toStdString());
    bool eyeOk = m_eyeCascade.load(eyePath.toLocal8Bit().toStdString());

    if (!faceOk || !eyeOk) {
        ui->infoText->appendPlainText("人脸/眼睛级联文件加载失败，文件可能损坏");
        return false;
    }

    m_faceCascadeLoadOk = true;
    return true;
}

void MainWindow::onSourceModeChanged()
{
    // 每次切换先统一停掉上一个来源（摄像头/视频）
    m_cameraTimer->stop();
    if (m_capture.isOpened())
        m_capture.release();

    if (m_staticImageRadio->isChecked()) {
        ui->openImageButton->setEnabled(true);
        if (!m_lastImagePath.isEmpty())
            showImage(m_lastImagePath);
        return;
    }

    ui->openImageButton->setEnabled(false);

    if (m_cameraRadio->isChecked()) {
        if (!m_capture.open(0)) {
            ui->infoText->appendPlainText("摄像头打开失败，请检查设备是否可用或被其他程序占用");
            m_staticImageRadio->setChecked(true); // 会重新触发本槽走静态图片分支，完成回退
            return;
        }
        ui->infoText->appendPlainText("摄像头已打开，开始实时预览");
        m_cameraTimer->start(33);
        return;
    }

    // 视频文件模式：弹窗选一个视频文件，循环播放，方便没有摄像头时也能测摄像头相关算子
    // （比如背景建模/运动检测，本质上就是"喂给同一套逐帧处理逻辑一段连续帧"，不关心帧的来源）
    QString filename = QFileDialog::getOpenFileName(
        this, "选择视频文件", m_lastOpenDir, "Videos (*.mp4 *.avi *.mov *.mkv *.wmv *.flv)");
    if (filename.isEmpty()) {
        m_staticImageRadio->setChecked(true);
        return;
    }

    if (!m_capture.open(filename.toLocal8Bit().toStdString())) {
        ui->infoText->appendPlainText("视频打开失败: " + filename);
        m_staticImageRadio->setChecked(true);
        return;
    }

    QFileInfo fileInfo(filename);
    m_lastOpenDir = fileInfo.absolutePath();
    ui->infoText->appendPlainText("视频已打开，循环播放: " + filename);

    double fps = m_capture.get(cv::CAP_PROP_FPS);
    int interval = (fps > 1.0 && fps < 240.0) ? static_cast<int>(1000.0 / fps) : 33;
    m_cameraTimer->start(interval);
}

void MainWindow::onCameraFrame()
{
    cv::Mat frame;
    if (!m_capture.read(frame) || frame.empty()) {
        // 视频播完了：跳回第0帧循环播放；摄像头掉帧则什么都不做，等下一次定时器
        if (m_videoFileRadio->isChecked() && m_capture.isOpened())
            m_capture.set(cv::CAP_PROP_POS_FRAMES, 0);
        return;
    }

    m_originalMat = frame.clone();
    m_originalDisplayImage = OpenCVHelper::matToQImage(frame);

    applyCurrentOperation();
}

void MainWindow::updateImageDisplay()
{
    if (!m_originalDisplayImage.isNull()) {
        QSize labelSize = ui->originalImageLabel->size();
        if (labelSize.width() > 0 && labelSize.height() > 0) {
            QPixmap pix = QPixmap::fromImage(m_originalDisplayImage)
                              .scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            ui->originalImageLabel->setPixmap(pix);
            // KeepAspectRatio 缩放后 pixmap 可能比 label 小（留黑边），取色器反算坐标要基于
            // pixmap 在 label 内实际居中显示的区域，而不是整个 label 区域。
            m_originalPixmapRect = QRect(QPoint((labelSize.width() - pix.width()) / 2,
                                                 (labelSize.height() - pix.height()) / 2),
                                          pix.size());
        }
    }

    if (!m_processedDisplayImage.isNull()) {
        QSize labelSize = ui->processedImageLabel->size();
        if (labelSize.width() > 0 && labelSize.height() > 0) {
            QPixmap pix = QPixmap::fromImage(m_processedDisplayImage)
                              .scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            ui->processedImageLabel->setPixmap(pix);
            m_processedPixmapRect = QRect(QPoint((labelSize.width() - pix.width()) / 2,
                                                  (labelSize.height() - pix.height()) / 2),
                                           pix.size());
        }
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    updateImageDisplay();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_cameraTimer->stop();
    if (m_capture.isOpened())
        m_capture.release();

    m_settings->setValue("Window/Width", width());

    m_settings->setValue("Window/Height", height());

    QMainWindow::closeEvent(event);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);

    static bool firstShow = true;

    if (firstShow) {
        firstShow = false;

        QString lastImage = m_settings->value("File/LastImagePath", "").toString();

        if (!lastImage.isEmpty() && QFileInfo::exists(lastImage)) {
            showImage(lastImage);
            ui->infoText->appendPlainText("恢复上次图片:" + lastImage);
            return;
        }

        if (!lastImage.isEmpty())
            ui->infoText->appendPlainText("上次图片不存在:" + lastImage);

        // 没有可恢复的图片时，自动加载一张示例图，避免界面空白
        QString demoPath = resolveTestDataPath("pexels-724211268-34685018.jpg");
        if (!demoPath.isEmpty()) {
            showImage(demoPath);
            ui->infoText->appendPlainText("已自动加载示例图片，随时可点『打开图片』换成你自己的图");
        }
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    bool isOriginalLabel = (watched == ui->originalImageLabel);
    bool isProcessedLabel = (watched == ui->processedImageLabel);

    if (isOriginalLabel || isProcessedLabel) {
        const cv::Mat &mat = isOriginalLabel ? m_originalMat : m_lastResultMat;
        const QRect &pixmapRect = isOriginalLabel ? m_originalPixmapRect : m_processedPixmapRect;

        // 把 label 里的鼠标坐标反算成原图/结果图上的像素坐标，越界返回 false
        auto mapToPixel = [&](const QPoint &pos, int &outX, int &outY) -> bool {
            if (mat.empty() || !pixmapRect.contains(pos))
                return false;
            double fx = static_cast<double>(pos.x() - pixmapRect.left()) / pixmapRect.width();
            double fy = static_cast<double>(pos.y() - pixmapRect.top()) / pixmapRect.height();
            outX = std::clamp(static_cast<int>(fx * mat.cols), 0, mat.cols - 1);
            outY = std::clamp(static_cast<int>(fy * mat.rows), 0, mat.rows - 1);
            return true;
        };

        if (event->type() == QEvent::MouseMove) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            int px, py;
            if (mapToPixel(mouseEvent->pos(), px, py)) {
                QString text;
                if (mat.channels() == 1) {
                    int gray = mat.at<uchar>(py, px);
                    text = QString("像素(%1,%2)  灰度值: %3").arg(px).arg(py).arg(gray);
                } else {
                    cv::Vec3b bgr = mat.at<cv::Vec3b>(py, px);
                    cv::Mat pixel(1, 1, CV_8UC3, cv::Scalar(bgr[0], bgr[1], bgr[2]));
                    cv::Mat hsvPixel;
                    cv::cvtColor(pixel, hsvPixel, cv::COLOR_BGR2HSV);
                    cv::Vec3b hsv = hsvPixel.at<cv::Vec3b>(0, 0);
                    text = QString("像素(%1,%2)  BGR(%3,%4,%5)  HSV(%6,%7,%8)  —— 点击原图可查看该点的运算细节")
                               .arg(px)
                               .arg(py)
                               .arg(bgr[0])
                               .arg(bgr[1])
                               .arg(bgr[2])
                               .arg(hsv[0])
                               .arg(hsv[1])
                               .arg(hsv[2]);
                }
                statusBar()->showMessage(text);
            } else {
                statusBar()->clearMessage();
            }
        } else if (event->type() == QEvent::Leave) {
            statusBar()->clearMessage();
        } else if (isOriginalLabel && event->type() == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            int px, py;
            if (mapToPixel(mouseEvent->pos(), px, py))
                showPixelInspector(px, py);
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::saveResult()
{
    if (m_lastResultMat.empty()) {
        ui->infoText->appendPlainText("还没有可保存的处理结果，先打开一张图片试试算子吧");
        return;
    }

    QString defaultPath = m_lastOpenDir.isEmpty() ? "result.png" : (m_lastOpenDir + "/result.png");
    QString filename = QFileDialog::getSaveFileName(this, "保存处理结果", defaultPath,
                                                      "PNG (*.png);;JPEG (*.jpg)");
    if (filename.isEmpty())
        return;

    bool ok = cv::imwrite(filename.toLocal8Bit().toStdString(), m_lastResultMat);
    if (ok)
        ui->infoText->appendPlainText("已保存: " + filename);
    else
        ui->infoText->appendPlainText("保存失败: " + filename);
}

void MainWindow::showTermsGlossary()
{
    static const QString html = QStringLiteral(R"(
        <h2>OpenCV 术语速查手册</h2>
        <p style="color:#666">面试复习用：按分类整理，斜体是常见追问点。</p>

        <h3>图像基础</h3>
        <p><b>像素 / 通道 (Pixel / Channel)</b>：一张彩色图每个像素由 B/G/R 三个通道的数值组成，
        每个通道 0-255（8位）。<i>追问：为什么 OpenCV 默认是 BGR 而不是 RGB？（历史遗留，早期相机厂商的习惯）</i></p>
        <p><b>ROI (Region of Interest)</b>：图像里你只关心的一小块矩形区域，OpenCV 里用 <code>Mat(Rect)</code> 直接切片，不拷贝数据（浅拷贝）。</p>
        <p><b>掩码 (Mask)</b>：一张和原图同尺寸的单通道图，非0的位置表示"要处理"，配合 <code>copyTo</code>/<code>bitwise_and</code> 实现"只处理指定区域"。</p>

        <h3>色彩空间</h3>
        <p><b>HSV</b>：色相(Hue,0-179)/饱和度(Saturation)/明度(Value) 三个维度，比 BGR 更贴近人眼对"颜色"的直觉。
        <i>追问：为什么颜色识别/追踪常转 HSV 而不直接用 BGR 做阈值？（BGR 三通道都会随光照亮度整体变化，同一个"红色"在暗光下三通道数值全变了；HSV 把亮度单独分离到 V 通道，H 通道对光照变化更稳定）</i></p>

        <h3>滤波与卷积</h3>
        <p><b>卷积核 / Kernel</b>：一个小矩阵（比如3x3），在图像上滑动，和覆盖到的像素做加权求和，得到输出像素值。</p>
        <p><b>锚点 (Anchor)</b>：核在滑动时对齐到当前像素的参考点，默认是核的中心。</p>
        <p><b>为什么核大小通常是奇数</b>：奇数核才有唯一的正中心像素，方便锚点对齐；偶数核中心在四个像素之间，没有单一锚点。</p>
        <p><b>Sigma（高斯）</b>：控制高斯分布的"胖瘦"，Sigma 越大权重分布越平缓、模糊范围越大。</p>
        <p><b>线性滤波 vs 非线性滤波</b>：高斯/均值模糊是加权求和（线性）；中值滤波取排序后的中间值（非线性），非线性滤波通常更擅长保边、抗椒盐噪声。
        <i>追问：为什么中值滤波对椒盐噪声特别有效？（噪声点数值极端，排序后大概率被挤到两端，取中位数天然被过滤掉）</i></p>

        <h3>形态学</h3>
        <p><b>结构元素 (Structuring Element)</b>：形态学操作里的"核"，通常是矩形/十字/椭圆形状的 0/1 矩阵，决定了"邻域"的形状。</p>
        <p><b>腐蚀 (Erode) / 膨胀 (Dilate)</b>：腐蚀取邻域最小值（前景收缩），膨胀取邻域最大值（前景扩张），二者是对偶运算。</p>
        <p><b>开运算 (Open) = 先腐蚀后膨胀</b>：去掉前景上的小噪点，同时保持整体形状。</p>
        <p><b>闭运算 (Close) = 先膨胀后腐蚀</b>：填补前景内部的小孔洞、连接断裂区域。
        <i>追问：开运算和闭运算分别适合什么场景？（开=去噪点，闭=补空洞/连断线，记忆口诀：开运算先腐蚀会让小噪点先被"吃掉"再也补不回来）</i></p>

        <h3>边缘与梯度</h3>
        <p><b>一阶导数 (Sobel) vs 二阶导数 (Laplacian)</b>：Sobel 反映亮度变化的"快慢"（梯度），在边缘处取极值；
        Laplacian 反映梯度的变化率，在边缘处过零点，对噪声更敏感、常用于锐化。</p>
        <p><b>非极大值抑制 (NMS)</b>：Canny 边缘检测的一步，只保留梯度方向上局部最大的点，把粗边缘变成单像素细边缘。</p>
        <p><b>双阈值 / 滞后阈值 (Hysteresis Thresholding)</b>：Canny 用高低两个阈值，高于高阈值直接判定为边缘，
        低于低阈值直接舍弃，中间的点只有连到已确认边缘时才保留——避免边缘断断续续。</p>

        <h3>阈值与分割</h3>
        <p><b>全局阈值 vs 自适应阈值</b>：全局阈值整张图用同一个数字切割，光照不均匀时会失败；
        自适应阈值给每个像素用它自己邻域算出的局部阈值，更抗光照不均。</p>
        <p><b>Otsu 算法</b>：遍历所有可能阈值，让"前景类"和"背景类"的类间方差最大（等价于类内方差最小），
        自动找出最优全局阈值，前提是直方图要有明显双峰。</p>

        <h3>特征与关键点</h3>
        <p><b>关键点 (Keypoint) vs 描述子 (Descriptor)</b>：关键点是"在哪"（坐标+尺度+方向），
        描述子是"长什么样"（一串数字向量，用于和其他关键点做相似度匹配）。</p>
        <p><b>ORB = FAST + BRIEF</b>：用 FAST 算法快速找角点当关键点，用 BRIEF（的旋转不变改进版）算描述子，
        比 SIFT/SURF 快很多，是专利免费的替代方案。
        <i>追问：ORB 和 SIFT 相比有什么劣势？（对尺度和噪声的鲁棒性通常不如 SIFT，但速度快得多，适合实时场景）</i></p>

        <h3>面试高频问题速答</h3>
        <p style="color:#666">按"面试官可能怎么问 / 你可以怎么答"整理，考前过一遍，答案控制在能口述的长度。</p>

        <p><b>Q: 怎么理解卷积在图像处理里的作用？</b><br>
        A: 用一个小核在图像上滑动，每滑到一个位置就把核和覆盖到的像素做加权求和，作为该位置的输出。
        核的形状和权重决定了这是模糊、锐化还是边缘检测，本质是给每个像素引入邻域信息。</p>

        <p><b>Q: 高斯模糊和均值模糊有什么区别？</b><br>
        A: 均值模糊核内权重全相等；高斯模糊权重按高斯分布，离中心越近权重越大，更符合"越近的像素相关性越强"的直觉，
        模糊效果更自然、不会有明显的方块感。</p>

        <p><b>Q: 双边滤波为什么能保边去噪？</b><br>
        A: 普通高斯模糊只看空间距离；双边滤波同时看空间距离和像素值差异两个权重，两个像素值差异很大（大概率跨越了边缘）
        时权重就会很小，边缘两侧不会被互相糊到一起，代价是计算量比高斯大很多。</p>

        <p><b>Q: 腐蚀和膨胀的底层实现原理？</b><br>
        A: 用结构元素在图像上滑动，腐蚀取窗口内最小值、膨胀取最大值，对二值图就是让前景收缩或扩张。
        开运算/闭运算/梯度/顶帽/黑帽都是这两个基础算子的不同组合。</p>

        <p><b>Q: Canny 边缘检测完整流程说一下？</b><br>
        A: 高斯模糊降噪 → Sobel 算梯度幅值和方向 → 非极大值抑制细化边缘 → 双阈值滞后阈值处理连接边缘，
        四步缺一不可，是经典的多阶段 pipeline 设计。</p>

        <p><b>Q: 霍夫变换的核心思想是什么？</b><br>
        A: 把图像空间的点映射到参数空间（比如直线用极坐标 ρ,θ 表示），图像空间里共线的点在参数空间会在同一处
        产生投票峰值，找峰值就等于找到了直线/圆，本质是一种投票算法。</p>

        <p><b>Q: 直方图均衡化和 CLAHE 的区别？</b><br>
        A: equalizeHist 对整张图统一拉伸对比度，局部明暗差异大时效果可能不均甚至放大噪声；CLAHE 先分块局部均衡化，
        并限制对比度放大倍数（clip limit）防止噪声过度放大，再对块边界做插值平滑过渡。</p>

        <p><b>Q: 模板匹配的原理和局限性？</b><br>
        A: 滑动窗口在大图上逐位置计算模板和窗口的相似度（比如归一化相关系数），取最大值位置。
        局限是对旋转、缩放、光照变化很敏感，模板必须和目标长得几乎一样，工程上更常用特征点匹配替代。</p>

        <p><b>Q: findContours 找到轮廓之后能做什么？</b><br>
        A: boundingRect 求外接矩形定位目标、contourArea 按面积过滤噪声轮廓、approxPolyDP 做多边形近似识别形状、
        minEnclosingCircle 找最小外接圆，是目标计数/形状识别的基础。</p>

        <p><b>Q: 仿射变换和透视变换有什么区别？</b><br>
        A: 仿射变换保证"平行线依然平行"（旋转/缩放/平移/错切的组合），2x3矩阵描述；透视变换不保证平行线平行
        （比如照片里近大远小的效果），需要4个对应点、3x3矩阵描述，能处理"拍歪的文档摆正"这类问题。</p>

        <p><b>Q: MOG2 背景建模的基本原理？</b><br>
        A: 对每个像素位置维护若干个高斯分布描述"这个位置历史上长什么样"，新一帧来了判断当前像素值是否落在
        已有分布内：落在内=背景，落在外=前景（运动物体），背景模型本身也会持续用新帧缓慢更新。</p>

        <p><b>Q: Haar 级联做人脸检测的基本原理？</b><br>
        A: 用大量矩形特征（相邻矩形区域像素和的差值）当弱分类器，AdaBoost 把很多弱分类器级联成强分类器；
        检测时滑动窗口+图像金字塔多尺度扫描，级联结构能在前几级就快速排除绝大多数非人脸窗口，所以速度很快。</p>

        <p><b>Q: 图像金字塔有什么实际用途？</b><br>
        A: 多尺度处理的基础——同一个物体在图片里可能有不同大小，在金字塔多个尺度上分别检测就能兼顾大小目标；
        SIFT 这类特征算法也是在图像金字塔上找"尺度不变"的关键点。</p>

        <p><b>Q: 自适应阈值和 Otsu 该怎么选？</b><br>
        A: 图片整体光照均匀、直方图有明显双峰，用 Otsu 一步到位不用调参；光照不均匀（比如手机拍文档一角有阴影）
        要用自适应阈值，因为它是给每个像素用邻域局部算出的阈值。</p>

        <p><b>Q: 怎么用 OpenCV 实现"抠出图片里的某种颜色"？</b><br>
        A: 先 BGR 转 HSV，inRange 给 H/S/V 三个通道各设一个范围生成掩码，再用原图和掩码做 bitwise_and，
        只保留掩码非0的像素；调参时 H 是最关键的，S/V 主要用来排除过暗/过白的干扰像素。</p>
    )");

    QDialog dialog(this);
    dialog.setWindowTitle("OpenCV 术语速查手册");
    dialog.resize(620, 640);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QTextBrowser *browser = new QTextBrowser(&dialog);
    browser->setHtml(html);
    layout->addWidget(browser);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog.exec();
}

void MainWindow::showPixelInspector(int px, int py)
{
    if (m_originalMat.empty())
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(QString("像素探索 - (%1, %2)").arg(px).arg(py));
    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    cv::Vec3b originBgr = m_originalMat.at<cv::Vec3b>(py, px);
    QLabel *headerLabel = new QLabel(
        QString("点击像素 (%1, %2)　原图 BGR = (%3, %4, %5)")
            .arg(px)
            .arg(py)
            .arg(originBgr[0])
            .arg(originBgr[1])
            .arg(originBgr[2]),
        &dialog);
    headerLabel->setWordWrap(true);
    layout->addWidget(headerLabel);

    if (!m_lastResultMat.empty() && px < m_lastResultMat.cols && py < m_lastResultMat.rows) {
        QString outText;
        if (m_lastResultMat.channels() == 1) {
            outText = QString("当前算子实际输出（来自OpenCV计算结果）：灰度值 = %1")
                          .arg(m_lastResultMat.at<uchar>(py, px));
        } else {
            cv::Vec3b outBgr = m_lastResultMat.at<cv::Vec3b>(py, px);
            outText = QString("当前算子实际输出（来自OpenCV计算结果）：BGR = (%1, %2, %3)")
                          .arg(outBgr[0])
                          .arg(outBgr[1])
                          .arg(outBgr[2]);
        }
        QLabel *outLabel = new QLabel(outText, &dialog);
        outLabel->setWordWrap(true);
        layout->addWidget(outLabel);
    }

    auto grayAt = [this](int x, int y) -> int {
        if (x < 0 || y < 0 || x >= m_originalMat.cols || y >= m_originalMat.rows)
            return -1;
        cv::Vec3b bgr = m_originalMat.at<cv::Vec3b>(y, x);
        return cvRound(0.114 * bgr[0] + 0.587 * bgr[1] + 0.299 * bgr[2]);
    };

    auto makeGrid = [&dialog](int rows, int cols,
                               const std::function<QString(int, int)> &valueAt) -> QTableWidget * {
        QTableWidget *table = new QTableWidget(rows, cols, &dialog);
        table->horizontalHeader()->setVisible(false);
        table->verticalHeader()->setVisible(false);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionMode(QAbstractItemView::NoSelection);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                QTableWidgetItem *item = new QTableWidgetItem(valueAt(r, c));
                item->setTextAlignment(Qt::AlignCenter);
                table->setItem(r, c, item);
            }
        }
        int cellSize = cols <= 7 ? 42 : 30;
        for (int c = 0; c < cols; ++c)
            table->setColumnWidth(c, cellSize);
        for (int r = 0; r < rows; ++r)
            table->setRowHeight(r, cellSize);
        table->setFixedHeight(cellSize * rows + 4);
        table->setFixedWidth(cellSize * cols + 4);
        return table;
    };

    enum class InspectorKind { PointOp, NeighborhoodOp, NotApplicable };
    InspectorKind kind = InspectorKind::NotApplicable;
    switch (m_currentOp) {
    case OperationType::Original:
    case OperationType::Invert:
    case OperationType::Gray:
    case OperationType::Threshold:
    case OperationType::HsvView:
    case OperationType::ColorRange:
    case OperationType::EqualizeHist:
    case OperationType::CLAHE:
    case OperationType::OtsuThreshold:
        kind = InspectorKind::PointOp;
        break;
    case OperationType::GaussianBlur:
    case OperationType::MedianBlur:
    case OperationType::BilateralFilter:
    case OperationType::Sobel:
    case OperationType::Laplacian:
    case OperationType::Erode:
    case OperationType::Dilate:
    case OperationType::MorphOpen:
    case OperationType::MorphClose:
    case OperationType::MorphGradient:
    case OperationType::TopHat:
    case OperationType::BlackHat:
    case OperationType::AdaptiveThreshold:
        kind = InspectorKind::NeighborhoodOp;
        break;
    default:
        break;
    }

    QStringList relatedTerms;

    if (kind == InspectorKind::PointOp) {
        QString formula;
        switch (m_currentOp) {
        case OperationType::Original:
            formula = "点运算：输出 = 输入，原样显示。";
            break;
        case OperationType::Invert:
            formula = QString("点运算：output = 255 - input，每个通道独立计算，例如 B 通道: 255-%1=%2")
                          .arg(originBgr[0])
                          .arg(255 - originBgr[0]);
            break;
        case OperationType::Gray:
            formula = "点运算：gray = 0.299*R + 0.587*G + 0.114*B，只用这一个像素的三个通道计算。";
            break;
        case OperationType::Threshold:
            formula = "点运算：先转灰度，再和阈值比较，大于阈值输出255否则输出0，只看这一个像素的灰度值。";
            break;
        case OperationType::HsvView:
            formula = "点运算：对这一个像素做 BGR→HSV 坐标转换，不看邻居。";
            break;
        case OperationType::ColorRange:
            formula = "点运算：把这个像素的 HSV 值和你设的范围比较，在范围内保留、不在范围内变黑。";
            break;
        case OperationType::EqualizeHist:
            formula = "点运算（但用的是全局LUT）：这个像素的灰度值查一张根据全图直方图算出的映射表得到新值，运算本身只看这一个值。";
            break;
        case OperationType::CLAHE:
            formula = "点运算（但LUT是按局部小块算的）：比 equalizeHist 多一步先判断这个像素属于哪个小块，再查那个小块专属的映射表。";
            break;
        case OperationType::OtsuThreshold:
            formula = "点运算：用（根据全图直方图自动算出的）阈值和这个像素的灰度值比较，运算本身只看这一个值。";
            break;
        default:
            formula = "点运算：输出只取决于这一个像素的值。";
            break;
        }
        QLabel *explainLabel = new QLabel("【点运算 Point Operation】\n" + formula, &dialog);
        explainLabel->setWordWrap(true);
        layout->addWidget(explainLabel);
        relatedTerms << "像素 / 通道" << "全局阈值 vs 自适应阈值";

    } else if (kind == InspectorKind::NeighborhoodOp) {
        int kernelSize = 3;
        cv::Mat weightKernel; // 空表示不展示第二个网格
        QString weightNote;
        bool isStructuring = false;

        switch (m_currentOp) {
        case OperationType::GaussianBlur: {
            kernelSize = 2 * m_blurKernelSlider->value() + 1;
            cv::Mat k1 = cv::getGaussianKernel(kernelSize, m_blurSigmaSlider->value(), CV_64F);
            weightKernel = k1 * k1.t();
            break;
        }
        case OperationType::Sobel: {
            kernelSize = 2 * m_sobelKernelSlider->value() + 1;
            cv::Mat kx, ky;
            cv::getDerivKernels(kx, ky, 1, 0, kernelSize, false, CV_64F);
            weightKernel = ky * kx.t();
            break;
        }
        case OperationType::Laplacian: {
            kernelSize = 2 * m_laplacianKernelSlider->value() + 1;
            if (kernelSize == 3) {
                weightKernel = (cv::Mat_<double>(3, 3) << 0, 1, 0, 1, -4, 1, 0, 1, 0);
            } else {
                weightNote = "该核大小下 OpenCV 用二阶 Sobel 近似实现，没有单一固定核；核大小切回默认值1可看经典3x3拉普拉斯核。";
            }
            break;
        }
        case OperationType::MedianBlur:
            kernelSize = 2 * m_medianKernelSlider->value() + 1;
            weightNote = "中值滤波没有固定权重：直接取邻域内排序后的中间值，不是加权求和。";
            break;
        case OperationType::BilateralFilter:
            kernelSize = m_bilateralDiameterSlider->value();
            weightNote = "双边滤波的权重是动态算出来的：同时看空间距离和颜色差异，每个像素的权重都不一样，没有固定核。";
            break;
        case OperationType::Erode:
            kernelSize = m_erodeKernelSlider->value();
            isStructuring = true;
            break;
        case OperationType::Dilate:
            kernelSize = m_dilateKernelSlider->value();
            isStructuring = true;
            break;
        case OperationType::MorphOpen:
            kernelSize = m_morphOpenKernelSlider->value();
            isStructuring = true;
            break;
        case OperationType::MorphClose:
            kernelSize = m_morphCloseKernelSlider->value();
            isStructuring = true;
            break;
        case OperationType::MorphGradient:
            kernelSize = m_morphGradientKernelSlider->value();
            isStructuring = true;
            break;
        case OperationType::TopHat:
            kernelSize = m_topHatKernelSlider->value();
            isStructuring = true;
            break;
        case OperationType::BlackHat:
            kernelSize = m_blackHatKernelSlider->value();
            isStructuring = true;
            break;
        case OperationType::AdaptiveThreshold:
            kernelSize = 2 * m_adaptiveBlockSlider->value() + 3;
            weightNote = "自适应阈值：拿这个邻域窗口算局部均值/高斯加权均值，再减常数C当作这个像素专属的阈值，不是普通卷积。";
            break;
        default:
            break;
        }

        if (isStructuring) {
            cv::Mat se = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(kernelSize, kernelSize));
            se.convertTo(weightKernel, CV_64F);
            weightNote = "当前用矩形结构元素(MORPH_RECT)，覆盖到的位置全部为1；换成十字形/椭圆形结构元素会看到0/1交替的图案。";
        }

        int dispN = std::min(kernelSize, 11);
        if (dispN % 2 == 0)
            dispN -= 1;
        if (dispN < 1)
            dispN = 1;
        int half = dispN / 2;

        QLabel *neighborTitle = new QLabel(
            QString("【邻域/卷积运算 Neighborhood Operation】核大小 %1x%1%2")
                .arg(kernelSize)
                .arg(kernelSize > dispN ? QString("（表格只显示中间 %1x%1）").arg(dispN) : QString()),
            &dialog);
        neighborTitle->setWordWrap(true);
        layout->addWidget(neighborTitle);

        QLabel *neighborLabel =
            new QLabel("原始邻域灰度值（为方便展示统一用灰度，实际三个通道各自独立运算）：", &dialog);
        neighborLabel->setWordWrap(true);
        layout->addWidget(neighborLabel);
        QTableWidget *neighborGrid = makeGrid(dispN, dispN, [&](int r, int c) {
            int gv = grayAt(px + (c - half), py + (r - half));
            return gv < 0 ? QStringLiteral("—") : QString::number(gv);
        });
        layout->addWidget(neighborGrid);

        if (!weightKernel.empty()) {
            QLabel *kernelLabel = new QLabel(isStructuring ? "结构元素 (0/1)：" : "卷积核权重：", &dialog);
            layout->addWidget(kernelLabel);
            int kCenter = weightKernel.rows / 2;
            QTableWidget *kernelGrid = makeGrid(dispN, dispN, [&](int r, int c) {
                int rr = kCenter + (r - half);
                int cc = kCenter + (c - half);
                if (rr < 0 || cc < 0 || rr >= weightKernel.rows || cc >= weightKernel.cols)
                    return QStringLiteral("—");
                double v = weightKernel.at<double>(rr, cc);
                return isStructuring ? QString::number(static_cast<int>(v)) : QString::number(v, 'f', 3);
            });
            layout->addWidget(kernelGrid);
        }

        if (!weightNote.isEmpty()) {
            QLabel *noteLabel = new QLabel(weightNote, &dialog);
            noteLabel->setWordWrap(true);
            noteLabel->setStyleSheet("color:#616161; font-size:12px;");
            layout->addWidget(noteLabel);
        }

        relatedTerms << "卷积核 / Kernel" << "锚点 (Anchor)" << "线性滤波 vs 非线性滤波";

    } else {
        QLabel *naLabel = new QLabel(
            "该算子不是逐像素的邻域/卷积运算（比如是基于全局统计、特征匹配或几何变换的算法），"
            "这里没有单点邻域数值可展示。可以去参数面板下方的代码面板看它的实际调用。",
            &dialog);
        naLabel->setWordWrap(true);
        layout->addWidget(naLabel);
    }

    if (!relatedTerms.isEmpty()) {
        QLabel *termsLabel =
            new QLabel("相关术语：" + relatedTerms.join("、") + "（详见左侧“术语手册”）", &dialog);
        termsLabel->setWordWrap(true);
        termsLabel->setStyleSheet("color:#0078D4; font-size:12px; margin-top:8px;");
        layout->addWidget(termsLabel);
    }

    layout->addStretch();

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}
