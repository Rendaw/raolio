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
#include <QAbstractItemModel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QGroupBox>
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
#include <QSettings>
#include <QMimeData>

#include <iomanip>
#include <memory>

QSettings *Settings = nullptr;

// Settings strings
#define SCServers "Servers"
#define SCHandle "Handle"
#define SCHost "Host"
#define SCPort "Port"
#define SCRemember "Remember"
#define SCRememberAs "Remember as"
#define SCVolume "Volume"
#define SCSort "Custom sort"
#define SCSortID "ID"
#define SCSortReverse "Reverse"
#define SCColumns "Columns"
#define SCColumnValue "Value"

struct ServerInfo
{
	ServerInfo(std::string const &Handle, std::string const &Host, uint16_t Port, bool Remember, std::string const &RememberAs) : Handle{Handle}, Host{Host}, Port{Port}, Remember{Remember}, RememberAs{RememberAs} {}

	std::string Handle;

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

struct ServerHistory : QAbstractItemModel
{
	ServerHistory(void)
	{
		int const Count = Settings->beginReadArray(SCServers);
		for (int Index = 0; Index < Count; ++Index)
		{
			Settings->setArrayIndex(Index);
			Items.emplace_back(
				Settings->value(SCHandle, QString::fromUtf8("Ghost")).toString().toUtf8().data(),
				Settings->value(SCHost, QString::fromUtf8("localhost")).toString().toUtf8().data(),
				Settings->value(SCPort, 20578).toInt(),
				Settings->value(SCRemember, false).toBool(),
				Settings->value(SCRememberAs, QString()).toString().toUtf8().data());
		}
		Settings->endArray();
	}

	size_t Count(void) const { return Items.size(); }
	ServerInfo const &Get(size_t Index) const { assert(Index < Items.size()); return Items[Index]; }
	ServerInfo GetDefault(void) const
	{
		if (Items.empty()) return {"Dog", "localhost", 20578, false, ""};
		return Items[0];
	}

	void RememberHistory(size_t Index, std::string const &Text)
	{
		assert(Index < Items.size());
		Items[Index].Remember = true;
		Items[Index].RememberAs = Text;
		dataChanged(index(Index, 0), index(Index, columnCount(QModelIndex())));
	}
	void ForgetHistory(size_t Index)
	{
		assert(Index < Items.size());
		Items[Index].Remember = false;
		Items[Index].RememberAs = std::string();
		dataChanged(index(Index, 0), index(Index, columnCount(QModelIndex())));
	}

	void Finish(ServerInfo const &NewItem)
	{
		std::vector<ServerInfo> NewItems;
		NewItems.reserve(Items.size() + 1);

		// Keep all checked items verbatim
		if (NewItem.Remember) NewItems.push_back(NewItem);
		for (auto const &Item : Items) if (Item.Remember) NewItems.push_back(Item);

		// Keep up to 20 unique unchecked items
		size_t UnrememberedCount = 0;
		if (!NewItem.Remember) { NewItems.push_back(NewItem); ++UnrememberedCount; }
		for (auto const &Item : Items)
		{
			if (UnrememberedCount > 20) break;
			if (Item.Remember) continue;
			if ((Item.Host == NewItem.Host) &&
				(Item.Port == NewItem.Port))
				continue;
			NewItems.push_back(Item);
			++UnrememberedCount;
		}
		Items.swap(NewItems);

		// Save
		Settings->beginWriteArray(SCServers);
		int Index = 0;
		for (auto &Item : Items)
		{
			Settings->setArrayIndex(Index++);
			Settings->setValue(SCHandle, QString::fromUtf8(Item.Handle.c_str()));
			Settings->setValue(SCHost, QString::fromUtf8(Item.Host.c_str()));
			Settings->setValue(SCPort, Item.Port);
			Settings->setValue(SCRemember, Item.Remember);
			Settings->setValue(SCRememberAs, QString::fromUtf8(Item.RememberAs.c_str()));
		}
		Settings->endArray();
	}

	std::vector<ServerInfo> Items;

