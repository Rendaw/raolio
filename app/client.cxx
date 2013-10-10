#include "regex.h"
#include "clientcore.h"
#include "qtaux.h"
#include "error.h"

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
#include <QTimer>
#include <QDir>
#include <QFileDialog>
#include <QCryptographicHash>
#include <QSplitter>
#include <iomanip>
#include <memory>

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
					if (Index.column() == 1) return QString::fromUtf8("New server...");
				}
				else if (Index.row() < ServerHistory.Count() + 1)
				{
					if (Index.column() == 0) return QVariant(ServerHistory.Get(Index.row() - 1).Remember);
					if (Index.column() == 1) return QString::fromUtf8(ServerHistory.Get(Index.row() - 1).Summary().c_str());
				}
				else return QVariant();
			default:
				return QVariant();
		}
	}
} ServerHistoryQTModel;

void OpenPlayer(std::string const &Host, uint16_t Port);
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
		/*ServerInfo Info = ServerHistory.GetDefault();
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
		ServerHistory.Finish(Info);*/ // libstdc++ is broken, libc++ has probs with boost
		OpenPlayer("localhost", 20578);
		ConnectWindow->close();
	});

	QObject::connect(Quit, &QPushButton::clicked, [=](bool)
	{
		ConnectWindow->close();
	});

	RecentServers->setCurrentIndex(ServerHistoryQTModel.index(0, 0));
	ConnectWindow->show();
}

struct PlaylistQTModel_Unique : QAbstractTableModel
{

	PlaylistQTModel_Unique(void) : Index{0} {}
	QModelIndex index(int Row, int Column, QModelIndex const &Parent) const override { return createIndex(Row, Column); }
	QModelIndex parent(QModelIndex const &Index) const override { return QModelIndex(); }
	int rowCount(const QModelIndex &Parent) const override { return Playlist.size(); }
	int columnCount(const QModelIndex &Parent = QModelIndex()) const override { return 5; }
	QVariant data(const QModelIndex &Index, int Role) const override
	{
		if (Index.column() > columnCount(QModelIndex()) - 1) return QVariant();
		if (Index.column() < 0) return QVariant();
		if (Index.row() < 0) return QVariant();
		if (Index.row() > Playlist.size()) return QVariant();
		switch (Role)
		{
			case Qt::DisplayRole:
				switch (Index.column())
				{
					case 0: return QString::fromUtf8(FormatHash(Playlist[Index.row()].Hash).c_str());
					case 1:
						switch (Playlist[Index.row()].State)
						{
							case PlaylistInfo::Pause: return QString("=");
							case PlaylistInfo::Play: return QString(">");
							default: return QVariant();
						}
					case 2: return QString::fromUtf8(Playlist[Index.row()].Artist.c_str());
					case 3: return QString::fromUtf8(Playlist[Index.row()].Album.c_str());
					case 4: return QString::fromUtf8(Playlist[Index.row()].Title.c_str());
					default: return QVariant();
				}
			default:
				return QVariant();
		}
	}

	Optional<size_t> Find(HashType const &Hash)
	{
		for (size_t Index = 0; Index < Playlist.size(); ++Index) if (Playlist[Index].Hash == Hash) return Index;
		return {};
	}

	void AddUpdate(MediaItem *Item)
	{
		auto Found = Find(Item->Hash);
		if (!Found)
		{
			beginInsertRows(QModelIndex(), Playlist.size() - 1, Playlist.size() - 1);
			Playlist.emplace_back(Item->Hash, PlaylistInfo::Deselected, Item->Title, Item->Album, Item->Artist);
			endInsertRows();
			return;
		}
		else
		{
			Playlist[*Found].Title = Item->Title;
			Playlist[*Found].Album = Item->Album;
			Playlist[*Found].Artist = Item->Artist;
			dataChanged(createIndex(*Found, 0), createIndex(*Found, columnCount()));
		}
	}

	void Remove(HashType const &Hash)
	{
		auto Found = Find(Hash);
		if (!Found) return;
		beginRemoveRows(QModelIndex(), *Found, *Found);
		Playlist.erase(Playlist.begin() + *Found);
		endRemoveRows();
	}

	void Select(HashType const &Hash)
	{
		auto Found = Find(Hash);
		if (!Found) return;
		if (Index < Playlist.size())
		{
			Playlist[Index].State = PlaylistInfo::Deselected;
			dataChanged(createIndex(Index, 1), createIndex(Index, 1));
		}
		Index = *Found;
		Playlist[Index].State = PlaylistInfo::Pause;
		dataChanged(createIndex(Index, 1), createIndex(Index, 1));
	}

	bool IsPlaying(void)
	{
		if (Index >= Playlist.size()) return false;
		return Playlist[Index].State == PlaylistInfo::Play;
	}

	HashType GetID(int Row) const
	{
		assert(Row >= 0);
		assert(Row < Playlist.size());
		return Playlist[Row].Hash;
	}

