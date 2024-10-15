#include "mainwindow.h"
#include <QWidget>
#include <QSplitter>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QMessageBox>
#include <QFileDialog>
#include <boost/thread.hpp>
#include <iostream>

using boost::asio::ip::tcp;

const QString applicationName("P2P File Transfer Client");

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    createWidgets();
    createLayouts();
    initializeLogging(); // Initialize logging
}

MainWindow::~MainWindow() {
    m_peer.leaveNetwork();
    
    if (m_logFile.isOpen()) {
        m_logStream << "===== Log Ended at " << QDateTime::currentDateTime().toString() << " =====\n";
        m_logFile.close();
    }
}


void MainWindow::initializeLogging() {
    QString logFileName = "p2p_analysis.log";
    m_logFile.setFileName(logFileName);

    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Logging Error"),
                             tr("Cannot open log file for writing: %1").arg(logFileName));
        return;
    }

    m_logStream.setDevice(&m_logFile);
    m_logStream << "===== P2P File Transfer Client Log Started at "
               << QDateTime::currentDateTime().toString()
               << " =====\n";
    m_logStream.flush();
}

void MainWindow::writeLog(const QString &message) {
    if (m_logFile.isOpen()) {
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        m_logStream << "[" << timestamp << "] " << message << "\n";
        m_logStream.flush();
    }
}

void MainWindow::createWidgets() {
    m_peersList = new QListWidget();
    m_availableFilesList = new QListWidget();
    m_sharedFilesList = new QListWidget();
    m_addrBar = new QLineEdit();
    m_addrBar->setPlaceholderText(tr("Connection Manager Address"));
    m_portBar = new QLineEdit();
    m_portBar->setPlaceholderText(tr("Connection Manager Port"));
    m_connectButton = new QPushButton(tr("Connect"));

    m_searchBar = new QLineEdit();
    m_searchBar->setPlaceholderText(tr("Search"));
    m_searchButton = new QToolButton();
    m_searchButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    m_addFileButton = new QPushButton(tr("Share a File"));
    m_remFileButton = new QPushButton(tr("Unshare a File"));
    m_downloadFileButton = new QPushButton(tr("Download"));

    m_downloadResultMessage = new QMessageBox(this);

    // disallow add/rem/download files until connected to network
    m_addFileButton->setEnabled(false);
    m_remFileButton->setEnabled(false);
    m_downloadFileButton->setEnabled(false);

    connect(m_connectButton, SIGNAL(clicked()), this, SLOT(s_connect()));
    connect(m_addFileButton, SIGNAL(clicked()), this, SLOT(s_addShareFile()));
    connect(m_remFileButton, SIGNAL(clicked()), this, SLOT(s_remShareFile()));
    connect(m_downloadFileButton, SIGNAL(clicked()), this, SLOT(s_downloadAvailableFile()));
    connect(m_addrBar, SIGNAL(returnPressed()), this, SLOT(s_connect()));
    connect(m_portBar, SIGNAL(returnPressed()), this, SLOT(s_connect()));
    connect(this, SIGNAL(downloadStatus(QString,int)), this, SLOT(s_downloadStatus(QString,int)));
    connect(this, SIGNAL(updateLists()), this, SLOT(s_updateLists()));
}

