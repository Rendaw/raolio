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

Ubuntu =
{
	Section = '',
	Dependencies = 
	{
		'libboost-filesystem (>= 1.53.0-6)',
		'libboost-regex (>= 1.53.0-6)',
		'libboost-system (>= 1.53.0-6)',
		'libqt5core5 (>= 5.0.2)',
		'libqt5widgets5 (>= 5.0.2)',
		'libqt5gui5 (>= 5.0.2)',
		'libev4 (>= 1.4.11-1)',
		'libvlc5 (>= 2.0.8-1)'
	}
}

if arg and arg[1]
then
	print(Info[arg[1]])
end

