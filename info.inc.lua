Info =
{
	ProjectName = 'raolio',
	Company = 'Zarbosoft',
	ShortDescription = 'A media player designed for networked playback synchronization.',
	ExtendedDescription = 'Raolio is a client-server media player system where any client can direct playback.  Raolio is suitable for private media performances in office or long-distance situations where turning the volume up really high isn\'t a solution.',
	Version = 0,
	Website = 'http://www.zarbosoft.com/raolio',
	Forum = 'http://www.zarbosoft.com/forum/index.php?board=10.0',
	CompanyWebsite = 'http://www.zarbosoft.com/',
	Author = 'Rendaw',
	EMail = 'spoo@zarbosoft.com'
}

if arg and arg[1]
then
	print(Info[arg[1]])
end