void MainWindow::createLayouts() {
    QWidget* centralWidget = new QWidget();
    QVBoxLayout* centralLayout = new QVBoxLayout(centralWidget);

    QHBoxLayout* topBox = new QHBoxLayout();
    QSplitter* topSplit = new QSplitter(Qt::Orientation::Horizontal);

    QHBoxLayout* botBox = new QHBoxLayout();
    QVBoxLayout* lBox = new QVBoxLayout();
    QVBoxLayout* rBox = new QVBoxLayout();

    // Top-left, lists hosts
    lBox->addWidget(new QLabel(tr("Peers")));
    lBox->addWidget(m_peersList);

    // Top-right, lists files available for download
    QHBoxLayout* addFileLayout = new QHBoxLayout();
    addFileLayout->addWidget(new QLabel(tr("Shared Files")));
    addFileLayout->addWidget(m_addFileButton);
    addFileLayout->addWidget(m_remFileButton);
    rBox->addLayout(addFileLayout);
    rBox->addWidget(m_sharedFilesList);
    QHBoxLayout* downloadFileLayout = new QHBoxLayout();
    downloadFileLayout->addWidget(new QLabel(tr("Available Files")));
    downloadFileLayout->addWidget(m_downloadFileButton);
    rBox->addLayout(downloadFileLayout);
    rBox->addWidget(m_availableFilesList);
    QHBoxLayout* searchLayout = new QHBoxLayout();
    searchLayout->addWidget(m_searchBar);
    searchLayout->addWidget(m_searchButton);
    //rBox->addLayout(searchLayout);

    // Splitter for hosts and files lists
    QWidget* lWidget = new QWidget();
    QWidget* rWidget = new QWidget();
    lWidget->setLayout(lBox);
    rWidget->setLayout(rBox);
    topSplit->addWidget(lWidget);
    topSplit->addWidget(rWidget);

    topBox->addWidget(topSplit);
    botBox->addWidget(m_addrBar);
    botBox->addWidget(m_portBar);
    botBox->addWidget(m_connectButton);

    centralLayout->addLayout(topBox);
    centralLayout->addLayout(botBox);

    this->setCentralWidget(centralWidget);
    this->setWindowTitle(applicationName);
}

void MainWindow::refreshPeerList() {
    const std::vector<std::string> peers = m_peer.getPeersList();

    m_peersList->clear();
    for (std::vector<std::string>::const_iterator it = peers.begin(); it != peers.end(); ++it) {
        m_peersList->addItem(QString::fromStdString(*it));
    }
}

void MainWindow::refreshShareList() {
    m_sharedFilesList->clear();
    for (std::vector<std::string>::const_iterator it = m_peer.getSharedFilesList().begin(); it != m_peer.getSharedFilesList().end(); ++it) {
        m_sharedFilesList->addItem(QString::fromStdString(*it));
    }
    m_peer.printSharedFiles();
}

void MainWindow::refreshAvailableList() {
    m_availableFilesList->clear();
    std::map<std::string, std::string> availMap = m_peer.getAvailableList();
    for(auto const &it : availMap) {
        m_availableFilesList->addItem(QString::fromStdString(it.first));
    }
}

void MainWindow::startAcceptorThread() {
    boost::thread acceptorThread(boost::bind(&MainWindow::waitForPeers, this));
}

void MainWindow::waitForPeers() {
    for (;;) {
        m_peer.handleConnection();
        emit updateLists();
    }
}

void MainWindow::downloadFile() {
    QList<QListWidgetItem*> selectedItems = m_availableFilesList->selectedItems();
    foreach (QListWidgetItem* item, selectedItems) {
        std::string filename = item->text().toUtf8().constData();
        try {
            emit downloadStatus(QString::fromStdString(filename), DOWNLOAD_START);
            QDateTime startTime = QDateTime::currentDateTime();
            m_peer.downloadAvailableFile(filename);
            QDateTime endTime = QDateTime::currentDateTime();
            qint64 downloadTime = startTime.msecsTo(endTime); // in milliseconds

            emit downloadStatus(QString::fromStdString(filename), DOWNLOAD_SUCCESS);

            // Log the download details
            writeLog(QString("Downloaded file: %1 | Time: %2 ms")
                     .arg(QString::fromStdString(filename))
                     .arg(downloadTime));
        } catch (std::exception& e) {
            std::cerr << "MainWindow::s_downloadAvailableFile(): Download failed. " << e.what() << std::endl;
            emit downloadStatus(QString::fromStdString(filename), DOWNLOAD_FAILURE);
            writeLog(QString("Failed to download file: %1 | Error: %2")
                     .arg(QString::fromStdString(filename))
                     .arg(e.what()));
        }
    }
}


