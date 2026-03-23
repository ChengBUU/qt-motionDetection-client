#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QtEndian>
#include <QResizeEvent>
#include <QFileDialog> // 【新增】文件对话框头文件
#include <QDateTime>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , socket(new QTcpSocket(this))
    , receivingImage(false)
    , expectedImageSize(0)
    , isRealDataMode(false)
    , snapshotRequested(false) // 【初始化】抓拍标志位
{
    ui->setupUi(this);

    // 视频标签自适应
    ui->videoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->videoLabel->setMinimumSize(320, 240);
    ui->videoLabel->setAlignment(Qt::AlignCenter);

    frameTimer.start();

    // 信号槽
    connect(ui->connectButton, &QPushButton::clicked,
            this, &MainWindow::onConnectButtonClicked);
    connect(ui->sensitivitySlider, &QSlider::valueChanged,
            this, &MainWindow::onSensitivityChanged);
    connect(ui->snapshotButton, &QPushButton::clicked,
            this, &MainWindow::onSnapshotClicked);

    connect(socket, &QTcpSocket::readyRead,
            this, &MainWindow::onReadyRead);
    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &MainWindow::onSocketErrorOccurred);
    connect(socket, &QTcpSocket::stateChanged,
            this, &MainWindow::onSocketStateChanged);

    ui->logTextBrowser->append("程序启动，等待连接...");
}

MainWindow::~MainWindow()
{
    if (socket->state() == QAbstractSocket::ConnectedState) {
        socket->disconnectFromHost();
    }
    delete ui;
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    const QPixmap* currentPix = ui->videoLabel->pixmap();
    if (currentPix != nullptr && !currentPix->isNull()) {
        QPixmap scaled = currentPix->scaled(ui->videoLabel->size(),
                                            Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation);
        ui->videoLabel->setPixmap(scaled);
    }
}

void MainWindow::onConnectButtonClicked()
{
    if (socket->state() == QAbstractSocket::ConnectedState) {
        socket->disconnectFromHost();
        ui->connectButton->setText("连接");
        ui->logTextBrowser->append("已断开连接");
    } else {
        QString ip = ui->ipLineEdit->text().trimmed();
        bool portOk;
        quint16 port = static_cast<quint16>(ui->portLineEdit->text().toUInt(&portOk));

        if (ip.isEmpty() || !portOk || port == 0) {
            QMessageBox::warning(this, "参数错误", "请输入有效的IP地址和端口号！");
            return;
        }

        // 连接前重置所有状态
        receivingImage = false;
        expectedImageSize = 0;
        imageBuffer.clear();
        snapshotRequested = false; // 【重置】抓拍标志
        frameTimer.restart();

        socket->connectToHost(ip, port);
        ui->connectButton->setText("连接中...");
        ui->logTextBrowser->append(QString("正在连接 %1:%2 ...").arg(ip).arg(port));
    }
}

void MainWindow::onSensitivityChanged(int value)
{
    if (socket->state() == QAbstractSocket::ConnectedState) {
        QString cmd = QString("set_area %1\n").arg(value);
        socket->write(cmd.toUtf8());
        socket->flush();
        ui->logTextBrowser->append(QString("发送灵敏度命令: %1").arg(cmd.trimmed()));
    }
}

// ========== 核心修改1：抓拍按钮点击事件 ==========
void MainWindow::onSnapshotClicked()
{
    // 检查是否有视频画面
    const QPixmap* currentPix = ui->videoLabel->pixmap();
    if (currentPix == nullptr || currentPix->isNull()) {
        QMessageBox::warning(this, "抓拍失败", "当前没有视频画面，无法抓拍！");
        ui->logTextBrowser->append("抓拍失败：无视频画面");
        return;
    }

    // 【关键】设置抓拍标志位，不发送命令给服务器
    snapshotRequested = true;
    ui->logTextBrowser->append("已请求抓拍，等待下一帧...");
}

