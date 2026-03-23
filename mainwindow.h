#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QPixmap>
#include <QElapsedTimer>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onConnectButtonClicked();
    void onSensitivityChanged(int value);
    void onSnapshotClicked();               // 抓拍按钮槽函数
    void onReadyRead();
    void onSocketErrorOccurred(QAbstractSocket::SocketError error);
    void onSocketStateChanged(QAbstractSocket::SocketState state);

private:
    Ui::MainWindow *ui;
    QTcpSocket *socket;
    QByteArray imageBuffer;
    bool receivingImage;
    int expectedImageSize;

    QElapsedTimer frameTimer;
    bool isRealDataMode;

    // 【新增】抓拍请求标志位
    bool snapshotRequested;
};

#endif // MAINWINDOW_H