	// Qt item model
	QModelIndex index(int Row, int Column, QModelIndex const &Parent = QModelIndex()) const override { return createIndex(Row, Column); }
	QModelIndex parent(QModelIndex const &Index) const override { return QModelIndex(); }
	int rowCount(const QModelIndex &Parent) const { if (Parent.isValid()) return 0; return Count() + 1; }
	int columnCount(const QModelIndex &Parent) const { return 2; }
	QVariant data(const QModelIndex &Index, int Role) const
	{
		if (Index.parent().isValid()) return QVariant();
		if (Index.column() > 1) return QVariant();
		if (Index.column() < 0) return QVariant();
		if (Index.row() < 0) return QVariant();
		if (Index.row() >= Count()) return QVariant();
		switch (Role)
		{
			case Qt::DisplayRole:
				if (Index.column() == 0) return QVariant(Get(Index.row()).Remember);
				if (Index.column() == 1) return QString::fromUtf8(Get(Index.row()).Summary().c_str());
			default:
				return QVariant();
		}
	}
};

void OpenPlayer(std::string const &Handle, std::string const &Host, uint16_t Port);
void OpenServerSelect(void)
{
	auto ConnectWindow = new QDialog();
	ConnectWindow->setWindowTitle("ICBM - Select Server");
	ConnectWindow->setAttribute(Qt::WA_DeleteOnClose, true);

	auto OpenServerData = CreateQTStorage(ConnectWindow, make_unique<ServerHistory>());
	auto ServerHistory = OpenServerData->Data.get();

	auto ConnectLayout = new QBoxLayout(QBoxLayout::TopToBottom);

	auto NameLayout = new QBoxLayout(QBoxLayout::LeftToRight);
	auto NameLabel = new QLabel("Handle");
	NameLayout->addWidget(NameLabel);
	auto Name = new QLineEdit(QString::fromUtf8(ServerHistory->GetDefault().Handle.c_str()));
	NameLayout->addWidget(Name);
	ConnectLayout->addLayout(NameLayout);

	auto ConnectEntryLayout = new QBoxLayout(QBoxLayout::LeftToRight);
	auto ServerLabel = new QLabel("Server");
	ConnectEntryLayout->addWidget(ServerLabel);
	auto Server = new QLineEdit(QString::fromUtf8(ServerHistory->GetDefault().URI().c_str()));
	ConnectEntryLayout->addWidget(Server);
	ConnectLayout->addLayout(ConnectEntryLayout);

	auto RememberEntryLayout = new QBoxLayout(QBoxLayout::LeftToRight);
	auto Remember = new QCheckBox("Remember as");
	Remember->setChecked(false);
	RememberEntryLayout->addWidget(Remember);
	auto RememberName = new QLineEdit();
	RememberName->setEnabled(false);
	RememberEntryLayout->addWidget(RememberName);
	ConnectLayout->addLayout(RememberEntryLayout);

	auto HistoryFrame = new QGroupBox("History");
	HistoryFrame->setFlat(true);
	auto HistoryLayout = new QBoxLayout(QBoxLayout::TopToBottom);

	auto RecentServers = new QTreeView;
	RecentServers->header()->hide();
	RecentServers->setModel(ServerHistory);
	RecentServers->setSelectionMode(QAbstractItemView::SingleSelection);
	HistoryLayout->addWidget(RecentServers);

	auto HistoricRememberEntryLayout = new QBoxLayout(QBoxLayout::LeftToRight);
	auto HistoricRemember = new QLineEdit();
	HistoricRememberEntryLayout->addWidget(HistoricRemember);
	auto ToggleHistoricRemember = new QPushButton();
	HistoricRememberEntryLayout->addWidget(ToggleHistoricRemember);
	HistoryLayout->addLayout(HistoricRememberEntryLayout);

	HistoryFrame->setLayout(HistoryLayout);
	ConnectLayout->addWidget(HistoryFrame);

	auto ActionLayout = new QBoxLayout(QBoxLayout::RightToLeft);
	auto Connect = new QPushButton("Connect");
	ActionLayout->addWidget(Connect);
	Connect->setDefault(true);
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

		auto Info = ServerHistory->Get(Index);

		Name->setText(QString::fromUtf8(Info.Handle.c_str()));
		Server->setText(QString::fromUtf8(Info.URI().c_str()));

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
		if (ServerHistory->Get(Index).Remember)
			ServerHistory->ForgetHistory(Index);
		else ServerHistory->RememberHistory(Index, HistoricRemember->text().toUtf8().data());
		Q_EMIT(ServerHistory->index(Index, 0), ServerHistory->index(Index, 0));
		SetHistoricRemember(ServerHistory->Get(Index));
	});

	QObject::connect(Connect, &QPushButton::clicked, [=](bool)
	{
		ServerInfo Info = ServerHistory->GetDefault();
		Info.Handle = Name->text().toUtf8().data();
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
		ServerHistory->Finish(Info); // libstdc++ is broken, libc++ has probs with boost
		OpenPlayer(Info.Handle, Info.Host, Info.Port);
	});

	QObject::connect(Quit, &QPushButton::clicked, [=](bool)
	{
		ConnectWindow->close();
	});

	RecentServers->setCurrentIndex(ServerHistory->index(0, 0));
	ConnectWindow->show();
}

enum class PlaylistColumns
{
	Track,
	Artist,
	Album,
	Title
};

struct PlaylistType : QAbstractItemModel
{
	PlaylistType(void) : Index{}
	{
		int const ColumnRows = Settings->beginReadArray(SCColumns);
		if (ColumnRows == 0)
			Columns = {PlaylistColumns::Track, PlaylistColumns::Artist, PlaylistColumns::Album, PlaylistColumns::Title};
		else for (int Row = 0; Row < ColumnRows; ++Row)
		{
			Settings->setArrayIndex(Row);
			Columns.emplace_back(static_cast<PlaylistColumns>(Settings->value(SCColumnValue).toInt()));
		}
		Settings->endArray();
	}

	std::function<void(void)> SignalUnsorted;

	std::vector<PlaylistColumns> const &GetColumns(void) const { return Columns; }

