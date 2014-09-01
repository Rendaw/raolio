DoOnce 'app/Tupfile.lua'

RegexTest = Define.Executable
{
	Name = 'regex',
	Sources = Item 'regex.cxx',
}
Define.Test { Executable = RegexTest }

FilesystemTest = Define.Executable
{
	Name = 'filesystem',
	Sources = Item 'filesystem.cxx',
	Objects = Item() + '../filesystem.o'
}
Define.Test { Executable = FilesystemTest }
