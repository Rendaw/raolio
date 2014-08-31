#include "filesystem.h"

#include <regex>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#include <direct.h>
#include <shlobj.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#endif

namespace Filesystem
{

PathElementT::PathElementT(PathSettingsT const &Settings) : Parent(new PathSettingsT(Settings)) { }

PathElementT::~PathElementT(void)
{
	if (Parent.Is<PathElementT const *>())
	{
		auto Element = Parent.Get<PathElementT const *>();
		Element->Count -= 1;
		if (Element->Count == 0) delete Element;
	}
	else
	{
		Assert(Parent.Is<PathSettingsT *>());
		delete Parent.Get<PathSettingsT *>();
	}
}

std::string PathElementT::Render(void) const
{
	PathSettingsT *Root = nullptr;
	std::list<PathElementT const *> Parts;
	{
		VariantT <PathElementT const *, PathSettingsT *> Part(this);
		while (Part.Is<PathElementT const *>())
		{
			Parts.push_front(Part.Get<PathElementT const *>());
			Part = Part.Get<PathElementT const *>()->Parent;
		}
		Root = Part.Get<PathSettingsT *>();
	}
	std::stringstream Out;
	if (Root->WindowsDrive) Out << *Root->WindowsDrive;
	if (Parts.size() == 1)
	{
		Out << Root->Separator;
	}
	else
	{
		if (!Parts.empty()) Parts.pop_front();
		for (auto &Part : Parts) Out << Root->Separator << Part->Value;
	}
	std::cout << "Rendered " << Out.str() << std::endl;
	return Out.str();
}
	
bool PathElementT::Contains(PathElementT const *Other) const
{
	PathElementT const *Candidates[2]{this, Other};
	PathSettingsT *PathSettings[2];
	std::vector<PathElementT const *> Parts[2];
	for (size_t Candidate = 0; Candidate < 2; ++Candidate)
	{
		VariantT <PathElementT const *, PathSettingsT *> Part(Candidates[Candidate]);
		while (Part.Is<PathElementT const *>())
		{
			Parts[Candidate].push_back(Part.Get<PathElementT const *>());
			Part = Part.Get<PathElementT const *>()->Parent;
		}
		std::reverse(Parts[Candidate].begin(), Parts[Candidate].end());
		PathSettings[Candidate] = Part.Get<PathSettingsT *>();
	}
	if (PathSettings[0]->WindowsDrive != PathSettings[1]->WindowsDrive) return false;
	if (Parts[1].size() < Parts[0].size()) return false;
	for (size_t Part = 0; Part < Parts[0].size(); ++Part)
		if (Parts[0][Part]->Value != Parts[1][Part]->Value) return false;
	return true;
}

PathT PathElementT::Enter(std::string const &Value) const
{
	return PathT(new PathElementT(this, Value));
}

std::regex ElementRegex("[/\\\\]+([^/\\\\]+)");
PathT PathElementT::EnterRaw(std::string const &Raw) const
{
	std::string Text = Raw;
	PathT Out(this);
	std::smatch Matches;
	while (std::regex_search(Text, Matches, ElementRegex)) 
	{
		if (Matches[1] == ".") continue;
		else if (Matches[1] == "..") Out = Out->Exit();
		else Out = Out->Enter(Matches[1]);
		Text = Matches.suffix().str();
	}
	return Out;
}
	
PathT PathElementT::Exit(void) const
{
	if (Parent.Is<PathSettingsT *>()) throw ConstructionErrorT() << "Cannot exit root path.";
	return {Parent.Get<PathElementT const *>()};
}

bool PathElementT::Exists(void) const
{
#ifdef _WIN32
	//DWORD Attributes = GetFileAttributesW(reinterpret_cast<wchar_t const *>(ToNativeString("\\\\?\\" + AsAbsoluteString()).c_str())); // Doesn't work for some reason -- mixed slashes?
	DWORD Attributes = GetFileAttributesW(reinterpret_cast<wchar_t const *>(ToNativeString(Render()).c_str()));
	if (Attributes == 0xFFFFFFFF) return false;
	if (Attributes == 0x10) return false;
	return true;
#else
	struct stat StatResultBuffer;
	int Result = stat(Render().c_str(), &StatResultBuffer);
	if (Result != 0) return false;
	return S_ISREG(StatResultBuffer.st_mode);
#endif
}

bool PathElementT::FileExists(void) const
{
	// TODO
	Assert(false);
	return true;
}

bool PathElementT::DirectoryExists(void) const
{
	// TODO
	Assert(false);
	return true;
}

bool PathElementT::Delete(void) const
{
#ifdef _WIN32
	return _wunlink(reinterpret_cast<wchar_t const *>(ToNativeString(Render()).c_str())) == 0;
#else
	return unlink(Render().c_str()) == 0;
#endif
}

bool PathElementT::CreateDirectory(void) const
{
	std::list<PathElementT const *> Parts;
	{
		VariantT <PathElementT const *, PathSettingsT *> Part(this);
		while (Part.Is<PathElementT>())
		{
			Parts.push_front(Part.Get<PathElementT const *>());
			Part = Part.Get<PathElementT const *>()->Parent;
		}
	}
	for (auto &Part : Parts)
	{
#ifdef _WIN32
		int Result = _wmkdir(reinterpret_cast<wchar_t const *>(ToNativeString(Part->Render()).c_str()));
#else
		int Result = mkdir((Part->Render()).c_str(), 0777);
#endif
		if (Result == -1 && errno != EEXIST)
			return false;
	}
	return true;
}

size_t PathElementT::Depth(void) const
{
	size_t Count = 0;
	VariantT <PathElementT const *, PathSettingsT *> Part(this);
	while (Part.Is<PathElementT>())
	{
		++Count;
		Part = Part.Get<PathElementT const *>()->Parent;
	}
	return Count;
}

std::string const &PathElementT::Filename(void) const { return Value; }

std::string PathElementT::Directory(void) const { return Parent.Is<PathElementT const *>() ? Parent.Get<PathElementT const *>()->Render() : Render(); }

OptionalT<std::string> PathElementT::Extension(void) const
{
	std::smatch Matches;
	std::regex_search(Value, Matches, std::regex("\\.([^.]+)$"));
	for (auto &Match : Matches) return Match.str();
	return {};
}

PathElementT::PathElementT(PathElementT const *Parent, std::string const &Value) : Value(Value), Parent(Parent) 
{ 
	++Parent->Count; 
}

static std::regex const DriveRegex("^([a-zA-Z]:)(.*)$");
PathT PathT::Absolute(std::string const &Raw)
{
#ifdef _WIN32
	std::smatch DriveMatch;
	if (!Assert(std::regex_search(Raw, DriveMatch, DriveRegex))) throw ConstructionErrorT() << "Windows absolute paths must contain drive.  This path is invalid: " << Raw;
	return PathT({DriveMatch[0], "\\"})->EnterRaw(DriveMatch[1]);
#else
	return PathT(PathSettingsT{{}, std::string(1, Raw[0])})->EnterRaw(Raw);
#endif
}

PathT PathT::Here(void)
{
	std::vector<char> Buffer(FILENAME_MAX);
	if (!getcwd(&Buffer[0], Buffer.size())) throw ConstructionErrorT() << "Couldn't obtain working directory!";
	return Absolute(std::string(&Buffer[0], Buffer.size()));
}

PathT PathT::Qualify(std::string const &Raw)
{
	if (Raw.empty()) return Here();
	if (Raw[0] == '/') return Absolute(Raw);
	if (Raw[0] == '\\') return Absolute(Raw);
	std::smatch DriveMatch;
	if (std::regex_search(Raw, DriveMatch, DriveRegex)) return Absolute(Raw);
	return Here()->EnterRaw(Raw);
}
	
PathT PathT::Temp(bool File, OptionalT<PathT> const &Base)
{
#ifdef _WIN32
	std::vector<wchar_t> BaseString;
	if (Base) 
	{
		auto Native = ToNativeString(Base->Render());
		std::copy(Native.begin(), Native.end(), BaseString.begin());
	}
	else 
	{
		// Get temp dir
		BaseString.resize(MAX_PATH);
		auto Length = GetTempPathW(BaseString.size(), &BaseString[0]);
		if (Length == 0) throw ConstructionErrorT() << "Could not find temporary file directory.";
		BaseString.resize(Length);
	}

	std::vector<wchar_t> TemporaryFilename;
	TemporaryFilename.resize(MAX_PATH);
	auto Length = GetTempFileNameW(&BaseString[0], L"zar", 0, &TemporaryFilename[0]);
	if (Length == 0) throw ConstructionErrorT() << "Could not determine a temporary filename.";
	Length = GetLongPathNameW(&TemporaryFilename[0], &TemporaryFilename[0], TemporaryFilename.size());
	AssertGE(Length, 0);
	TemporaryFilename.resize(Length);
	if ((unsigned int)Length > TemporaryFilename.size())
	{
		Length = GetLongPathNameW(&TemporaryFilename[0], &TemporaryFilename[0], TemporaryFilename.size());
		AssertLE((unsigned int)Result, TemporaryFilename.size());
	}
	if (Length == 0) throw Error::System("Could not qualify the temporary filename.");
	if (File)
	{
		auto Result = CreateFileW(&TemporaryFilename[0], GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (Result == INVALID_HANDLE_VALUE) throw ConstructionErrorT() << "Failed to create temporary file.";
		CloseHandle(Result);
	}
	else
	{
		int Result = _wmkdir(&TemporaryFilename[0]);
		if (Result == -1) throw ConstructionErrorT() << "Failed to create temporary directory (error code " << Result << ").";
	}
	return Absolute(FromNativeString(std::u16string(&TemporaryFilename[0], TemporaryFilename.size())));
#else
	std::vector<char> BaseString;
	if (Base)
	{
		auto Native = (*Base)->Render();
		BaseString.insert(BaseString.end(), Native.begin(), Native.end());
	}
	else 
	{
		char const *Buffer = getenv("TMPDIR");
		if (Buffer == nullptr) Buffer = getenv("P_tmpdir");
		if (Buffer == nullptr) Buffer = "/tmp";
		BaseString.insert(BaseString.end(), Buffer, Buffer + strlen(Buffer));
	}

	// Create temp file
	static char const *Template = "/XXXXXX";
	BaseString.insert(BaseString.end(), Template, Template + sizeof(Template));
	AssertE(BaseString.back(), 0);
	if (File)
	{
		auto Result = mkstemp(&BaseString[0]);
		if (Result < 0) throw ConstructionErrorT() << "Failed to create temporary file with template " << std::string(&BaseString[0], BaseString.size()) << ".";
		close(Result);
		return Absolute(std::string(&BaseString[0], BaseString.size() - 1));
	}
	else
	{
		auto Result = mkdtemp(&BaseString[0]);
		if (Result <= 0) throw ConstructionErrorT() << "Failed to create temporary directory with template " << std::string(&BaseString[0], BaseString.size()) << ".";
		return Absolute(Result);
	}
#endif
}

PathT::PathT(PathElementT const *Element) { Set(Element); }

PathT::PathT(PathSettingsT const &Settings) { Set(new PathElementT(Settings)); }
	
PathT::PathT(void) { Set(new PathElementT(PathSettingsT{{}, "/"})); }
	
PathT::PathT(PathT const &Other) { Set(Other.Element); }

void PathT::Set(PathElementT const *Element)
{ 
	this->Element = Element;
	Element->Count += 1;
}

PathT::~PathT(void) 
{
	Element->Count -= 1;
	if (Element->Count == 0) delete Element;
}
	
PathT &PathT::operator =(PathT const &Other) { Set(Other.Element); return *this; }
	
PathElementT const *PathT::operator ->(void) const { return Element; }

PathT::operator PathElementT const *(void) const { return Element; }

}