	void SetColumns(std::vector<PlaylistColumns> &&NewColumns)
	{
		beginRemoveColumns(QModelIndex(), 0, Columns.size() - 1);
		endRemoveColumns();
		beginInsertColumns(QModelIndex(), 0, NewColumns.size());
		Columns.swap(NewColumns);
		endInsertColumns();

		Settings->beginWriteArray(SCColumns);
		int Index = 0;
		for (auto &Column : Columns)
		{
			Settings->setArrayIndex(Index++);
			Settings->setValue(SCColumnValue, static_cast<int>(Column));
		}
		Settings->endArray();
	}

	Optional<size_t> Find(HashType const &Hash)
	{
		for (size_t Index = 0; Index < Playlist.size(); ++Index) if (Playlist[Index].Hash == Hash) return Index;
		return {};
	}

	void AddUpdate(MediaInfo const &Item)
	{
		auto Found = Find(Item.Hash);
		if (!Found)
		{
			beginInsertRows(QModelIndex(), Playlist.size() - 1, Playlist.size() - 1);
			Playlist.emplace_back(Item.Hash, PlaylistInfo::Deselected, Item.Track, Item.Title, Item.Album, Item.Artist);
			endInsertRows();
			if (SignalUnsorted) SignalUnsorted();
		}
		else
		{
			Playlist[*Found].Track = Item.Track;
			Playlist[*Found].Title = Item.Title;
			Playlist[*Found].Album = Item.Album;
			Playlist[*Found].Artist = Item.Artist;
			dataChanged(createIndex(*Found, 0), createIndex(*Found, columnCount()));
		}
	}

	void Remove(HashType const &Hash)
	{
		auto Found = Find(Hash);
		if (!Found) return;
		if (Index && (*Found == *Index)) Index = {};
		beginRemoveRows(QModelIndex(), *Found, *Found);
		Playlist.erase(Playlist.begin() + *Found);
		endRemoveRows();
	}

	void Select(HashType const &Hash)
	{
		auto Found = Find(Hash);
		if (!Found) return;
		if (Index)
		{
			Playlist[*Index].State = PlaylistInfo::Deselected;
			dataChanged(createIndex(*Index, 1), createIndex(*Index, 1));
		}
		Index = *Found;
		Playlist[*Index].State = PlaylistInfo::Pause;
		dataChanged(createIndex(*Index, 1), createIndex(*Index, 1));
	}

	Optional<bool> IsPlaying(void)
	{
		if (!Index) return {};
		return Playlist[*Index].State == PlaylistInfo::Play;
	}

	HashType GetID(int Row) const
	{
		assert(Row >= 0);
		assert(Row < Playlist.size());
		return Playlist[Row].Hash;
	}

	Optional<HashType> GetCurrentID(void) const
	{
		if (!Index) return {};
		return Playlist[*Index].Hash;
	}

	Optional<HashType> GetNextID(void) const
	{
		if (!Index)
		{
			if (Playlist.empty()) return {};
			else return Playlist.front().Hash;
		}
		else
		{
			if (*Index + 1 >= Playlist.size())
				return Playlist.front().Hash;
			return Playlist[*Index + 1].Hash;
		}
	}

	Optional<HashType> GetPreviousID(void) const
	{
		if (!Index)
		{
			if (Playlist.empty()) return {};
			else return Playlist.back().Hash;
		}
		else
		{
			if (*Index == 0)
				return Playlist.back().Hash;
			return Playlist[*Index - 1].Hash;
		}
	}

	void Play(void)
	{
		if (!Index) return;
		Playlist[*Index].State = PlaylistInfo::Play;
		dataChanged(createIndex(*Index, 1), createIndex(*Index, 1));
	}

	void Stop(void)
	{
		if (!Index) return;
		Playlist[*Index].State = PlaylistInfo::Play;
		dataChanged(createIndex(*Index, 1), createIndex(*Index, 1));
	}

	void Shuffle(void)
	{
		HashType CurrentID;
		if (Index)
		{
			assert(*Index < Playlist.size());
			CurrentID = Playlist[*Index].Hash;
		}
		layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
		std::random_shuffle(Playlist.begin(), Playlist.end());
		layoutChanged({}, QAbstractItemModel::VerticalSortHint);
		if (SignalUnsorted) SignalUnsorted();
		auto Found = Find(CurrentID);
		if (Found) Index = *Found;
	}

