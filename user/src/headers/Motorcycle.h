#pragma once
#include "Library/LiveActor/LiveActor.h"

class Motorcycle : public al::LiveActor {
public:
    Motorcycle(const char* name);
private:
    char buffer[0x250 - sizeof(al::LiveActor)];
};