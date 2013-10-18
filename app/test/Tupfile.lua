DoOnce 'app/Tupfile.lua'

RegexTest = Define.Executable
{
	Name = 'regex',
	Sources = Item 'regex.cxx',
	LinkFlags = '-lboost_regex'
}
Define.Test { Executable = RegexTest }

