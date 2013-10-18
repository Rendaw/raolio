local LinkFlags = ' -pthread -lboost_system -lboost_filesystem -lboost_regex -lev'

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

raolioclient = Define.Executable
{
	Name = 'raolioclient',
	Sources = Item()
		:Include 'client.cxx'
		:Include 'clientcore.cxx'
		:Include(icmbcMocOutputs),
	Objects = SharedObjects,
	LinkFlags = LinkFlags .. ' -lvlc'
}

raolioserver = Define.Executable
{
	Name = 'raolioserver',
	Sources = Item()
		:Include 'server.cxx',
	Objects = SharedObjects,
	LinkFlags = LinkFlags
}

raolioremote = Define.Executable
{
	Name = 'raolioremote',
	Sources = Item()
		:Include 'remote.cxx',
	Objects = SharedObjects,
	LinkFlags = LinkFlags
}

if not IsDebug()
then
	Package = Define.Package
	{
		Executables = raolioclient:Include(raolioremote):Include(raolioserver),
		Resources = Item() + '*.png',
		Licenses = Item() + '../license.txt'
	}
end