void MainWindow::s_connect() {
    try {
        m_peer.setConnectionManagerAddress(m_addrBar->text().toUtf8().constData());
        m_peer.setConnectionManagerPort(m_portBar->text().toUtf8().constData());
        m_peer.joinNetwork();

        m_connectButton->setEnabled(false);     // disable ability to reconnect
        m_addrBar->setEnabled(false);
        m_portBar->setEnabled(false);

        m_addFileButton->setEnabled(true);
        m_remFileButton->setEnabled(true);
        m_downloadFileButton->setEnabled(true);

        refreshPeerList();
        refreshAvailableList();

        setWindowTitle(windowTitle() + QString(" [%1]").arg(QString::fromStdString(m_peer.getAcceptorPort())));
        startAcceptorThread();                  // start separate thread for receiving connections

        writeLog(QString("Connected to Connection Manager at %1:%2")
                 .arg(m_addrBar->text())
                 .arg(m_portBar->text()));
    } catch(std::exception& e) {
        QMessageBox::information(
            this,
            applicationName,
            QString(tr("Unable to connect to Connection Manager.\n" \
                       "Please make sure the address and port are valid and that it is running.")));

        m_peer.setConnectionManagerAddress("");
        m_peer.setConnectionManagerPort("");
        m_portBar->clear();
        m_addrBar->clear();

        writeLog("Failed to connect to Connection Manager.");
    }
}

void MainWindow::s_addShareFile() {
    std::string filepath = QFileDialog::getOpenFileName(this, tr("Share a File")).toUtf8().constData();
    if (filepath.empty()) return;

    try {
        m_peer.addShareFile(filepath);
        writeLog(QString("Shared file: %1").arg(QString::fromStdString(filepath)));
    } catch (std::invalid_argument& e) {
        std::cerr << "MainWindow::s_addShareFile(): " << e.what() << std::endl;
        QMessageBox::information(
            this,
            applicationName,
            QString(tr("Unable to add file \"%1\" because it already exists.")).arg(QFileInfo(QString::fromStdString(filepath)).fileName()));
        writeLog(QString("Failed to share file (already exists): %1").arg(QString::fromStdString(filepath)));
        return;
    } catch (std::exception& e) {
        std::cerr << "MainWindow::s_addShareFile(): " << e.what() << std::endl;
        QMessageBox::information(
            this,
            applicationName,
            QString(tr("Unable to add file \"%1\". \nMake sure you have connected to the Connection Manager.")).arg(QFileInfo(QString::fromStdString(filepath)).fileName()));
        writeLog(QString("Failed to share file: %1").arg(QString::fromStdString(filepath)));
        return;
    }
    refreshShareList();
}

void MainWindow::s_remShareFile() {
    // loop through all selected files
    QList<QListWidgetItem*> selectedItems = m_sharedFilesList->selectedItems();
    foreach (QListWidgetItem* item, selectedItems) {
        std::string filepath = item->text().toUtf8().constData();

        try {
            m_peer.remShareFile(filepath);
            writeLog(QString("Unshared file: %1").arg(QString::fromStdString(filepath)));
        } catch (std::exception& e) {
            std::cerr << "MainWindow:s_remShareFile(): " << e.what() << std::endl;
            QMessageBox::information(
                this,
                applicationName,
                QString(tr("Unable to remove file \"%1\".\nGet rekt.")).arg(QFileInfo(QString::fromStdString(filepath)).fileName()));
            writeLog(QString("Failed to unshare file: %1").arg(QString::fromStdString(filepath)));
            return;
        }
    }
    refreshShareList();
}

void MainWindow::s_downloadAvailableFile() {
    boost::thread downloadThread(boost::bind(&MainWindow::downloadFile, this));
}

void MainWindow::s_downloadStatus(const QString filename, const int downloadStatus) {
    if (downloadStatus == DOWNLOAD_START) {
        m_downloadResultMessage->setText(QString(tr("Downloading \"%1\".\nYou will be notified once download has completed.")).arg(filename));
        writeLog(QString("Download started for file: %1").arg(filename));
    } else if (downloadStatus == DOWNLOAD_SUCCESS) {
        m_downloadResultMessage->setText(QString(tr("File \"%1\" has finished downloading.")).arg(filename));
        writeLog(QString("Download succeeded for file: %1").arg(filename));
    } else {
        m_downloadResultMessage->setText(QString(tr("\"%1\" download failed.")).arg(filename));
        writeLog(QString("Download failed for file: %1").arg(filename));
    }
    m_downloadResultMessage->show();
}


void MainWindow::s_updateLists() {
    refreshPeerList();
    refreshAvailableList();
}
