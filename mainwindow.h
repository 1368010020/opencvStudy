#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRect>
#include <QSettings>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <opencv2/opencv.hpp>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

// 支持的算子，顺序即 paramsStack 的页面顺序（与 navList 行号无关，
// navList 里夹了分组标题行，靠 Qt::UserRole 存的 OperationType 关联）
// 约定：新增算子只加在所属分组的末尾，且 buildNavList()/buildParamPanels()
// 必须按同样的顺序追加，否则页面和算子会错位。
enum class OperationType {
    // -- 基础点运算 --
    Original = 0,
    Invert,
    Gray,
    Threshold,
    // -- 滤波与降噪 --
    GaussianBlur,
    MedianBlur,
    BilateralFilter,
    // -- 形态学 --
    Erode,
    Dilate,
    MorphOpen,
    MorphClose,
    MorphGradient,
    TopHat,
    BlackHat,
    // -- 颜色空间 --
    HsvView,
    ColorRange,
    // -- 几何变换 --
    RotateScale,
    // -- 直方图 --
    HistogramView,
    EqualizeHist,
    CLAHE,
    // -- 边缘与梯度 --
    Sobel,
    Laplacian,
    Canny,
    // -- 霍夫变换 --
    HoughLines,
    HoughCircles,
    // -- 阈值进阶 --
    AdaptiveThreshold,
    OtsuThreshold,
    // -- 特征与匹配 --
    OrbKeypoints,
    TemplateMatch,
    Pyramid,
    // -- 轮廓 --
    Contours,
    // -- 视频分析 --
    BackgroundSubtraction,

    Count // 哨兵值，不对应任何算子
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);

    ~MainWindow();

protected:
    void resizeEvent(QResizeEvent *event) override;

    void closeEvent(QCloseEvent *event) override;

    void showEvent(QShowEvent *event) override;

    // 给原图/结果图两个 QLabel 装的取色器：监听鼠标移动，反算像素坐标
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:

    void openImage();

    // 左侧导航切换算子（跳过分组标题行）
    void onNavRowChanged(int row);

    // 唯一的重算入口：基于 m_originalMat + 当前算子的滑块参数重新计算处理结果
    void applyCurrentOperation();

    // 静态图片 / 摄像头实时 / 视频文件 三者切换（不用参数，内部读三个 radio 的选中状态）
    void onSourceModeChanged();

    // 摄像头/视频定时取帧
    void onCameraFrame();

    // 模板匹配页面专属：选择模板图片
    void selectTemplateImage();

    // 参数面板"加载推荐图片"按钮专属：加载 test_data 里的指定素材
    void loadRecommendedImage(const QString &filename);

    // 保存当前处理结果到文件
    void saveResult();

    // 打开 OpenCV 术语速查手册
    void showTermsGlossary();

