#include "regex.h"

#include <QApplication>
#include <QBoxLayout>
#include <QPushButton>
#include <QWidget>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QTreeView>
#include <QAbstractTableModel>
#include <QMessageBox>
#include <QHeaderView>
#include <QSlider>
#include <QTextEdit>
#include <QToolBar>
#include <QAction>

struct ServerInfo
{
	std::string Host;
	uint16_t Port;
	std::string URI(void) const { return String() << "icbm://" << Host << ":" << Port; }

	bool Remember;
	std::string RememberAs;

	std::string Summary(void) const
	{
		if (Remember && !RememberAs.empty())
			return String() << RememberAs << " (" << URI() << ")";
		return URI();
	}
};

struct ServerHistory_Unique
{
	size_t Count(void) { return Items.size(); }
	ServerInfo const &Get(size_t Index) const { assert(Index < Items.size()); return Items[Index]; }
	ServerInfo GetDefault(void) const
	{
		if (Items.empty()) return {"localhost", 20578, false, ""};
		return Items[0];
	}

	void RememberHistory(size_t Index, std::string const &Text)
	{
		assert(Index < Items.size());
		Items[Index].Remember = true;
		Items[Index].RememberAs = Text;
	}
	void ForgetHistory(size_t Index)
	{
		assert(Index < Items.size());
		Items[Index].Remember = false;
		Items[Index].RememberAs = std::string();
	}

	void Finish(ServerInfo const &NewItem)
	{
		std::vector<ServerInfo> NewItems;
		NewItems.reserve(Items.size() + 1);

		if (NewItem.Remember) NewItems.push_back(NewItem);
		for (auto const &Item : Items) if (Item.Remember) NewItems.push_back(Item);
		std::cout << "1 New item count is " << NewItems.size() << std::endl;

		size_t UnrememberedCount = 0;
		if (!NewItem.Remember) { NewItems.push_back(NewItem); ++UnrememberedCount; }
		for (auto const &Item : Items)
		{
			if (UnrememberedCount > 20) break;
			if (!Item.Remember)
			{
				NewItems.push_back(Item);
				++UnrememberedCount;
			}
		}
		std::cout << "New item count is " << NewItems.size() << std::endl;
		Items.swap(NewItems);
	}

	std::vector<ServerInfo> Items;
} ServerHistory;

struct ServerHistoryQTModel_Unique : QAbstractTableModel
{
	int rowCount(const QModelIndex &Parent) const { return ServerHistory.Count() + 1; }
	int columnCount(const QModelIndex &Parent) const { return 2; }
	QVariant data(const QModelIndex &Index, int Role) const
	{
		if (Index.column() > 1) return QVariant();
		if (Index.column() < 0) return QVariant();
		if (Index.row() < 0) return QVariant();
		switch (Role)
		{
			case Qt::DisplayRole:
				if (Index.row() == 0)
				{
					if (Index.column() == 0) return QVariant(false);
					if (Index.column() == 1) return QVariant("New server...");
				}
				else if (Index.row() < ServerHistory.Count() + 1)
				{
					if (Index.column() == 0) return QVariant(ServerHistory.Get(Index.row() - 1).Remember);
					if (Index.column() == 1) return QVariant(QString::fromUtf8(ServerHistory.Get(Index.row() - 1).Summary().c_str()));
				}
				else return QVariant();
			default:
				return QVariant();
		}
	}
} ServerHistoryQTModel;