// ========== 核心修改2：onReadyRead中处理抓拍保存 ==========
void MainWindow::onReadyRead()
{
    // 先无条件读所有数据
    imageBuffer.append(socket->readAll());

    while (true) {
        if (!receivingImage) {
            if (imageBuffer.size() < 4) break;
            QByteArray sizeData = imageBuffer.left(4);
            quint32 networkSize = *reinterpret_cast<const quint32*>(sizeData.data());
            expectedImageSize = static_cast<int>(qFromBigEndian(networkSize));
            imageBuffer = imageBuffer.mid(4);
            receivingImage = true;
        }

        if (receivingImage) {
            if (imageBuffer.size() < expectedImageSize) break;

            QByteArray frameData = imageBuffer.left(expectedImageSize);
            imageBuffer = imageBuffer.mid(expectedImageSize);
            receivingImage = false;

            // 解码帧
            QPixmap pix;
            bool decodeSuccess = pix.loadFromData(frameData, "JPEG");

            if (decodeSuccess) {
                // 显示画面
                QPixmap scaled = pix.scaled(ui->videoLabel->size(),
                                            Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation);
                ui->videoLabel->setPixmap(scaled);

                // ========== 核心修改3：检查抓拍标志位，保存照片 ==========
                if (snapshotRequested) {
                    snapshotRequested = false; // 立即重置标志位

                    // 1. 生成默认文件名（带时间戳）
                    QString defaultFileName = QString("snapshot_%1.jpg")
                        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz"));

                    // 2. 弹出文件保存对话框，让用户选路径
                    QString selectedFilePath = QFileDialog::getSaveFileName(
                        this,
                        "保存抓拍照片",
                        defaultFileName, // 默认文件名
                        "JPEG 图片 (*.jpg);;PNG 图片 (*.png);;所有文件 (*.*)" // 文件过滤器
                    );

                    // 3. 如果用户选了路径，就保存
                    if (!selectedFilePath.isEmpty()) {
                        if (pix.save(selectedFilePath)) {
                            // 保存成功
                            ui->logTextBrowser->append(QString("抓拍成功，已保存到: %1").arg(selectedFilePath));
                            QMessageBox::information(this, "抓拍成功", "照片已保存！");
                        } else {
                            // 保存失败
                            ui->logTextBrowser->append("抓拍失败：无法保存文件");
                            QMessageBox::warning(this, "抓拍失败", "无法保存文件，请检查路径权限！");
                        }
                    } else {
                        // 用户取消了保存
                        ui->logTextBrowser->append("用户取消了抓拍保存");
                    }
                }
            } else {
                static int decodeFailCount = 0;
                decodeFailCount++;
                if (decodeFailCount % 10 == 0) {
                    ui->logTextBrowser->append(QString("图像解码失败 (累计%1次)").arg(decodeFailCount));
                }
            }

            // 丢帧策略（仅用于显示，不影响抓拍）
            if (frameTimer.elapsed() >= 100) {
                frameTimer.restart();
            }
        }
    }
}

void MainWindow::onSocketErrorOccurred(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    QString errorStr = socket->errorString();
    ui->logTextBrowser->append(QString("Socket 错误: %1").arg(errorStr));
    ui->connectButton->setText("连接");
}

void MainWindow::onSocketStateChanged(QAbstractSocket::SocketState state)
{
    if (state == QAbstractSocket::ConnectedState) {
        ui->connectButton->setText("断开");
        ui->logTextBrowser->append("已连接成功");
        isRealDataMode = true;

        int curValue = ui->sensitivitySlider->value();
        onSensitivityChanged(curValue);
    } else if (state == QAbstractSocket::UnconnectedState) {
        ui->connectButton->setText("连接");
        ui->logTextBrowser->append("连接已断开");
        isRealDataMode = false;

        ui->videoLabel->clear();
        ui->videoLabel->setText("无视频流");
        receivingImage = false;
        expectedImageSize = 0;
        imageBuffer.clear();
        snapshotRequested = false; // 断开时重置抓拍标志
    }
}
