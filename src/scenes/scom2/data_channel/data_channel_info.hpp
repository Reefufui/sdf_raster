#pragma once

#include <string>

#ifndef KERNEL_SLICER
struct DataChannelInfo
{
    std::string name = "unnamed_channel";
    int num_components = 1;
    double min_val;
    double max_val;
};
#endif