	Optional<HashType> GetCurrentID(void) const
	{
		if (Index >= Playlist.size()) return {};
		else return Playlist[Index].Hash;
	}

	Optional<HashType> GetNextID(void) const
	{
		if (Index >= Playlist.size())
		{
			if (Playlist.empty()) return {};
			else return Playlist.front().Hash;
		}
		else
		{
			if (Index + 1 >= Playlist.size())
				return Playlist.front().Hash;
			return Playlist[Index + 1].Hash;
		}
	}

	Optional<HashType> GetPreviousID(void) const
	{
		if (Index >= Playlist.size())
		{
			if (Playlist.empty()) return {};
			else return Playlist.back().Hash;
		}
		else
		{
			if (Index == 0)
				return Playlist.back().Hash;
			return Playlist[Index - 1].Hash;
		}
	}

	void Play(void)
	{
		if (Index >= Playlist.size()) return;
		Playlist[Index].State = PlaylistInfo::Play;
		dataChanged(createIndex(Index, 1), createIndex(Index, 1));
	}

	void Stop(void)
	{
		if (Index >= Playlist.size()) return;
		Playlist[Index].State = PlaylistInfo::Play;
		dataChanged(createIndex(Index, 1), createIndex(Index, 1));
	}

	void Shuffle(void)
	{
		HashType CurrentID;
		if (Index >= Playlist.size()) CurrentID = Playlist[Index].Hash;
		std::random_shuffle(Playlist.begin(), Playlist.end());
		dataChanged(createIndex(0, 0), createIndex(Playlist.size(), columnCount()));
		auto Found = Find(CurrentID);
		if (Found) Index = *Found;
	}

	private:
		struct PlaylistInfo
		{
			HashType Hash;
			enum { Deselected, Pause, Play } State;
			std::string Title;
			std::string Album;
			std::string Artist;
			PlaylistInfo(HashType const &Hash, decltype(State) const &State, std::string const &Title, std::string const &Album, std::string const &Artist) : Hash(Hash), State{State}, Title{Title}, Album{Album}, Artist{Artist} {}
		};
		std::vector<PlaylistInfo> Playlist;
		size_t Index;
};

