#pragma once
#include <string>

namespace StateLog
{
    void open();
    void close();
    const char* reason(double time, const std::string& discrete);
    void write(double time, const char* reason, const std::string& discrete, const std::string& continuous);
}
