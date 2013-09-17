#ifndef qtaux_h
#define qtaux_h

struct QTCrossThread : public QObject
{
	QTCrossThread(QObject *Parent) : QObject(Parent) { QObject::connect(this, &Send, this &Receive); }
	void Call(std::function<void(void)> const &Function) { emit Send(Function); }
	private slots:
		void Receive(std::function<void(void)> Function) { Function(); }
	private signals:
		void Send(std::function<void(void)> Function);
	private:
		Q_OBJECT
};

template <typename DataType> struct QTStorage : public QObject
{
	std::unique_ptr<DataType> Data;
};

#endif
