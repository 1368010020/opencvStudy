#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDebug>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QMessageBox>
#include <QPixmap>
#include <QResizeEvent>
#include <QSizePolicy>

#include "fluentstyle.h"
#include "opencvhelper.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <algorithm>

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

    // ---- 图片来源：静态图片 / 摄像头实时 ----
    m_staticImageRadio = ui->staticImageRadio;
    m_cameraRadio = ui->cameraRadio;
    connect(ui->cameraRadio, &QRadioButton::toggled, this, &MainWindow::onSourceModeChanged);

    m_cameraTimer = new QTimer(this);
    connect(m_cameraTimer, &QTimer::timeout, this, &MainWindow::onCameraFrame);

    // 图片显示区域设置
    ui->originalImageLabel->setAlignment(Qt::AlignCenter);
    ui->originalImageLabel->setScaledContents(false);
    ui->originalImageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    ui->processedImageLabel->setAlignment(Qt::AlignCenter);
    ui->processedImageLabel->setScaledContents(false);
    ui->processedImageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    buildParamPanels();

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
    if (m_cameraRadio->isChecked())
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
    ui->paramsStack->setCurrentIndex(opIndex);
    ui->processedGroupBox->setTitle(QString("处理结果 - %1").arg(item->text()));
    ui->infoText->appendPlainText("切换算子: " + item->text());

    applyCurrentOperation();
}