private:
    // 点击原图触发：按当前算子展示"点运算"公式代入或"邻域/卷积"数值网格
    void showPixelInspector(int px, int py);

    // 切到背景建模算子/加载新图片时调用，重新开始学习背景模型
    void resetBackgroundSubtractor();

    Ui::MainWindow *ui;

    /*
     * 图片相关
     */

    // 从路径读取图片
    void showImage(const QString &path);

    // 根据窗口大小刷新左右两个图片显示
    void updateImageDisplay();

    // 填充左侧导航：分组标题（不可选中）+ 算子条目（携带 OperationType）
    void buildNavList();

    // 构建所有算子的参数面板，塞进 ui->paramsStack，顺序对应 OperationType
    void buildParamPanels();

    // 在 test_data 目录里查找推荐素材，找不到返回空字符串
    QString resolveTestDataPath(const QString &filename) const;

    // 创建一个"滑块 + 数值标签"行，返回滑块指针；value 变化时自动刷新数值标签与处理结果
    QSlider *addSliderRow(QWidget *page, QVBoxLayout *layout, const QString &labelText,
                           int minValue, int maxValue, int defaultValue, QLabel **outValueLabel);

    /*
     * OpenCV数据
     */

    // 打开的原始图片，处理算子只读它，永远不会被修改
    cv::Mat m_originalMat;

    // 最近一次算出的处理结果，供取色器/保存按钮读取
    cv::Mat m_lastResultMat;

    // 当前选中的算子
    OperationType m_currentOp = OperationType::Original;

    // 当前选中算子的显示名（导航条目文字），用于在标题里拼耗时
    QString m_currentOpName;

    /*
     * Qt显示数据
     */

    QImage m_originalDisplayImage;

    QImage m_processedDisplayImage;

    // updateImageDisplay() 里记录的 pixmap 在 label 内的实际显示区域（居中+等比缩放留出的黑边要排除），
    // 取色器反算鼠标位置对应的原图/结果图像素坐标时使用
    QRect m_originalPixmapRect;
    QRect m_processedPixmapRect;

    // 常驻代码面板：显示当前算子对应的真实 OpenCV 调用（参数会替换成滑块当前值）
    QPlainTextEdit *m_codeSnippetText = nullptr;

    // 保存处理结果按钮
    QPushButton *m_saveResultButton = nullptr;

    // 术语手册按钮
    QPushButton *m_glossaryButton = nullptr;

    /*
     * 各算子参数控件
     */

    QSlider *m_thresholdSlider = nullptr;

    QSlider *m_blurKernelSlider = nullptr;
    QSlider *m_blurSigmaSlider = nullptr;

    QSlider *m_medianKernelSlider = nullptr;

    QSlider *m_bilateralDiameterSlider = nullptr;
    QSlider *m_bilateralSigmaColorSlider = nullptr;
    QSlider *m_bilateralSigmaSpaceSlider = nullptr;

    QSlider *m_cannyLowSlider = nullptr;
    QSlider *m_cannyHighSlider = nullptr;

    QSlider *m_erodeKernelSlider = nullptr;
    QSlider *m_erodeIterSlider = nullptr;

    QSlider *m_dilateKernelSlider = nullptr;
    QSlider *m_dilateIterSlider = nullptr;

    QSlider *m_rotateAngleSlider = nullptr;
    QSlider *m_rotateScaleSlider = nullptr;

    QSlider *m_claheClipSlider = nullptr;
    QSlider *m_claheTileSlider = nullptr;

    QSlider *m_sobelKernelSlider = nullptr;

    QSlider *m_laplacianKernelSlider = nullptr;

    QSlider *m_adaptiveBlockSlider = nullptr;
    QSlider *m_adaptiveCSlider = nullptr;

    QSlider *m_contourMinAreaSlider = nullptr;

    QSlider *m_morphOpenKernelSlider = nullptr;
    QSlider *m_morphOpenIterSlider = nullptr;

    QSlider *m_morphCloseKernelSlider = nullptr;
    QSlider *m_morphCloseIterSlider = nullptr;

    QSlider *m_morphGradientKernelSlider = nullptr;

    QSlider *m_topHatKernelSlider = nullptr;

    QSlider *m_blackHatKernelSlider = nullptr;

    QSlider *m_colorRangeHLowSlider = nullptr;
    QSlider *m_colorRangeHHighSlider = nullptr;
    QSlider *m_colorRangeSLowSlider = nullptr;
    QSlider *m_colorRangeSHighSlider = nullptr;
    QSlider *m_colorRangeVLowSlider = nullptr;
    QSlider *m_colorRangeVHighSlider = nullptr;

    QSlider *m_houghLinesThresholdSlider = nullptr;
    QSlider *m_houghLinesMinLengthSlider = nullptr;
    QSlider *m_houghLinesMaxGapSlider = nullptr;

    QSlider *m_houghCirclesMinDistSlider = nullptr;
    QSlider *m_houghCirclesParam1Slider = nullptr;
    QSlider *m_houghCirclesParam2Slider = nullptr;
    QSlider *m_houghCirclesMinRadiusSlider = nullptr;
    QSlider *m_houghCirclesMaxRadiusSlider = nullptr;

    QSlider *m_orbFeaturesSlider = nullptr;

    QSlider *m_templateCropPercentSlider = nullptr;
    QPushButton *m_selectTemplateButton = nullptr;
    QLabel *m_templateStatusLabel = nullptr;

    QSlider *m_pyramidLevelSlider = nullptr;

    // 背景建模/运动检测
    cv::Ptr<cv::BackgroundSubtractorMOG2> m_bgSubtractor;
    QSlider *m_bgVarThresholdSlider = nullptr;
    QSlider *m_bgLearningRateSlider = nullptr;

    // 模板匹配页面用户手动选择的模板图，为空时自动取原图中心裁剪代替
    cv::Mat m_templateMat;

    /*
     * 图片来源：静态图片 / 摄像头实时
     */

    QRadioButton *m_staticImageRadio = nullptr;
    QRadioButton *m_cameraRadio = nullptr;
    QRadioButton *m_videoFileRadio = nullptr;

    QTimer *m_cameraTimer = nullptr;
    cv::VideoCapture m_capture;

    // 摄像头模式下用于恢复的最近一次打开的图片路径
    QString m_lastImagePath;

    /*
     * 配置
     */

    // 最近打开目录
    QString m_lastOpenDir;

    // ini配置文件
    QSettings *m_settings = nullptr;
};

#endif // MAINWINDOW_H
