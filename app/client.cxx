#include <QBoxLayout>
#include <QPushButton>
#include <QWidget>
#include <QLabel>
#include <QCheckBox>
#include <QLineEntry>
#include <QListView>

void OpenServerSelect(void)
{
	auto ConnectWindow = new QWidget();
	ConnectWindow->setTitle("Zarbomedia - Select Server");
	ConnectWindow->setDeleteOnClose(true);
	
	
	auto ConnectLayout = new QBoxLayout(TopToBottom);
	
	auto ConnectEntryLayout = new QBoxLayout(LeftToRight);
	auto ServerLabel = new QLabel("Server");
	ConnectEntryLayout->addWidget(ServerLabel);
	auto Server = new QLineEntry("sctp://server:port");
	ConnectEntryLayout->addWidget(Server);
	ConnectLayout->AddLayout(ConnectEntryLayout);
	
	auto FavoriteEntryLayout = new QBoxLayout(LeftToRight);
	auto Favorite = new QCheckBox("Remember as");
	FavoriteEntryLayout->Add(Favorite);
	auto FavoriteName = new QLineEntry("");
	FavoriteEntryLayout->Add(FavoriteName);
	ConnectLayout->AddLayout(FavoriteEntryLayout);
	
	RecentServers = new QListView();
	ConnectLayout->AddWidget(RecentServers);
	
	ActionLayout = new QBoxLayout(RightToLeft);
	auto Connect = new QPushButton("Connect");
	ActionLayout->Add(Connect);
	auto Quit = new QPushButton("Quit");
	ActionLayout->Add(Quit);
	ConnectLayout->AddWidget(ActionLayout);
	
	ConnectWindow->SetLayout(RecentServers);
	ConnectWindow->Show();
}

/*void OpenPlayer(void)
{
	QWidget MainWindow;
	MainWindow->SetTitle("Zarbomedia - Player");
	QTextEdit ChatDisplay;
	QLineEdit ChatEntry;
	QListView Playlist;
	QToolBar PlaylistControls;
	QToolButton Add;
	QToolButton Shuffle;
	QToolButton AlphaSort;
	QToolButton Previous;
	QToolButton PlayStop;
	QToolButton Next;
}*/

int main(int argc, char **argv)
{
	QApplication QTContext(argc, argv);
	
	OpenServerDialog();
	
	return QTContext.exec();
	
	return 0;
}
