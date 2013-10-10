local SharedObjects = Define.Objects
{
	Sources = Item()
		:Include 'shared.cxx'
		:Include 'core.cxx'
		:Include 'hash.cxx'
		:Include 'md5.c'
		:Include 'network.cxx'
}

local icmbcMocOutputs = Define.Raw
{
	Inputs = Item 'qtaux.h',
	Outputs = Item 'moc_qtaux.cxx',
	Command = 'moc qtaux.h > moc_qtaux.cxx'
}

icbmc = Define.Executable
{
	Name = 'icbmc',
	Sources = Item()
		:Include 'client.cxx'
		:Include 'clientcore.cxx'
		:Include(icmbcMocOutputs),
	Objects = SharedObjects,
	LinkFlags = ' -lvlc -lboost_system -lboost_filesystem -lev'
}

icbms = Define.Executable
{
	Name = 'icbms',
	Sources = Item()
		:Include 'server.cxx',
	Objects = SharedObjects,
	LinkFlags = ' -lboost_system -lboost_filesystem -lev'
}

icbmr = Define.Executable
{
	Name = 'icbmr',
	Sources = Item()
		:Include 'remote.cxx',
	Objects = SharedObjects,
	LinkFlags = ' -lboost_system -lboost_filesystem -lev'
}
