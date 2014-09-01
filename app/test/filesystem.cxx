#include "../filesystem.h"
#include "../extrastandard.h"

#include <iostream>

int main(int, char **)
{
	std::cout << "The beginning" << std::endl;
	try 
	{ 
		auto Temp = Filesystem::PathT::Temp(false); 
		std::cout << Temp << std::endl;
	}
	catch (ConstructionErrorT &Error) { std::cout << "Error " << Error << std::endl; }
	std::cout << "The end" << std::endl;
	return 0;
}