	struct SortFactor
	{
		PlaylistColumns Column;
		bool Reverse;
		SortFactor(PlaylistColumns const Column, bool const Reverse) : Column{Column}, Reverse{Reverse} {}
	};
	void Sort(std::list<SortFactor> const &Factors)
	{
		HashType CurrentID;
		if (Index)
		{
			assert(*Index < Playlist.size());
			CurrentID = Playlist[*Index].Hash;
		}
		layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
		std::stable_sort(Playlist.begin(), Playlist.end(), [&Factors](PlaylistInfo const &First, PlaylistInfo const &Second)
		{
			for (auto &Factor : Factors)
			{
				auto const Fix = [&Factor](bool const Verdict) { if (Factor.Reverse) return !Verdict; return Verdict; };
				switch (Factor.Column)
				{
					case PlaylistColumns::Track:
						if (First.Track == Second.Track) continue;
						if (!First.Track) return Fix(true);
						if (!Second.Track) return Fix(false);
						return Fix(*First.Track < *Second.Track);
					case PlaylistColumns::Title:
						if (First.Title == Second.Title) continue;
						return Fix(First.Title < Second.Title);
					case PlaylistColumns::Album:
						if (First.Album == Second.Album) continue;
						return Fix(First.Album < Second.Album);
					case PlaylistColumns::Artist:
						if (First.Artist == Second.Artist) continue;
						return Fix(First.Artist < Second.Artist);
					default: assert(false); continue;
				}
			}
			return false;
		});
		layoutChanged({}, QAbstractItemModel::VerticalSortHint);
		if (SignalUnsorted) SignalUnsorted();
		auto Found = Find(CurrentID);
		if (Found) Index = *Found;
	}

	QModelIndex index(int Row, int Column, QModelIndex const &Parent) const override { return createIndex(Row, Column); }
	QModelIndex parent(QModelIndex const &Index) const override { return QModelIndex(); }
	int rowCount(const QModelIndex &Parent) const override
	{
		if (Parent.isValid()) return 0;
		if (Parent.parent().isValid()) return 0;
		return Playlist.size();
	}
	int columnCount(const QModelIndex &Parent = QModelIndex()) const override { return 1 + Columns.size(); }

	QVariant headerData(int Section, Qt::Orientation Orientation, int Role) const override
	{
		if (Orientation != Qt::Horizontal) return QVariant();
		if (Section < 0) return QVariant();
		if (Section >= columnCount()) return QVariant();
		switch (Role)
		{
			case Qt::DisplayRole:
			{
				if (Section == 0) return QVariant();
				switch (Columns[Section - 1])
				{
					case PlaylistColumns::Track: return QString("#");
					case PlaylistColumns::Artist: return QString("Artist");
					case PlaylistColumns::Album: return QString("Album");
					case PlaylistColumns::Title: return QString("Title");
					default: assert(false); return QVariant();
				}
			}
			default: return QVariant();
		}
	}

	QVariant data(const QModelIndex &Index, int Role) const override
	{
		if (!Index.isValid()) return QVariant();
		if (Index.column() > columnCount(QModelIndex()) - 1) return QVariant();
		if (Index.column() < 0) return QVariant();
		if (Index.row() < 0) return QVariant();
		if (Index.row() > Playlist.size()) return QVariant();
		if (Index.parent().isValid()) return QVariant();
		switch (Role)
		{
			case Qt::DisplayRole:
				if (Index.column() == 0)
					switch (Playlist[Index.row()].State)
					{
						case PlaylistInfo::Pause: return QString("=");
						case PlaylistInfo::Play: return QString(">");
						default: return QVariant();
					}
				switch (Columns[Index.column() - 1])
				{
					case PlaylistColumns::Track:
					{
						auto &Track = Playlist[Index.row()].Track;
						if (!Track) return QVariant();
						return QString::fromUtf8((String() << *Track).str().c_str());
					}
					case PlaylistColumns::Artist: return QString::fromUtf8(Playlist[Index.row()].Artist.c_str());
					case PlaylistColumns::Album: return QString::fromUtf8(Playlist[Index.row()].Album.c_str());
					case PlaylistColumns::Title: return QString::fromUtf8(Playlist[Index.row()].Title.c_str());
					default: assert(false); return QVariant();
				}
			default:
				return QVariant();
		}
	}

	Qt::ItemFlags flags(const QModelIndex &Index) const override
	{
		if (!Index.isValid()) return Qt::ItemIsEnabled | Qt::ItemIsDropEnabled;
		if (Index.column() > columnCount(QModelIndex()) - 1) return 0;
		if (Index.column() < 0) return 0;
		if (Index.row() < 0) return 0;
		if (Index.row() > Playlist.size()) return 0;
		if (Index.parent().isValid()) return 0;
		return Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsEnabled;
	}

	Qt::DropActions supportedDropActions(void) const override
		{ return Qt::MoveAction; }

	void sort(int Column, Qt::SortOrder Order) override
	{
		HashType CurrentID;
		if (Index)
		{
			assert(*Index < Playlist.size());
			CurrentID = Playlist[*Index].Hash;
		}
		if (Column < 0) return;
		if (Column == 0) return;
		if (Column >= columnCount({}) + 1) return;
		layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
		switch (Columns[Column - 1])
		{
			case PlaylistColumns::Track:
				std::sort(Playlist.begin(), Playlist.end(), [](PlaylistInfo const &Left, PlaylistInfo const &Right)
					{ return Left.Track < Right.Track; });
				break;
			case PlaylistColumns::Artist:
				std::sort(Playlist.begin(), Playlist.end(), [](PlaylistInfo const &Left, PlaylistInfo const &Right)
					{ return Left.Artist < Right.Artist; });
				break;
			case PlaylistColumns::Album:
				std::sort(Playlist.begin(), Playlist.end(), [](PlaylistInfo const &Left, PlaylistInfo const &Right)
					{ return Left.Album < Right.Album; });
				break;
			case PlaylistColumns::Title:
				std::sort(Playlist.begin(), Playlist.end(), [](PlaylistInfo const &Left, PlaylistInfo const &Right)
					{ return Left.Title < Right.Title; });
				break;
			default: assert(false); break;
		}
		if (Order == Qt::DescendingOrder)
			std::reverse(Playlist.begin(), Playlist.end());
		if (Index)
		{
			auto Found = Find(CurrentID);
			if (Found) Index = *Found;
		}
		layoutChanged({}, QAbstractItemModel::VerticalSortHint);
	}