void OpenPlayer(void);
void OpenServerSelect(void)
{
	auto ConnectWindow = new QWidget();
	ConnectWindow->setWindowTitle("Zarbomedia - Select Server");
	ConnectWindow->setAttribute(Qt::WA_DeleteOnClose, true);

	auto ConnectLayout = new QBoxLayout(QBoxLayout::TopToBottom);

	auto ConnectEntryLayout = new QBoxLayout(QBoxLayout::LeftToRight);
	auto ServerLabel = new QLabel("Server");
	ConnectEntryLayout->addWidget(ServerLabel);
	auto Server = new QLineEdit(QString::fromUtf8(ServerHistory.GetDefault().URI().c_str()));
	ConnectEntryLayout->addWidget(Server);
	ConnectLayout->addLayout(ConnectEntryLayout);

	auto RememberEntryLayout = new QBoxLayout(QBoxLayout::LeftToRight);
	auto Remember = new QCheckBox("Remember as");
	Remember->setChecked(false);
	RememberEntryLayout->addWidget(Remember);
	auto RememberName = new QLineEdit();
	RememberEntryLayout->addWidget(RememberName);
	ConnectLayout->addLayout(RememberEntryLayout);

	auto RecentServers = new QTreeView;
	RecentServers->header()->hide();
	RecentServers->setModel(&ServerHistoryQTModel);
	RecentServers->setSelectionMode(QAbstractItemView::SingleSelection);
	ConnectLayout->addWidget(RecentServers);

	auto HistoricRememberEntryLayout = new QBoxLayout(QBoxLayout::LeftToRight);
	auto HistoricRemember = new QLineEdit();
	HistoricRememberEntryLayout->addWidget(HistoricRemember);
	auto ToggleHistoricRemember = new QPushButton();
	HistoricRememberEntryLayout->addWidget(ToggleHistoricRemember);
	ConnectLayout->addLayout(HistoricRememberEntryLayout);

	auto ActionLayout = new QBoxLayout(QBoxLayout::RightToLeft);
	auto Connect = new QPushButton("Connect");
	ActionLayout->addWidget(Connect);
	auto Quit = new QPushButton("Quit");
	ActionLayout->addWidget(Quit);
	ActionLayout->addStretch();
	ConnectLayout->addLayout(ActionLayout);

	ConnectWindow->setLayout(ConnectLayout);

	QObject::connect(Remember, &QCheckBox::clicked, [=](bool IsChecked)
	{
		if (IsChecked) RememberName->setEnabled(true);
		else
		{
			RememberName->setText(QString());
			RememberName->setEnabled(false);
		}
	});

	auto SetHistoricRemember = [=](ServerInfo const &Info)
	{
		if (Info.Remember)
		{
			HistoricRemember->setText(QString::fromUtf8(Info.RememberAs.c_str()));
			HistoricRemember->setEnabled(false);
			ToggleHistoricRemember->setText("Forget");
			ToggleHistoricRemember->setEnabled(true);
		}
		else
		{
			HistoricRemember->setText(QString());
			HistoricRemember->setEnabled(true);
			ToggleHistoricRemember->setText("Remember");
			ToggleHistoricRemember->setEnabled(false);
		}
	};

	QObject::connect(RecentServers->selectionModel(), &QItemSelectionModel::selectionChanged, [=](const QItemSelection &selected, const QItemSelection &)
	{
		if (selected.indexes().isEmpty()) return;

		int Index = selected.indexes().first().row();

		if (Index == 0)
		{
			HistoricRemember->setEnabled(false);
			ToggleHistoricRemember->setEnabled(false);
			return;
		}

		assert(Index >= 1);
		auto Info = ServerHistory.Get(Index - 1);

		Server->setText(QString::fromUtf8(Info.URI().c_str()));
		if (!Remember->isChecked())
			RememberName->setText(QString::fromUtf8(Info.RememberAs.c_str()));

		SetHistoricRemember(Info);

		Server->setFocus();
	});

	QObject::connect(HistoricRemember, &QLineEdit::textChanged, [=](const QString &NewText)
	{
		if (NewText.isEmpty())
			ToggleHistoricRemember->setEnabled(false);
		else ToggleHistoricRemember->setEnabled(true);
	});

	QObject::connect(ToggleHistoricRemember, &QPushButton::clicked, [=](bool)
	{
		int Index = RecentServers->selectionModel()->currentIndex().row();
		assert(Index >= 1);
		if (ServerHistory.Get(Index - 1).Remember)
			ServerHistory.ForgetHistory(Index - 1);
		else ServerHistory.RememberHistory(Index - 1, HistoricRemember->text().toUtf8().data());
		Q_EMIT(ServerHistoryQTModel.index(Index, 0), ServerHistoryQTModel.index(Index, 0));
		SetHistoricRemember(ServerHistory.Get(Index - 1));
	});

	QObject::connect(Connect, &QPushButton::clicked, [=](bool)
	{
		ServerInfo Info = ServerHistory.GetDefault();
		Info.Remember = Remember->isChecked();
		if (Info.Remember)
			Info.RememberAs = RememberName->text().toUtf8().data();
		static Regex::Parser<Regex::Ignore, std::string, Regex::Ignore, uint16_t> Parse("^(icbm://)?([^:]*)(:([0-9]+))?$");
		if (!Parse(Server->text().toUtf8().data(), Info.Host, Info.Port))
		{
			QMessageBox::warning(ConnectWindow, "Invalid server", "The server you entered is invalid or in an unsupported format.");
			Server->setFocus();
			return;
		}
		std::cout << "Server '" << Server->text().toUtf8().data() << "' host '" << Info.Host << "' port '" << Info.Port << "'" << std::endl;

		ConnectWindow->close();
		ServerHistory.Finish(Info);
		OpenPlayer();
	});

	QObject::connect(Quit, &QPushButton::clicked, [=](bool)
	{
		ConnectWindow->close();
	});

	RecentServers->setCurrentIndex(ServerHistoryQTModel.index(0, 0));
	ConnectWindow->show();
}

