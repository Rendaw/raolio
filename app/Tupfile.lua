local icmbcMocOutputs = Define.Raw
{
	Inputs = Item 'qtaux.h',
	Outputs = Item 'moc_qtaux.cxx',
	Command = 'moc qtaux.h > moc_qtaux.cxx'
}

icbmc = Define.Executable
{
	Name = 'icbmc',
	Sources = Item():Include 'client.cxx':Include 'clientcore.cxx':Include(icmbcMocOutputs),
	LinkFlags = ' -lvlc -lboost_system -lboost_filesystem'
}