	QStringList mimeTypes(void) const override
	{
		QStringList Types;
		Types << "application/qtsucks";
		return Types;
	}

	QMimeData *mimeData(const QModelIndexList &Indices) const override
	{
		QMimeData *MIMEData = new QMimeData();
		QByteArray EncodedData;
		QDataStream Stream(&EncodedData, QIODevice::WriteOnly);
		std::set<int> RecordedRows; // QT, being totally awesome, gives the same multiple times
		for (auto const &Index : Indices)
			if (Index.isValid() && RecordedRows.insert(Index.row()).second)
				Stream << Index.row();
		MIMEData->setData("application/qtsucks", EncodedData);
		return MIMEData;
	}

	bool dropMimeData(const QMimeData *Data, Qt::DropAction Action, int ToIndex, int, const QModelIndex &) override
	{
		if (Action == Qt::IgnoreAction) return true;
		assert(Action == Qt::MoveAction);
		if (Action != Qt::MoveAction) return false;
		if (!Data->hasFormat("application/qtsucks")) return false;
		assert(!Playlist.empty());

		QByteArray EncodedData = Data->data("application/qtsucks");
		QDataStream Stream(&EncodedData, QIODevice::ReadOnly);
		std::vector<int> Moves;
		while (!Stream.atEnd())
		{
			int OriginalIndex = -1;
			Stream >> OriginalIndex;
			assert(OriginalIndex >= 0);
			if (OriginalIndex < 0) continue;
			assert(OriginalIndex < Playlist.size());
			if (OriginalIndex >= Playlist.size()) return false;
			Moves.push_back(OriginalIndex);
		}
		if (Moves.empty()) return true;

		std::vector<PlaylistInfo> New;
		New.reserve(Playlist.size());

		for (int Index = 0; Index < Playlist.size(); ++Index)
		{
			if (Index == ToIndex)
				for (auto const &Move : Moves)
					New.push_back(Playlist[Move]);
			bool Skip = false;
			for (auto const &Move : Moves) if (Index == Move) { Skip = true; break; }
			if (Skip) continue;
			New.push_back(Playlist[Index]);
		}
		if ((ToIndex == -1) || (ToIndex == Playlist.size()))
			for (auto const &Move : Moves)
				New.push_back(Playlist[Move]);

		Assert(New.size(), Playlist.size());
		layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
		Playlist.swap(New);
		layoutChanged({}, QAbstractItemModel::VerticalSortHint);

		if (SignalUnsorted) SignalUnsorted();

		return true;
	}

	private:
		std::vector<PlaylistColumns> Columns;
		struct PlaylistInfo
		{
			HashType Hash;
			enum { Deselected, Pause, Play } State;
			Optional<uint16_t> Track;
			std::string Title;
			std::string Album;
			std::string Artist;
			PlaylistInfo(HashType const &Hash, decltype(State) const &State, Optional<uint16_t> const &Track, std::string const &Title, std::string const &Album, std::string const &Artist) : Hash(Hash), State{State}, Track{Track}, Title{Title}, Album{Album}, Artist{Artist} {}
		};
		std::vector<PlaylistInfo> Playlist;
		Optional<size_t> Index;
};

