DoOnce 'app/translation/Tupfile.lua'

local LinkFlags = ' -lev'
if tup.getconfig 'PLATFORM' ~= 'windows'
then
	LinkFlags = LinkFlags .. ' -pthread -lboost_system -lboost_filesystem -lboost_regex'
else
	LinkFlags = LinkFlags .. ' -lboost_system-mt -lboost_filesystem-mt -lboost_regex-mt -lintl -lws2_32'
end

local SharedObjects = Define.Objects
{
	Sources = Item()
		+ 'shared.cxx'
		+ 'core.cxx'
		+ 'hash.cxx'
		+ 'md5.c'
		+ 'network.cxx'
} + TranslationObjects

local SharedClientObjects = Define.Objects
{
	Sources = Item()
		+ 'clientcore.cxx'
}

local WindowsIconRes = Item()
local ExtraCoreLibraries = Item()
local ExtraBoostLibraries = Item()
local ExtraVLCLibraries = Item()
local ExtraQt5Libraries = Item()
local ExtraQt5PlatformLibraries = Item()
local ExtraBoostLicenses = Item()
local ExtraVLCLicenses = Item()
local ExtraQt5Licenses = Item()
local ExtraGettextLicenses = Item()
local ExtraLibEVLicenses = Item()
if tup.getconfig 'PLATFORM' == 'windows'
then
	local WindowsIcon = Define.Raw
	{
		Inputs = Item 'raolio.png',
		Outputs = Item 'raolio.ico',
		Command = 'convert raolio.png raolio.ico'
	}

	local WindowsIconRc = Define.Write
	{
		Output = Item 'raolioicon.rc',
		Text = 'id ICON "raolio.ico"'
	}

	WindowsIconRes = Define.Raw
	{
		Inputs = WindowsIcon + WindowsIconRc,
		Outputs = Item 'raolioicon.res',
		Command = tup.getconfig 'WINDRES' .. ' ' .. tostring(WindowsIconRc) .. ' -O coff -o raolioicon.res'
	}

	local AddExtras = function(Storage, Config)
		local Added = 0
		for Match in tup.getconfig(Config):gmatch '[^:]+'
			do Storage = Storage + Match; Added = Added + 1 end
		if Added == 0 then error(Config .. ' is empty!  Package will be broken without this config properly specified.') end
		return Storage
	end
	ExtraCoreLibraries = AddExtras(ExtraCoreLibraries, 'WINDOWSCOREDLLS')
	ExtraBoostLibraries = AddExtras(ExtraBoostLibraries, 'WINDOWSBOOSTDLLS')
	ExtraVLCLibraries = AddExtras(ExtraVLCLibraries, 'WINDOWSVLCDLLS')
	ExtraQt5Libraries = AddExtras(ExtraQt5Libraries, 'WINDOWSQT5DLLS')
	ExtraQt5PlatformLibraries = AddExtras(ExtraQt5PlatformLibraries, 'WINDOWSQT5PLATFORMDLLS')
	ExtraBoostLicenses = AddExtras(ExtraBoostLicenses, 'WINDOWSBOOSTLICENSES')
	ExtraVLCLicenses = AddExtras(ExtraVLCLicenses, 'WINDOWSVLCLICENSES')
	ExtraQt5Licenses = AddExtras(ExtraQt5Licenses, 'WINDOWSQT5LICENSES')
	ExtraGettextLicenses = AddExtras(ExtraGettextLicenses, 'WINDOWSGETTEXTLICENSES')
	ExtraLibEVLicenses = AddExtras(ExtraLibEVLicenses, 'WINDOWSLIBEVLICENSES')
end

local PackageDependencies =
	(tup.getconfig 'PLATFORM' == 'arch64' and "'boost>=1.54.0-3', 'boost-libs>=1.54.0-3', 'libev>=4.15-1'" or '') ..
	(tup.getconfig 'PLATFORM' == 'ubuntu' and 'libboost-all-dev (>= 1.53.0-0), libev4 (>= 1.4.11-1)' or '')

