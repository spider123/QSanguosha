#include "mainwindow.h"
#include "startscene.h"
#include "roomscene.h"
#include "server.h"
#include "client.h"
#include "generaloverview.h"
#include "cardoverview.h"
#include "ui_mainwindow.h"
#include "audiere.h"
#include "libircclient.h"
#include "scenario-overview.h"

extern audiere::AudioDevicePtr Device;

#include <QGraphicsView>
#include <QGraphicsItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsTextItem>
#include <QVariant>
#include <QMessageBox>
#include <QTime>
#include <QProcess>
#include <QCheckBox>
#include <QFileDialog>
#include <QDesktopServices>
#include <QSystemTrayIcon>

class FitView : public QGraphicsView
{
public:
    FitView(QGraphicsScene *scene) : QGraphicsView(scene) {
        setSceneRect(Config.Rect);
    }

protected:
    virtual void resizeEvent(QResizeEvent *event) {
        QGraphicsView::resizeEvent(event);
        if(Config.FitInView)
            fitInView(sceneRect(), Qt::KeepAspectRatio);

        if(scene()->inherits("RoomScene")){
            RoomScene *room_scene = qobject_cast<RoomScene *>(scene());
            room_scene->adjustItems();
        }
    }
};

MainWindow::MainWindow(QWidget *parent)
    :QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // initialize random seed for later use
    qsrand(QTime(0,0,0).secsTo(QTime::currentTime()));

    connection_dialog = new ConnectionDialog(this);
    connect(ui->actionStart_Game, SIGNAL(triggered()), connection_dialog, SLOT(show()));    
    connect(connection_dialog, SIGNAL(accepted()), this, SLOT(startConnection()));
    connect(connection_dialog, SIGNAL(wan_detect()), ui->actionWAN_IP_detect, SIGNAL(triggered()));

    config_dialog = new ConfigDialog(this);
    connect(ui->actionConfigure, SIGNAL(triggered()), config_dialog, SLOT(show()));
    connect(config_dialog, SIGNAL(bg_changed()), this, SLOT(changeBackground()));   

    connect(ui->actionAbout_Qt, SIGNAL(triggered()), qApp, SLOT(aboutQt()));

    StartScene *start_scene = new StartScene;    

    QList<QAction*> actions;
    actions << ui->actionStart_Game            
            << ui->actionStart_Server
            << ui->actionReplay
            << ui->actionConfigure
            << ui->actionGeneral_Overview
            << ui->actionCard_Overview
            << ui->actionScenario_Overview;

    foreach(QAction *action, actions)
        start_scene->addButton(action);    

    scene = start_scene;
    FitView *view = new FitView(scene);

    setCentralWidget(view);
    restoreFromConfig();

    addAction(ui->actionShow_Hide_Menu);
    addAction(ui->actionFullscreen);   
    addAction(ui->actionMinimize_to_system_tray);

    systray = NULL;
}

void MainWindow::restoreFromConfig(){
    resize(Config.value("WindowSize", QSize(1042, 719)).toSize());
    move(Config.value("WindowPosition", QPoint(20,20)).toPoint());

    QFont font;
    if(Config.AppFont != font)
        QApplication::setFont(Config.AppFont);
    if(Config.UIFont != font)
        QApplication::setFont(Config.UIFont, "QTextEdit");

    ui->actionEnable_Hotkey->setChecked(Config.EnableHotKey);
    ui->actionNever_Nullify_My_Trick->setChecked(Config.NeverNullifyMyTrick);
}

void MainWindow::closeEvent(QCloseEvent *event){
    Config.setValue("WindowSize", size());
    Config.setValue("WindowPosition", pos());

    if(systray){
        systray->showMessage(windowTitle(), tr("Game is minimized"));
        hide();
        event->ignore();
    }
}

MainWindow::~MainWindow()
{    
    delete ui;
}

void MainWindow::gotoScene(QGraphicsScene *scene){
    QGraphicsView *view = qobject_cast<QGraphicsView *>(centralWidget());
    view->setScene(scene);
    delete this->scene;
    this->scene = scene;
}

void MainWindow::on_actionExit_triggered()
{
    QMessageBox::StandardButton result;
    result = QMessageBox::question(this,
                                   tr("Sanguosha"),
                                   tr("Are you sure to exit?"),
                                   QMessageBox::Ok | QMessageBox::Cancel);
    if(result == QMessageBox::Ok){
        systray = NULL;
        close();
    }
}