void OpenPlayer(std::string const &Handle, std::string const &Host, uint16_t Port)
{
	try
	{
		auto MainWindow = new QWidget();
		MainWindow->setWindowTitle("ICBM");
		MainWindow->setAttribute(Qt::WA_DeleteOnClose, true);

		float InitialVolume = Settings->value(SCVolume, 0.75f).toFloat();

		auto CrossThread = new QTCrossThread(MainWindow);

		struct PlayerDataType
		{
			PlayerDataType(std::string const &Handle, float Volume) : Core{Handle, Volume} {}
			ClientCore Core;
			PlaylistType Playlist;
			struct
			{
				void Request(void) { Count = 0u; }
				void Ack(void) { Count = std::min(2u, Count + 1u); }
				bool InControl(void) { return Count == 1u; }
				void Maintain(void) { if (InControl()) Request(); }
				unsigned int Count = 2u;
			} Volition;
		};
		auto PlayerData = CreateQTStorage(MainWindow, make_unique<PlayerDataType>(Handle, InitialVolume));
		auto Core = &PlayerData->Data->Core;
		auto Playlist = &PlayerData->Data->Playlist;
		auto Volition = &PlayerData->Data->Volition;

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
		auto PlaylistView = new QTreeView();
		PlaylistView->setSortingEnabled(true);
		PlaylistView->setModel(Playlist);
		auto ConfigureColumns = new QAction("Select columns...", nullptr);
		PlaylistView->header()->addAction(ConfigureColumns);
		PlaylistView->header()->setContextMenuPolicy(Qt::ActionsContextMenu);
		PlaylistView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
		PlaylistView->header()->setStretchLastSection(true);
		PlaylistView->setDragEnabled(true);
		PlaylistView->setAcceptDrops(true);
		PlaylistView->setDropIndicatorShown(true);
		RightLayout->addWidget(PlaylistView);
		auto Position = new QSlider(Qt::Horizontal);
		Position->setRange(0, 10000);
		RightLayout->addWidget(Position);
		auto Volume = new QSlider(Qt::Horizontal);
		Volume->setRange(0, 10000);
		Volume->setValue(10000 * InitialVolume);
		RightLayout->addWidget(Volume);
		auto PlaylistControls = new QToolBar;
		auto Add = new QAction("Add", nullptr);
		PlaylistControls->addAction(Add);
		auto OrderMenu = new QMenu("Order");
		auto Shuffle = new QAction("Shuffle", nullptr);
		OrderMenu->addAction(Shuffle);
		auto AdvancedOrder = new QAction("Advanced...", nullptr);
		OrderMenu->addAction(AdvancedOrder);
		PlaylistControls->addAction(OrderMenu->menuAction());
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

		Playlist->SignalUnsorted = [PlaylistView](void)
			{ PlaylistView->sortByColumn(-1, Qt::AscendingOrder); };

		auto PositionUpdateTimer = new QTimer(Position);
		QObject::connect(PositionUpdateTimer, &QTimer::timeout, [=](void)
			{ Core->GetTime(); });
		PositionUpdateTimer->start(1000);

		Core->LogCallback = [=](std::string const &Message)
		{
			CrossThread->Transfer([ChatDisplay, ChatCursor, Message](void)
			{
				ChatCursor->insertText(QString::fromUtf8((Message + "\n").c_str()));
				ChatDisplay->setTextCursor(*ChatCursor);
			});
		};
		Core->SeekCallback = [=](float Time) { CrossThread->Transfer([=](void)
			{ if (!Position->isSliderDown()) Position->setValue(static_cast<int>(Time * 10000)); }); };
		Core->AddCallback = [=](MediaInfo Item) { CrossThread->Transfer([=](void) { Playlist->AddUpdate(Item); }); };
		Core->UpdateCallback = [=](MediaInfo Item) { CrossThread->Transfer([=](void) { Playlist->AddUpdate(Item); }); };
		Core->SelectCallback = [=](HashType const &MediaID)
			{ CrossThread->Transfer([=](void) { Volition->Ack(); Playlist->Select(MediaID); }); };
		Core->PlayCallback = [=](void) { CrossThread->Transfer([=](void)
		{
			Playlist->Play();
			PlayStop->setText("Pause");
		}); };
		Core->StopCallback = [=](void) { CrossThread->Transfer([=](void)
		{
			Playlist->Stop();
			PlayStop->setText("Play");
		}); };
		Core->EndCallback = [=](void)
		{
			CrossThread->Transfer([=](void)
			{
				if (!Volition->InControl()) return;
				auto NextID = Playlist->GetNextID();
				if (!NextID) return;
				Volition->Request();
				Core->Play(*NextID, 0ul);
			});
		};

		QObject::connect(ChatEntry, &QLineEdit::returnPressed, [=](void)
		{
			Core->Chat(ChatEntry->text().toUtf8().data());
			ChatEntry->setText("");
		});

		QObject::connect(ConfigureColumns, &QAction::triggered, [=](bool)
		{
			auto Dialog = new QDialog{MainWindow};
			Dialog->setWindowTitle("ICBM - Select columns");

			auto ColumnsLayout = new QBoxLayout(QBoxLayout::TopToBottom);

			auto Label = new QLabel{"Column order:"};
			ColumnsLayout->addWidget(Label);

			auto Table = new QTreeWidget;
			Table->setHeaderHidden(true);
			Table->setDragEnabled(true);
			Table->setAcceptDrops(true);
			Table->setDropIndicatorShown(true);
			Table->setDragDropMode(QAbstractItemView::InternalMove);

			{
				auto const CreateRow = [=](PlaylistColumns const Column, bool const Checked)
				{
					auto Row = new QTreeWidgetItem;
					Row->setFlags(Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled);
					Row->setCheckState(0, Checked ? Qt::Checked : Qt::Unchecked);
					auto Data = "";
					switch (Column)
					{
						case PlaylistColumns::Track: Data = "Track number"; break;
						case PlaylistColumns::Album: Data = "Album"; break;
						case PlaylistColumns::Artist: Data = "Artist"; break;
						case PlaylistColumns::Title: Data = "Title"; break;
						default: assert(false); break;
					}
					Row->setData(0, Qt::DisplayRole, QString::fromUtf8(Data));
					Row->setData(0, Qt::UserRole + 1, QVariant(static_cast<int>(Column)));
					Table->addTopLevelItem(Row);
				};

				// Restore saved sort factors
				for (auto Column : Playlist->GetColumns())
					CreateRow(Column, true);

				// Create default/unsaved sort factors
				auto Root = Table->invisibleRootItem();
				for (auto Column :
				{
					PlaylistColumns::Track,
					PlaylistColumns::Album,
					PlaylistColumns::Artist,
					PlaylistColumns::Title
				})
				{
					bool Found = false;
					for (int ChildIndex = 0; ChildIndex < Root->childCount(); ++ChildIndex)
					{
						auto Child = Root->child(ChildIndex);
						if (static_cast<PlaylistColumns>(Child->data(0, Qt::UserRole + 1).toInt()) == Column)
						{
							Found = true;
							break;
						}
					}
					if (Found) continue;
					CreateRow(Column, false);
				}
			}
			ColumnsLayout->addWidget(Table);

			auto ActionLayout = new QBoxLayout(QBoxLayout::RightToLeft);
			auto Save = new QPushButton("Save");
			ActionLayout->addWidget(Save);
			Save->setDefault(true);
			auto Cancel = new QPushButton("Cancel");
			ActionLayout->addWidget(Cancel);
			ActionLayout->addStretch();
			ColumnsLayout->addLayout(ActionLayout);

			Dialog->setLayout(ColumnsLayout);

			QObject::connect(Save, &QPushButton::clicked, [=](bool)
			{
				std::vector<PlaylistColumns> Columns;
				auto Root = Table->invisibleRootItem();
				for (int ChildIndex = 0; ChildIndex < Root->childCount(); ++ChildIndex)
				{
					auto Child = Root->child(ChildIndex);
					if (Child->checkState(0) == Qt::Checked)
						Columns.emplace_back(static_cast<PlaylistColumns>(Child->data(0, Qt::UserRole + 1).toInt()));
				}

				Playlist->SetColumns(std::move(Columns));
				Dialog->done(0);
			});

			QObject::connect(Cancel, &QPushButton::clicked, [=](bool)
				{ Dialog->done(0); });

			Dialog->show();
		});

		QObject::connect(PlaylistView, &QAbstractItemView::doubleClicked, [=](QModelIndex const &Index)
			{ Volition->Request(); Core->Play(Playlist->GetID(Index.row()), 0ul); });

		QObject::connect(Position, &QSlider::sliderReleased, [=](void)
		{
			auto CurrentID = Playlist->GetCurrentID();
			if (!CurrentID) return;
			Volition->Maintain();
			Core->Play(*CurrentID, (float)Position->value() / 10000.0f);
		});

		QObject::connect(Volume, &QSlider::sliderReleased, [=](void)
		{
			float Percent = std::max(0.0f, std::min(1.0f, Volume->value() / 10000.0f));
			Core->SetVolume(Percent);
			Settings->setValue(SCVolume, Percent);
		});

		QObject::connect(Add, &QAction::triggered, [=](bool)
		{
			auto Dialog = new QFileDialog(MainWindow, "Add media...", QDir::homePath(),
				"All media (*.mp3 *.m4a *.wav *.ogg *.wma *.flv *.flac *.mid *.mod *.s3c *.it);; "
				"All files (*)");
			Dialog->setFileMode(QFileDialog::ExistingFiles);
			auto AlreadySelected = std::make_shared<bool>(false);
			QObject::connect(Dialog, &QFileDialog::filesSelected, [=](const QStringList &Selected) mutable
			{
				if (*AlreadySelected)
				{
					  // I'm pretty sure this is a Qt issue
					  std::cout << "filesSelected double called." << std::endl;
					  return;
				}
				for (auto File : Selected)
				{
					auto Hash = HashFile(File.toUtf8().data());
					if (!Hash) continue; // TODO Warn?
					Core->Add(Hash->first, Hash->second, File.toUtf8().data());
				}
				*AlreadySelected = true;
			});
			Dialog->show();
		});

		QObject::connect(Shuffle, &QAction::triggered, [=](bool)
			{ Playlist->Shuffle(); });

		QObject::connect(AdvancedOrder, &QAction::triggered, [=](bool)
		{
			auto Dialog = new QDialog{MainWindow};
			Dialog->setWindowTitle("ICBM - Custom sort...");

			auto SortLayout = new QBoxLayout(QBoxLayout::TopToBottom);

			auto Label = new QLabel{"Sort priority:"};
			SortLayout->addWidget(Label);

			auto Table = new QTreeWidget;
			Table->setHeaderHidden(true);
			Table->setDragEnabled(true);
			Table->setAcceptDrops(true);
			Table->setDropIndicatorShown(true);
			Table->setDragDropMode(QAbstractItemView::InternalMove);

			{
				auto const CreateRow = [=](PlaylistColumns const Column, bool const Reverse, bool const Checked)
				{
					auto Row = new QTreeWidgetItem;
					Row->setFlags(Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled);
					Row->setCheckState(0, Checked ? Qt::Checked : Qt::Unchecked);
					auto Data = "";
					switch (Column)
					{
						case PlaylistColumns::Track:
							if (!Reverse) Data = "Track number, ascending";
							else Data = "Track number, descending";
							break;
						case PlaylistColumns::Album:
							if (!Reverse) Data = "Album, ascending";
							else Data = "Album, descending";
							break;
						case PlaylistColumns::Artist:
							if (!Reverse) Data = "Artist, ascending";
							else Data = "Artist, descending";
							break;
						case PlaylistColumns::Title:
							if (!Reverse) Data = "Title, ascending";
							else Data = "Title, descending";
							break;
						default: assert(false); break;
					}
					Row->setData(0, Qt::DisplayRole, QString::fromUtf8(Data));
					Row->setData(0, Qt::UserRole + 1, QVariant(static_cast<int>(Column)));
					Row->setData(0, Qt::UserRole + 2, QVariant(Reverse));
					Table->addTopLevelItem(Row);
				};

				// Restore saved sort factors
				int const SavedFactorCount = Settings->beginReadArray(SCSort);
				for (int Index = 0; Index < SavedFactorCount; ++Index)
				{
					Settings->setArrayIndex(Index);
					CreateRow(
						static_cast<PlaylistColumns>(Settings->value(SCSortID).toInt()),
						Settings->value(SCSortReverse).toBool(),
						true);
				}
				Settings->endArray();

				// Create default/unsaved sort factors
				auto Root = Table->invisibleRootItem();
				struct Tuple
				{
					PlaylistColumns ID;
					bool Reverse;
				}; // tuples suck
				for (auto Column : std::vector<Tuple>
				{
					{PlaylistColumns::Track, false},
					{PlaylistColumns::Track, true},
					{PlaylistColumns::Album, false},
					{PlaylistColumns::Album, true},
					{PlaylistColumns::Artist, false},
					{PlaylistColumns::Artist, true},
					{PlaylistColumns::Title, false},
					{PlaylistColumns::Title, true}
				})
				{
					bool Found = false;
					for (int ChildIndex = 0; ChildIndex < Root->childCount(); ++ChildIndex)
					{
						auto Child = Root->child(ChildIndex);
						if (
							(static_cast<PlaylistColumns>(Child->data(0, Qt::UserRole + 1).toInt()) == Column.ID) &&
							(Child->data(0, Qt::UserRole + 2).toBool() == Column.Reverse))
						{
							Found = true;
							break;
						}
					}
					if (Found) continue;
					CreateRow(Column.ID, Column.Reverse, false);
				}
			}
			SortLayout->addWidget(Table);

			auto ActionLayout = new QBoxLayout(QBoxLayout::RightToLeft);
			auto Sort = new QPushButton("Sort");
			ActionLayout->addWidget(Sort);
			Sort->setDefault(true);
			auto Cancel = new QPushButton("Cancel");
			ActionLayout->addWidget(Cancel);
			ActionLayout->addStretch();
			SortLayout->addLayout(ActionLayout);

			Dialog->setLayout(SortLayout);

			QObject::connect(Sort, &QPushButton::clicked, [=](bool)
			{
				std::list<PlaylistType::SortFactor> SortFactors;
				Settings->beginWriteArray(SCSort);
				int SaveIndex = 0;
				auto Root = Table->invisibleRootItem();
				for (int ChildIndex = 0; ChildIndex < Root->childCount(); ++ChildIndex)
				{
					auto Child = Root->child(ChildIndex);
					if (Child->checkState(0) == Qt::Checked)
					{
						Settings->setArrayIndex(SaveIndex++);
						Settings->setValue(SCSortID, Child->data(0, Qt::UserRole + 1));
						Settings->setValue(SCSortReverse, Child->data(0, Qt::UserRole + 2));
						SortFactors.emplace_back(
							static_cast<PlaylistColumns>(Child->data(0, Qt::UserRole + 1).toInt()),
							Child->data(0, Qt::UserRole + 2).toBool());
					}
				}
				Settings->endArray();

				Playlist->Sort(SortFactors);
				Dialog->done(0);
			});

			QObject::connect(Cancel, &QPushButton::clicked, [=](bool)
				{ Dialog->done(0); });

			Dialog->show();
		});

		QObject::connect(Previous, &QAction::triggered, [=](bool)
		{
			auto PreviousID = Playlist->GetPreviousID();
			if (!PreviousID) return;
			Volition->Request();
			Core->Play(*PreviousID, 0ul);
		});

		QObject::connect(PlayStop, &QAction::triggered, [=](bool)
		{
			auto IsPlaying = Playlist->IsPlaying();
			if (!IsPlaying) return;
			if (*IsPlaying)
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
			auto NextID = Playlist->GetNextID();
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
	QTContext.setStyleSheet(
		"QGroupBox {"
		"	border: 1px solid gray;"
		"	border-radius: 4px;"
		"	margin-top: 0.5em;"
		"}"
		"QGroupBox::title {"
		"	subcontrol-origin: margin;"
		"	left: 10px;"
		"	padding: 0 3px 0 3px;"
		"}"); // Thanks for the sensible defaults, qt!



	QSettings Settings("Zarbosoft", "ICBM");
	::Settings = &Settings;

	OpenServerSelect();
	return QTContext.exec();
}