if tup.getconfig 'BUILDGUI' ~= 'false'
then
	local raolioguiMocOutputs = Define.Raw
	{
		Inputs = Item 'qtaux.h',
		Outputs = Item 'moc_qtaux.cxx',
		Command = 'moc qtaux.h > moc_qtaux.cxx'
	}

	local LinkFlags = LinkFlags
	if tup.getconfig 'PLATFORM' == 'windows'
	then
		LinkFlags = LinkFlags .. ' -mwindows'
	end

	raoliogui = Define.Executable
	{
		Name = 'raoliogui',
		Sources = Item() + 'gui.cxx' + raolioguiMocOutputs,
		Objects = SharedObjects + SharedClientObjects + WindowsIconRes,
		BuildFlags = tup.getconfig 'GUIBUILDFLAGS',
		LinkFlags = LinkFlags .. ' ' .. tup.getconfig 'GUILINKFLAGS' .. ' -lvlc'
	}

	local PackageDependencies = PackageDependencies ..
		(tup.getconfig 'PLATFORM' == 'arch64' and ", 'qt5-base>=5.1.1-1', 'vlc>=2.1.0-3'" or '') ..
		(tup.getconfig 'PLATFORM' == 'ubuntu' and ', libqt5core5 (>= 5.0.2), libqt5widgets5 (>= 5.0.2), libqt5gui5 (>= 5.0.2), libvlc5 (>= 2.0.8-0)' or '')

	Package = Define.Package
	{
		Name = 'raoliogui',
		Dependencies = PackageDependencies,
		Executables = raoliogui,
		Resources = Item '*.png',
		ArchLicenseStyle = 'LGPL',
		DebianSection = 'sound',
		Licenses = Item('../license-raolio.txt'),
		ExtraLibraries = ExtraCoreLibraries + ExtraBoostLibraries + ExtraVLCLibraries + ExtraQt5Libraries,
		ExtraQt5PlatformLibraries = ExtraQt5PlatformLibraries,
		ExtraLicenses = ExtraBoostLicenses + ExtraVLCLicenses + ExtraQt5Licenses + ExtraGettextLicenses + ExtraLibEVLicenses
	}
end

if tup.getconfig 'BUILDSERVER' ~= 'false'
then
	raolioserver = Define.Executable
	{
		Name = 'raolioserver',
		Sources = Item() + 'server.cxx',
		Objects = SharedObjects + WindowsIconRes,
		LinkFlags = LinkFlags
	}

	raolioremote = Define.Executable
	{
		Name = 'raolioremote',
		Sources = Item() + 'remote.cxx',
		Objects = SharedObjects + WindowsIconRes,
		LinkFlags = LinkFlags
	}

	Package = Define.Package
	{
		Name = 'raolioserver',
		Dependencies = PackageDependencies,
		Executables = raolioserver + raolioremote,
		ArchLicenseStyle = 'LGPL3',
		DebianSection = 'sound',
		Licenses = Item '../license-raolio.txt',
		ExtraLibraries = ExtraCoreLibraries + ExtraBoostLibraries + ExtraVLCLibraries,
		ExtraLicenses = ExtraBoostLicenses + ExtraVLCLicenses + ExtraGettextLicenses + ExtraLibEVLicenses
	}
end

if tup.getconfig 'BUILDCLI' ~= 'false'
then
	raoliocli = Define.Executable
	{
		Name = 'raoliocli',
		Sources = Item() + 'cli.cxx',
		Objects = SharedObjects + SharedClientObjects + WindowsIconRes,
		LinkFlags = LinkFlags .. ' -lreadline -lvlc'
	}

	local PackageDependencies = PackageDependencies ..
		(tup.getconfig 'PLATFORM' == 'arch64' and ", 'readline>=6.2.004-1', 'vlc>=2.1.0-3'" or '') ..
		(tup.getconfig 'PLATFORM' == 'ubuntu' and ', readline6 (>= 6.2-9), libvlc5 (>= 2.0.8-0)' or '')
	Package = Define.Package
	{
		Name = 'raoliocli',
		Dependencies = PackageDependencies,
		Executables = raoliocli,
		ArchLicenseStyle = 'LGPL',
		DebianSection = 'sound',
		Licenses = Item '../license-raolio.txt'
	}
end