void OpenPlayer(std::string const &Host, uint16_t Port)
{
	try
	{
		auto MainWindow = new QWidget();
		MainWindow->setWindowTitle("Zarbomedia - Player");
		MainWindow->setAttribute(Qt::WA_DeleteOnClose, true);

		auto MainLayout = new QBoxLayout(QBoxLayout::TopToBottom);
		auto Splitter = new QSplitter();

		auto LeftWidget = new QWidget();
		auto LeftLayout = new QBoxLayout(QBoxLayout::TopToBottom);
		LeftLayout->setMargin(0);
		auto ChatDisplay = new QTextEdit();
		ChatDisplay->setReadOnly(true);
		auto ChatCursor = std::make_shared<QTextCursor>(ChatDisplay->document());
		LeftLayout->addWidget(ChatDisplay);
		auto ChatEntry = new QLineEdit();
		LeftLayout->addWidget(ChatEntry);
		LeftWidget->setLayout(LeftLayout);
		Splitter->addWidget(LeftWidget);

		auto RightWidget = new QWidget();
		auto RightLayout = new QBoxLayout(QBoxLayout::TopToBottom);
		RightLayout->setMargin(0);
		auto Playlist = new QTreeView();
		RightLayout->addWidget(Playlist);
		auto Position = new QSlider(Qt::Horizontal);
		Position->setRange(0, 10000);
		RightLayout->addWidget(Position);
		auto Volume = new QSlider(Qt::Horizontal);
		Volume->setRange(0, 10000);
		Volume->setValue(10000);
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
		RightWidget->setLayout(RightLayout);
		Splitter->addWidget(RightWidget);

		MainLayout->addWidget(Splitter);
		MainWindow->setLayout(MainLayout);

		auto CrossThread = new QTCrossThread(MainWindow);

		struct PlayerDataType
		{
			ClientCore Core;
			PlaylistQTModel_Unique PlaylistQTModel;
			struct
			{
				void Request(void) { Count = 0u; }
				void Ack(void) { Count = std::min(2u, Count + 1u); }
				bool InControl(void) { return Count == 1u; }
				void Maintain(void) { if (InControl()) Request(); }
				unsigned int Count = 2u;
			} Volition;
		};
		auto PlayerData = CreateQTStorage(MainWindow, make_unique<PlayerDataType>());
		auto Core = &PlayerData->Data->Core;
		auto PlaylistQTModel = &PlayerData->Data->PlaylistQTModel;
		auto Volition = &PlayerData->Data->Volition;

		Playlist->setModel(PlaylistQTModel);

		Core->LogCallback = [=](std::string const &Message)
		{
			CrossThread->Transfer([ChatDisplay, ChatCursor, Message](void)
			{
				ChatCursor->insertText(QString::fromUtf8((Message + "\n").c_str()));
				ChatDisplay->setTextCursor(*ChatCursor);
			});
		};
		Core->SeekCallback = [=](float Time) { CrossThread->Transfer([=](void) { if (!Position->isSliderDown()) Position->setValue(static_cast<int>(Time * 10000)); }); };
		Core->AddCallback = [=](MediaItem *Item) { CrossThread->Transfer([=](void) { PlaylistQTModel->AddUpdate(Item); }); };
		Core->UpdateCallback = [=](MediaItem *Item) { CrossThread->Transfer([=](void) { PlaylistQTModel->AddUpdate(Item); }); };
		Core->SelectCallback = [=](HashType const &MediaID)
			{ CrossThread->Transfer([=](void) { Volition->Ack(); PlaylistQTModel->Select(MediaID); }); };
		Core->PlayCallback = [=](void) { CrossThread->Transfer([=](void)
		{
			PlaylistQTModel->Play();
			PlayStop->setText("Pause");
		}); };
		Core->StopCallback = [=](void) { CrossThread->Transfer([=](void)
		{
			PlaylistQTModel->Stop();
			PlayStop->setText("Play");
		}); };
		Core->EndCallback = [=](void)
		{
			CrossThread->Transfer([=](void)
			{
				if (!Volition->InControl()) return;
				auto NextID = PlaylistQTModel->GetNextID();
				if (!NextID) return;
				Volition->Request();
				Core->Play(*NextID, 0ul);
			});
		};

		QObject::connect(ChatEntry, &QLineEdit::returnPressed, [=](void)
		{
			Core->Chat(ChatEntry->text().toUtf8().data());
			ChatCursor->insertText(ChatEntry->text() + "\n");
			ChatEntry->setText("");
			ChatDisplay->setTextCursor(*ChatCursor);
		});

		QObject::connect(Playlist, &QAbstractItemView::doubleClicked, [=](QModelIndex const &Index)
			{ Volition->Request(); Core->Play(PlaylistQTModel->GetID(Index.row()), 0ul); });

		auto PositionUpdateTimer = new QTimer(Position);
		QObject::connect(PositionUpdateTimer, &QTimer::timeout, [=](void)
			{ Core->GetTime(); });
		PositionUpdateTimer->start(1000);

		QObject::connect(Position, &QSlider::sliderReleased, [=](void)
		{
			auto CurrentID = PlaylistQTModel->GetCurrentID();
			if (!CurrentID) return;
			Volition->Maintain();
			Core->Play(*CurrentID, (float)Position->value() / 10000.0f);
		});

		QObject::connect(Volume, &QSlider::sliderReleased, [=](void)
			{ Core->SetVolume(Volume->value() / 10000.0f); });

		QObject::connect(Add, &QAction::triggered, [=](bool)
		{
			auto Dialog = new QFileDialog(MainWindow, "Add media...", QDir::homePath(),
				"All media (*.mp3 *.m4a *.wav *.ogg *.wma *.flv *.flac *.mid *.mod *.s3c *.it);; "
				"All files (*.*)");
			Dialog->setFileMode(QFileDialog::ExistingFiles);
			QObject::connect(Dialog, &QFileDialog::filesSelected, [=](const QStringList &Selected)
			{
				for (auto File : Selected)
				{
					auto Hash = HashFile(File.toUtf8().data());
					if (!Hash) continue; // TODO Warn?
					Core->Add(Hash->first, Hash->second, File.toUtf8().data());
				}
			});
			Dialog->show();
		});

		QObject::connect(Shuffle, &QAction::triggered, [=](bool)
			{ PlaylistQTModel->Shuffle(); });

		QObject::connect(Previous, &QAction::triggered, [=](bool)
		{
			auto PreviousID = PlaylistQTModel->GetPreviousID();
			if (!PreviousID) return;
			Volition->Request();
			Core->Play(*PreviousID, 0ul);
		});

		QObject::connect(PlayStop, &QAction::triggered, [=](bool)
		{
			if (PlaylistQTModel->IsPlaying())
			{
				Core->Stop();
				PlayStop->setText("Play");
			}
			else
			{
				Core->Play();
				PlayStop->setText("Pause");
			}
		});

		QObject::connect(Next, &QAction::triggered, [=](bool)
		{
			auto NextID = PlaylistQTModel->GetNextID();
			if (!NextID) return;
			Volition->Request();
			Core->Play(*NextID, 0ul);
		});

		MainWindow->show();

		Core->Open(false, Host, Port);
	}
	catch (ConstructionError const &Error)
	{
		QMessageBox::warning(0, "Error during startup", ((std::string)Error).c_str());
		OpenServerSelect();
	}
}

int main(int argc, char **argv)
{
	QApplication QTContext{argc, argv};
	QTContext.setQuitOnLastWindowClosed(true);

	OpenServerSelect();

	return QTContext.exec();

	return 0;
}