void MainWindow::applyCurrentOperation()
{
    if (m_originalMat.empty())
        return;

    cv::Mat result;
    cv::Mat gray;

    switch (m_currentOp) {
    case OperationType::Original:
        result = m_originalMat.clone();
        break;

    case OperationType::Invert:
        cv::bitwise_not(m_originalMat, result);
        break;

    case OperationType::Gray:
        cv::cvtColor(m_originalMat, result, cv::COLOR_BGR2GRAY);
        break;

    case OperationType::Threshold:
        cv::cvtColor(m_originalMat, gray, cv::COLOR_BGR2GRAY);
        cv::threshold(gray, result, m_thresholdSlider->value(), 255, cv::THRESH_BINARY);
        break;

    case OperationType::GaussianBlur: {
        int kernel = 2 * m_blurKernelSlider->value() + 1;
        double sigma = m_blurSigmaSlider->value();
        cv::GaussianBlur(m_originalMat, result, cv::Size(kernel, kernel), sigma);
        break;
    }

    case OperationType::MedianBlur: {
        int kernel = 2 * m_medianKernelSlider->value() + 1;
        cv::medianBlur(m_originalMat, result, kernel);
        break;
    }

    case OperationType::BilateralFilter:
        cv::bilateralFilter(m_originalMat, result, m_bilateralDiameterSlider->value(),
                             m_bilateralSigmaColorSlider->value(),
                             m_bilateralSigmaSpaceSlider->value());
        break;

    case OperationType::Erode: {
        cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_RECT, cv::Size(m_erodeKernelSlider->value(), m_erodeKernelSlider->value()));
        cv::erode(m_originalMat, result, kernel, cv::Point(-1, -1), m_erodeIterSlider->value());
        break;
    }

    case OperationType::Dilate: {
        cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_RECT, cv::Size(m_dilateKernelSlider->value(), m_dilateKernelSlider->value()));
        cv::dilate(m_originalMat, result, kernel, cv::Point(-1, -1), m_dilateIterSlider->value());
        break;
    }

    case OperationType::MorphOpen: {
        cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_RECT,
            cv::Size(m_morphOpenKernelSlider->value(), m_morphOpenKernelSlider->value()));
        cv::morphologyEx(m_originalMat, result, cv::MORPH_OPEN, kernel, cv::Point(-1, -1),
                          m_morphOpenIterSlider->value());
        break;
    }

    case OperationType::MorphClose: {
        cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_RECT,
            cv::Size(m_morphCloseKernelSlider->value(), m_morphCloseKernelSlider->value()));
        cv::morphologyEx(m_originalMat, result, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1),
                          m_morphCloseIterSlider->value());
        break;
    }

    case OperationType::MorphGradient: {
        cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_RECT,
            cv::Size(m_morphGradientKernelSlider->value(), m_morphGradientKernelSlider->value()));
        cv::morphologyEx(m_originalMat, result, cv::MORPH_GRADIENT, kernel);
        break;
    }

    case OperationType::TopHat: {
        cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_RECT, cv::Size(m_topHatKernelSlider->value(), m_topHatKernelSlider->value()));
        cv::morphologyEx(m_originalMat, result, cv::MORPH_TOPHAT, kernel);
        break;
    }

    case OperationType::BlackHat: {
        cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_RECT,
            cv::Size(m_blackHatKernelSlider->value(), m_blackHatKernelSlider->value()));
        cv::morphologyEx(m_originalMat, result, cv::MORPH_BLACKHAT, kernel);
        break;
    }

    case OperationType::HsvView:
        // 故意不转回 BGR：直接把 HSV 三个通道当成 RGB 显示，
        // 让用户直观看到这是完全不同的一套三通道坐标系。
        cv::cvtColor(m_originalMat, result, cv::COLOR_BGR2HSV);
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
        break;
    }

    case OperationType::RotateScale: {
        cv::Point2f center(m_originalMat.cols / 2.0f, m_originalMat.rows / 2.0f);
        double angle = m_rotateAngleSlider->value();
        double scale = m_rotateScaleSlider->value() / 100.0;
        cv::Mat rotMat = cv::getRotationMatrix2D(center, angle, scale);
        cv::warpAffine(m_originalMat, result, rotMat, m_originalMat.size(), cv::INTER_LINEAR,
                        cv::BORDER_CONSTANT, cv::Scalar(240, 240, 240));
        break;
    }

    case OperationType::EqualizeHist:
        cv::cvtColor(m_originalMat, gray, cv::COLOR_BGR2GRAY);
        cv::equalizeHist(gray, result);
        break;

    case OperationType::CLAHE: {
        cv::cvtColor(m_originalMat, gray, cv::COLOR_BGR2GRAY);
        double clipLimit = m_claheClipSlider->value() / 10.0;
        int tile = m_claheTileSlider->value();
        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(clipLimit, cv::Size(tile, tile));
        clahe->apply(gray, result);
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
        break;
    }

    case OperationType::Laplacian: {
        cv::cvtColor(m_originalMat, gray, cv::COLOR_BGR2GRAY);
        int ksize = 2 * m_laplacianKernelSlider->value() + 1;
        cv::Mat lap;
        cv::Laplacian(gray, lap, CV_16S, ksize);
        cv::convertScaleAbs(lap, result);
        break;
    }

    case OperationType::Canny:
        cv::cvtColor(m_originalMat, gray, cv::COLOR_BGR2GRAY);
        cv::Canny(gray, result, m_cannyLowSlider->value(), m_cannyHighSlider->value());
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
        break;
    }

    case OperationType::AdaptiveThreshold: {
        cv::cvtColor(m_originalMat, gray, cv::COLOR_BGR2GRAY);
        int blockSize = 2 * m_adaptiveBlockSlider->value() + 3; // 至少为3，且为奇数
        cv::adaptiveThreshold(gray, result, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                               cv::THRESH_BINARY, blockSize, m_adaptiveCSlider->value());
        break;
    }

    case OperationType::OtsuThreshold:
        cv::cvtColor(m_originalMat, gray, cv::COLOR_BGR2GRAY);
        cv::threshold(gray, result, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
        break;

    case OperationType::OrbKeypoints: {
        cv::Ptr<cv::ORB> orb = cv::ORB::create(m_orbFeaturesSlider->value());
        std::vector<cv::KeyPoint> keypoints;
        orb->detect(m_originalMat, keypoints);
        cv::drawKeypoints(m_originalMat, keypoints, result, cv::Scalar(0, 220, 0),
                           cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);
        break;
    }

    case OperationType::TemplateMatch: {
        cv::Mat tmpl;
        if (!m_templateMat.empty()) {
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
        break;
    }

    case OperationType::Count:
        return;
    }

    m_processedDisplayImage = OpenCVHelper::matToQImage(result);
    updateImageDisplay();
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

    applyCurrentOperation();
}

void MainWindow::onSourceModeChanged(bool cameraMode)
{
    if (!cameraMode) {
        m_cameraTimer->stop();
        if (m_capture.isOpened())
            m_capture.release();
        ui->openImageButton->setEnabled(true);

        if (!m_lastImagePath.isEmpty())
            showImage(m_lastImagePath);
        return;
    }

    ui->openImageButton->setEnabled(false);

    if (!m_capture.open(0)) {
        ui->infoText->appendPlainText("摄像头打开失败，请检查设备是否可用或被其他程序占用");
        m_staticImageRadio->setChecked(true); // 会重新触发本槽走 !cameraMode 分支，完成回退
        return;
    }

    ui->infoText->appendPlainText("摄像头已打开，开始实时预览");
    m_cameraTimer->start(33);
}

void MainWindow::onCameraFrame()
{
    cv::Mat frame;
    if (!m_capture.read(frame) || frame.empty())
        return;

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
        }
    }

    if (!m_processedDisplayImage.isNull()) {
        QSize labelSize = ui->processedImageLabel->size();
        if (labelSize.width() > 0 && labelSize.height() > 0) {
            QPixmap pix = QPixmap::fromImage(m_processedDisplayImage)
                              .scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            ui->processedImageLabel->setPixmap(pix);
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