void MainWindow::on_actionStart_Server_triggered()
{
    ServerDialog *dialog = new ServerDialog(this);
    if(!dialog->config())
        return;

    Server *server = new Server(this);
    if(! server->listen()){
        QMessageBox::warning(this, tr("Warning"), tr("Can not start server!"));

        return;
    }

    server->daemonize();

    ui->actionStart_Game->disconnect();
    connect(ui->actionStart_Game, SIGNAL(triggered()), this, SLOT(startGameInAnotherInstance()));

    StartScene *start_scene = qobject_cast<StartScene *>(scene);
    if(start_scene){
        start_scene->switchToServer(server);
    }
}

void MainWindow::startConnection(){
    Client *client = new Client(this);

    connect(client, SIGNAL(error_message(QString)), SLOT(networkError(QString)));
    connect(client, SIGNAL(server_connected()), SLOT(enterRoom()));

    //client->signup();
}

void MainWindow::on_actionReplay_triggered()
{
    QString location = QDesktopServices::storageLocation(QDesktopServices::HomeLocation);
    QString filename = QFileDialog::getOpenFileName(this,
                                                    tr("Select a reply file"),
                                                    location,
                                                    tr("Replay file (*.txt)"));

    if(filename.isEmpty())
        return;

    Client *client = new Client(this, filename);

    connect(client, SIGNAL(server_connected()), SLOT(enterRoom()));

    client->signup();
}

void MainWindow::restartConnection(){
    ClientInstance = NULL;

    delete Self;
    Self = NULL;

    startConnection();
}

void MainWindow::networkError(const QString &error_msg){
    if(isVisible())
        QMessageBox::warning(this, tr("Network error"), error_msg);
}

void MainWindow::enterRoom(){
    // add current ip to history
    if(!Config.HistoryIPs.contains(Config.HostAddress)){
        Config.HistoryIPs << Config.HostAddress;
        Config.HistoryIPs.sort();
        Config.setValue("HistoryIPs", Config.HistoryIPs);
    }

    ui->actionStart_Game->setEnabled(false);
    ui->actionStart_Server->setEnabled(false);

    int player_count = Sanguosha->getPlayerCount(ServerInfo.GameMode);
    RoomScene *room_scene = new RoomScene(player_count, this);

    ui->actionView_Discarded->setEnabled(true);
    ui->actionView_distance->setEnabled(true);
    ui->actionServerInformation->setEnabled(true);
    ui->actionKick->setEnabled(true);
    ui->actionSurrender->setEnabled(true);
    ui->actionSaveRecord->setEnabled(true);

    connect(ui->actionView_Discarded, SIGNAL(triggered()), room_scene, SLOT(viewDiscards()));
    connect(ui->actionView_distance, SIGNAL(triggered()), room_scene, SLOT(viewDistance()));
    connect(ui->actionServerInformation, SIGNAL(triggered()), room_scene, SLOT(showServerInformation()));
    connect(ui->actionKick, SIGNAL(triggered()), room_scene, SLOT(kick()));
    connect(ui->actionSurrender, SIGNAL(triggered()), room_scene, SLOT(surrender()));
    connect(ui->actionSaveRecord, SIGNAL(triggered()), room_scene, SLOT(saveReplayRecord()));

    connect(room_scene, SIGNAL(restart()), this, SLOT(restartConnection()));

    gotoScene(room_scene);
}

void MainWindow::startGameInAnotherInstance(){
    QProcess::startDetached(QApplication::applicationFilePath(), QStringList());
}

void MainWindow::on_actionGeneral_Overview_triggered()
{
    GeneralOverview *overview = new GeneralOverview(this);
    overview->show();
}

void MainWindow::on_actionCard_Overview_triggered()
{
    CardOverview *overview = new CardOverview(this);
    overview->loadFromAll();
    overview->show();
}

void MainWindow::on_actionEnable_Hotkey_toggled(bool checked)
{
    if(Config.EnableHotKey != checked){
        Config.EnableHotKey = checked;
        Config.setValue("EnableHotKey", checked);
    }
}

void MainWindow::on_actionAbout_triggered()
{
    // Cao Cao's pixmap
    QString content =  "<center><img src=':/shencc.png'> <br /> </center>";

    // Cao Cao' poem
    QString poem = tr("Disciples dressed in blue, my heart worries for you. You are the cause, of this song without pause");
    content.append(QString("<p align='right'><i>%1</i></p>").arg(poem));

    // Cao Cao's signature
    QString signature = tr("\"A Short Song\" by Cao Cao");
    content.append(QString("<p align='right'><i>%1</i></p>").arg(signature));

    content.append(tr("This is the open source clone of the popular <b>Sanguosha</b> game,"
                      "totally written in C++ Qt GUI framework <br />"
                      "My Email: moligaloo@gmail.com <br/>"));

    content.append(tr("Current version: %1 <br/>").arg(Sanguosha->getVersion()));

    const char *date = __DATE__;
    const char *time = __TIME__;
    content.append(tr("Compilation time: %1 %2 <br/>").arg(date).arg(time));

    QString project_url = "http://github.com/Moligaloo/QSanguosha";
    content.append(tr("Project home: <a href='%1'>%1</a> <br/>").arg(project_url));

    // FIXME: add acknowledgement

    QMessageBox::about(this, tr("About QSanguosha"), content);
}

