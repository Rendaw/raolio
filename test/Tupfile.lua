DoOnce 'app/Tupfile.lua'

--[[ libc++ is broke RegexTest = Define.Executable
{
	Name = 'regex',
	Sources = Item 'regex.cxx'
}
Define.Test { Executable = RegexTest }]]
