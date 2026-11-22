#pragma once

#ifdef __ANDROID__
#include "AndroidGameData.h"
using GameDataStream = android::GameDataStream;
#else
#include <fstream>
using GameDataStream = std::ifstream;
#endif
