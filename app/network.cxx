#include "network.h"

SocketInfo::~SocketInfo(void) { if (Socket >= 0) close(Socket); }
