#ifndef qtaux_h
#define qtaux_h

#include "shared.h"

#include <QObject>
#include <QMetaType>
#include <functional>
#include <memory>

Q_DECLARE_METATYPE(std::function<void(void)>);
struct QTCrossThread : QObject, CallTransferType
{
	QTCrossThread(QObject *Parent) : QObject(Parent)
	{
		struct RegisterTypes { RegisterTypes(void)
		{
			qRegisterMetaType<std::function<void(void)>>();
		}};
		static RegisterTypes Registration{};
		QObject::connect(this, &QTCrossThread::Send, this, &QTCrossThread::Receive);
	}
	inline void Call(std::function<void(void)> const &Function) { Send(Function); }
	inline void operator()(std::function<void(void)> const &Call) override { Send(Call); }
	private slots:
		void Receive(std::function<void(void)> Function) { Function(); }
	signals:
		void Send(std::function<void(void)> Function);
	private:
		Q_OBJECT
};

template <typename DataType> struct QTStorage : QObject
{
	QTStorage(QObject *Parent, DataType &&Data) : QObject(Parent), Data(std::forward<DataType &&>(Data)) {}
	QTStorage(QObject *Parent, DataType const &Data) : QObject(Parent), Data(std::forward<DataType const &>(Data)) {}
	DataType Data;
};

template <typename DataType> auto CreateQTStorage(QObject *Parent, DataType &&Data) -> QTStorage<DataType> *
	{ return new QTStorage<DataType>(Parent, std::forward<DataType>(Data)); }

#endif
