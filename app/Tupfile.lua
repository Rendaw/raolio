local LinkFlags = ' -pthread -lboost_system -lboost_filesystem -lboost_regex -lev'

local SharedObjects = Define.Objects
{
	Sources = Item()
		+ 'shared.cxx'
		+ 'core.cxx'
		+ 'hash.cxx'
		+ 'md5.c'
		+ 'network.cxx'
}

local icmbcMocOutputs = Define.Raw
{
	Inputs = Item 'qtaux.h',
	Outputs = Item 'moc_qtaux.cxx',
	Command = 'moc qtaux.h > moc_qtaux.cxx'
}

PackageExecutables = Item()
PackageResources = Item()

if tup.getconfig 'BUILDGUI' ~= 'false'
then
	raoliogui = Define.Executable
	{
		Name = 'raoliogui',
		Sources = Item() + 'gui.cxx' + 'clientcore.cxx' + icmbcMocOutputs,
		Objects = SharedObjects,
		LinkFlags = LinkFlags .. ' -lvlc'
	}
	PackageExecutables = PackageExecutables + raoliogui
	PackageResources = PackageResources + '*.png'
end

if tup.getconfig 'BUILDSERVER'
then
	raolioserver = Define.Executable
	{
		Name = 'raolioserver',
		Sources = Item() + 'server.cxx',
		Objects = SharedObjects,
		LinkFlags = LinkFlags
	}

	raolioremote = Define.Executable
	{
		Name = 'raolioremote',
		Sources = Item() + 'remote.cxx',
		Objects = SharedObjects,
		LinkFlags = LinkFlags
	}

	PackageExecutables = PackageExecutables
		+ (raolioserver)
		+ (raolioremote)
end

if tup.getconfig 'PACKAGE' ~= 'false'
then
	if IsDebug() then error 'Debug builds aren\'t suitable for packaging.  Disable packaging or mark this build as release.' end
	Package = Define.Package
	{
		Executables = PackageExecutables,
		Resources = PackageResources,
		Licenses = Item() + '../license.txt'
	}
end
