Info = 
{
	PackageName = 'raolio',
	Company = 'Zarbosoft',
	ShortDescription = 'A media player designed for networked playback synchronization.',
	ExtendedDescription = 'Raolio is a client-server media player system where any client can direct playback.  Raolio is suitable for private media performances in office or long-distance situations where turning the volume up really high isn\'t a solution.',
	Version = 0,
	Website = 'http://www.zarbosoft.com/raolio',
	Forum = 'http://www.zarbosoft.com/forum/index.php?board=9.0',
	CompanyWebsite = 'http://www.zarbosoft.com/',
	Author = 'Rendaw',
	EMail = 'spoo@zarbosoft.com'
}

Arch64 =
{
	LicenseStyle = 'BSD',
	Dependencies = {'boost>=1.54.0-3', 'boost-libs>=1.54.0-3', 'libev>=4.15-1', 'qt5-base>=5.1.1-1', 'vlc>=2.1.0-3'}
}

Ubuntu12 =
{
	Section = '',
	Dependencies = {} -- In the form 'pkg (>= version)' as from trial and error
}

if arg and arg[1]
then
	print(Info[arg[1]])
end