void OpenPlayer(void)
{
	auto MainWindow = new QWidget;
	MainWindow->setWindowTitle("Zarbomedia - Player");
	MainWindow->setAttribute(Qt::WA_DeleteOnClose, true);

	auto TopLayout = new QBoxLayout(QBoxLayout::LeftToRight);

	auto LeftLayout = new QBoxLayout(QBoxLayout::TopToBottom);
	auto ChatDisplay = new QTextEdit();
	auto ChatCursor = std::make_shared(new QTextCursor(ChatDisplay->document()));
	LeftLayout->addWidget(ChatDisplay);
	auto ChatEntry = new QLineEdit();
	LeftLayout->addWidget(ChatEntry);
	TopLayout->addLayout(LeftLayout);

	auto RightLayout = new QBoxLayout(QBoxLayout::TopToBottom);
	auto Playlist = new QTreeView();
	RightLayout->addWidget(Playlist);
	auto Position = new QSlider(Qt::Horizontal);
	RightLayout->addWidget(Position);
	auto Volume = new QSlider(Qt::Horizontal);
	RightLayout->addWidget(Volume);
	auto PlaylistControls = new QToolBar;
	auto Add = new QAction("Add", nullptr);
	PlaylistControls->addAction(Add);
	auto Shuffle = new QAction("Shuffle", nullptr);
	PlaylistControls->addAction(Shuffle);
	auto AlphaSort = new QAction("AlphaSort", nullptr);
	PlaylistControls->addAction(AlphaSort);
	PlaylistControls->addSeparator();
	auto Previous = new QAction("Previous", nullptr);
	PlaylistControls->addAction(Previous);
	auto PlayStop = new QAction("Play", nullptr);
	PlaylistControls->addAction(PlayStop);
	auto Next = new QAction("Next", nullptr);
	PlaylistControls->addAction(Next);
	RightLayout->addWidget(PlaylistControls);
	TopLayout->addLayout(RightLayout);

	MainWindow->setLayout(TopLayout);

	auto CrossThread = new QTCrossThread(MainWindow);

	QObject::connect(ChatEntry, &QLineEdit::returnPressed, [Core, ChatCursor, ChatEntry](void)
	{
		Core->Command(ChatEntry->text().fromUtf8().data());
		ChatCursor->insertText(ChatEntry->text());
		ChatEntry->setText("");
	});

	auto PositionUpdateTimer = new QTimer(Position);
	QObject::connect(PositionUpdateTimer, &QTimer::timeout, [Core, Position](void)
		{ Position->setValue(Core->GetPosition()); });

	QObject::connect(Position, &QSlider::sliderReleased, [Core, Position](void)
		{ Core->GoTo(Position->value()); });

	QObject::connect(Volume, &QSlider::sliderReleased, [Core, Volume](void)
		{ Core->SetVolume(Volume->value()); });

	QObject::connect(Add, &QAction::triggered, [Core, Add](bool)
	{
		auto Dialog = QFileDialog(Add, "Add media...", QDir::homepath(), "*.mp3;*.m4a;*.wav;*.ogg;*.wma;*.flv;*.flac;*.mid;*.mod;*.s3c;*.it");
		QObject::connect(Dialog, &QFileDialog::filesSelected, [Core](const QStringList &Selected)
		{ 
			for (auto File : Selected) 
			{
				QCryptographicHash Hash(QCryptographicHash::Md5);
				QFile OpenedFile(File);
				OpenedFile.open(QFile::ReadOnly);
				while (!OpenedFile.atEnd())
					Hash.addData(file.read(8192));
				QByteArray hash = Hash.result();
				Core->Add({hash.begin(), hash.end())}, File.toUtf8().data()); 
			}
		});
	});

	QObject::connect(Shuffle, &QAction::triggered, [Core](bool) { Core->Shuffle(); });
	QObject::connect(AlphaSort, &QAction::triggered, [Core](bool) { Core->Sort(); });
	QObject::connect(Previous, &QAction::triggered, [Core](bool) {});
	QObject::connect(PlayStop, &QAction::triggered, [Core](bool) {});
	QObject::connect(Next, &QAction::triggered, [Core](bool) {});

	MainWindow->show();
}

int main(int argc, char **argv)
{
	QApplication QTContext{argc, argv};
	QTContext.setQuitOnLastWindowClosed(true);

	OpenServerSelect();

	return QTContext.exec();

	return 0;
}