void MainWindow::changeBackground(){
    if(scene)
        scene->setBackgroundBrush(Config.BackgroundBrush);

    if(scene->inherits("RoomScene")){
        RoomScene *room_scene = qobject_cast<RoomScene *>(scene);
        room_scene->changeTextEditBackground();
    }
}

void MainWindow::on_actionAbout_audiere_triggered()
{
    QString content = tr("Audiere is a high-level audio engine <br/>");
    QString address = "http://audiere.sourceforge.net/";
    content.append(tr("Official site: <a href='%1'>%1</a> <br/>").arg(address));
    content.append(tr("Current version %1 <br/>").arg(audiere::GetVersion()));
    content.append(tr("Device name: %1 <br/>").arg(Device->getName()));

    QMessageBox::about(this, tr("About audiere"), content);
}

void MainWindow::on_actionFullscreen_triggered()
{
    if(isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void MainWindow::on_actionShow_Hide_Menu_triggered()
{
    QMenuBar *menu_bar = menuBar();
    menu_bar->setVisible(! menu_bar->isVisible());
}

void MainWindow::on_actionAbout_libircclient_triggered()
{
    QString content = tr("libircclient is a small but powerful library, which implements client-server IRC protocol. <br/>");
    QString address = "http://libircclient.sourceforge.net";
    content.append(tr("Official site: <a href='%1'>%1</a> <br/>").arg(address));

    char version[255];
    unsigned int high, low;
    irc_get_version(&high, &low);
    sprintf(version, "%d.%02d", high, low);
    content.append(tr("Current version %1 <br/>").arg(version));

    QMessageBox::about(this, tr("About libircclient"), content);
}

void MainWindow::on_actionWAN_IP_detect_triggered()
{
    QStringList args;
    args << "-detect";
    QProcess::startDetached(QApplication::applicationFilePath(), args);
}

void MainWindow::on_actionMinimize_to_system_tray_triggered()
{
    if(systray == NULL){
        QIcon icon(":/magatamas/5.png");
        systray = new QSystemTrayIcon(icon, this);

        QAction *appear = new QAction(tr("Show main window"), this);
        connect(appear, SIGNAL(triggered()), this, SLOT(show()));

        QMenu *menu = new QMenu;
        menu->addAction(appear);
        menu->addMenu(ui->menuGame);
        menu->addMenu(ui->menuView);
        menu->addMenu(ui->menuOptions);
        menu->addMenu(ui->menuHelp);

        systray->setContextMenu(menu);

        systray->show();
        systray->showMessage(windowTitle(), tr("Game is minimized"));

        hide();
    }
}

void MainWindow::on_actionRole_assign_table_triggered()
{
    QString content;

    QStringList headers;
    headers << tr("Count") << tr("Lord") << tr("Loyalist") << tr("Rebel") << tr("Renegade");
    foreach(QString header, headers)
        content += QString("<th>%1</th>").arg(header);

    content = QString("<tr>%1</tr>").arg(content);

    QStringList rows;
    rows << "2 1 0 1 0" << "3 1 0 1 1" << "4 1 0 2 1"
            << "5 1 1 2 1" << "6 1 1 3 1" << "6d 1 1 2 2"
            << "7 1 2 3 1" << "8 1 2 4 1" << "8d 1 2 3 2"
            << "9 1 3 4 1" << "10 1 3 4 2";

    foreach(QString row, rows){
        QStringList cells = row.split(" ");
        QString header = cells.takeFirst();
        if(header.endsWith("d")){
            header.chop(1);
            header += tr(" (double renegade)");
        }

        QString row_content;
        row_content = QString("<td>%1</td>").arg(header);
        foreach(QString cell, cells){
            row_content += QString("<td>%1</td>").arg(cell);
        }

        content += QString("<tr>%1</tr>").arg(row_content);
    }

    content = QString("<table border='1'>%1</table").arg(content);

    QMessageBox::information(this, tr("Role assign table"), content);
}

void MainWindow::on_actionScenario_Overview_triggered()
{
    ScenarioOverview *dialog = new ScenarioOverview(this);
    dialog->show();
}
