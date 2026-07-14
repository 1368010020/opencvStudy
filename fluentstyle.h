#ifndef FLUENTSTYLE_H
#define FLUENTSTYLE_H

#include <QString>

// Fluent Design 风格 QSS，用于 MainWindow::setStyleSheet
namespace FluentStyle {

inline QString styleSheet()
{
    return QStringLiteral(R"(
        * {
            font-family: "Segoe UI", "Microsoft YaHei UI", sans-serif;
        }

        QMainWindow, QWidget#centralwidget {
            background-color: #F3F3F3;
        }

        /* ---- 左侧导航 ---- */
        QListWidget#navList {
            background-color: #F9F9F9;
            border: none;
            outline: none;
            font-size: 13px;
            padding: 4px;
        }

        QListWidget#navList::item {
            height: 36px;
            padding-left: 12px;
            border-radius: 6px;
            color: #202020;
            margin: 2px 4px;
        }

        QListWidget#navList::item:hover {
            background-color: #ECECEC;
        }

        QListWidget#navList::item:selected {
            background-color: #E1EEFB;
            color: #0078D4;
            font-weight: 600;
            border-left: 3px solid #0078D4;
        }

        /* 分组标题行：不可点击，弱化显示 */
        QListWidget#navList::item:disabled {
            color: #9E9E9E;
            font-weight: 700;
            font-size: 11px;
            height: 24px;
            padding-left: 8px;
            background-color: transparent;
        }

        /* ---- 通用按钮 ---- */
        QPushButton {
            background-color: #0078D4;
            color: white;
            border: none;
            border-radius: 6px;
            padding: 8px 14px;
            font-size: 13px;
        }

        QPushButton:hover {
            background-color: #106EBE;
        }

        QPushButton:pressed {
            background-color: #005A9E;
        }

        QPushButton:disabled {
            background-color: #C7C7C7;
            color: #F0F0F0;
        }

        /* ---- 次要按钮（如"加载推荐图片"）：描边风格，弱化于主按钮 ---- */
        QPushButton[variant="secondary"] {
            background-color: transparent;
            color: #0078D4;
            border: 1px solid #0078D4;
            border-radius: 6px;
            padding: 6px 10px;
            font-size: 12px;
            font-weight: 600;
            text-align: left;
        }

        QPushButton[variant="secondary"]:hover {
            background-color: #E1EEFB;
        }

        QPushButton[variant="secondary"]:pressed {
            background-color: #CCE4F7;
        }

        /* ---- 图片来源单选按钮 ---- */
        QRadioButton {
            color: #202020;
            font-size: 12px;
            spacing: 6px;
        }

        QRadioButton::indicator {
            width: 14px;
            height: 14px;
            border-radius: 7px;
            border: 2px solid #8A8A8A;
            background-color: #FFFFFF;
        }

        QRadioButton::indicator:hover {
            border-color: #0078D4;
        }

        QRadioButton::indicator:checked {
            border: 2px solid #0078D4;
            background-color: #0078D4;
        }

        /* ---- 卡片容器 ---- */
        QGroupBox {
            background-color: #FFFFFF;
            border: 1px solid #E0E0E0;
            border-radius: 8px;
            margin-top: 14px;
            font-size: 13px;
            font-weight: 600;
            color: #202020;
        }

        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 6px;
            color: #0078D4;
        }

        /* ---- 图片显示区域 ---- */
        QLabel#originalImageLabel, QLabel#processedImageLabel {
            background-color: #FAFAFA;
            border: 1px dashed #D0D0D0;
            border-radius: 6px;
            color: #A0A0A0;
        }

        /* ---- 参数面板 ---- */
        QWidget#rightPanel, QStackedWidget#paramsStack {
            background-color: transparent;
        }

        QLabel {
            color: #202020;
            font-size: 12px;
        }

        QLabel[role="hint"] {
            color: #616161;
            font-size: 12px;
        }

        QLabel[role="value"] {
            color: #0078D4;
            font-weight: 600;
        }

        /* ---- 应用场景说明：用绿色和"原理说明"的灰色区分开 ---- */
        QLabel[role="scenario"] {
            color: #107C10;
            font-size: 12px;
            font-weight: 600;
        }

        /* ---- 滑块 ---- */
        QSlider::groove:horizontal {
            height: 4px;
            background: #D6D6D6;
            border-radius: 2px;
        }

        QSlider::handle:horizontal {
            background: #0078D4;
            width: 16px;
            height: 16px;
            margin: -6px 0;
            border-radius: 8px;
        }

        QSlider::handle:horizontal:hover {
            background: #106EBE;
        }

        QSlider::sub-page:horizontal {
            background: #0078D4;
            border-radius: 2px;
        }

        /* ---- 信息日志 ---- */
        QPlainTextEdit#infoText {
            background-color: #FFFFFF;
            border: 1px solid #E0E0E0;
            border-radius: 6px;
            font-family: "Consolas", "Cascadia Mono", monospace;
            font-size: 12px;
            color: #404040;
        }

        /* ---- 代码面板：实时显示当前算子对应的真实 OpenCV 调用 ---- */
        QPlainTextEdit#codeSnippetText {
            background-color: #1E1E1E;
            border: 1px solid #333333;
            border-radius: 6px;
            font-family: "Consolas", "Cascadia Mono", monospace;
            font-size: 12px;
            color: #D4D4D4;
            padding: 4px;
        }

        QSplitter::handle {
            background-color: #F3F3F3;
        }
    )");
}

} // namespace FluentStyle

#endif // FLUENTSTYLE_H
