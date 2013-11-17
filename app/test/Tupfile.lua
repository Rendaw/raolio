DoOnce 'app/Tupfile.lua'

RegexTest = Define.Executable
{
	Name = 'regex',
	Sources = Item 'regex.cxx',
	LinkFlags = tup.getconfig 'PLATFORM' == 'windows' and '-lboost_regex-mt' or '-lboost_regex'
}
Define.Test { Executable = RegexTest }

