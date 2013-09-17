#ifndef qtaux_h
#define qtaux_h

#include <QObject>
#include <functional>
#include <memory>

struct QTCrossThread : public QObject
{
	QTCrossThread(QObject *Parent) : QObject(Parent) { QObject::connect(this, &QTCrossThread::Send, this, &QTCrossThread::Receive); }
	void Call(std::function<void(void)> const &Function) { Send(Function); }
	private slots:
		void Receive(std::function<void(void)> Function) { Function(); }
	signals:
		void Send(std::function<void(void)> Function);
	private:
		Q_OBJECT
};

template <typename DataType> struct QTStorage : public QObject
{
	QTStorage(QObject *Parent, DataType &&Data) : QObject(Parent), Data(std::forward<DataType &&>(Data)) {}
	QTStorage(QObject *Parent, DataType const &Data) : QObject(Parent), Data(std::forward<DataType const &>(Data)) {}
	DataType Data;
};

template <typename DataType> auto CreateQTStorage(QObject *Parent, DataType &&Data) -> QTStorage<DataType> *
	{ return new QTStorage<DataType>(Parent, std::forward<DataType>(Data)); }

#endif
