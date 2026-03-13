#include "data_channel.hpp"

#include <cassert>

void save_data_channel (std::ofstream &fs, const DataChannel &channel) {
    fs.write ((const char *)(&channel.type), sizeof (channel.type));
    fs.write ((const char *)(&channel.num_components), sizeof (channel.num_components));
    fs.write ((const char *)(&channel.min_val), sizeof (channel.min_val));
    fs.write ((const char *)(&channel.max_val), sizeof (channel.max_val));

    unsigned name_len = channel.name.size ();
    fs.write ((const char *)(&name_len), sizeof (name_len));
    fs.write ((const char *)channel.name.data (), name_len + 1);

    unsigned size = 0;
    if (channel.type == DataChannel::Type::INT) {
        size = channel.data_i.size();
        fs.write ((const char *)(&size), sizeof(unsigned));
        fs.write ((const char *)channel.data_i.data(), channel.data_i.size() * sizeof(int));
    } else if (channel.type == DataChannel::Type::FLOAT) {
        size = channel.data_f.size();
        fs.write ((const char *)(&size), sizeof(unsigned));
        fs.write ((const char *)channel.data_f.data(), channel.data_f.size() * sizeof(float));
    } else if (channel.type == DataChannel::Type::FP8) {
        size = channel.data_fp8.size();
        fs.write ((const char *)(&size), sizeof(unsigned));
        fs.write ((const char *)channel.data_fp8.data(), channel.data_fp8.size() * sizeof(unsigned char));
    } else {
        assert (false && "invalid DataChannel::Type");
    }
}

void load_data_channel(std::ifstream &fs, DataChannel &channel) {
    fs.read ((char *)(&channel.type), sizeof (channel.type));
    fs.read ((char *)(&channel.num_components), sizeof (channel.num_components));
    fs.read ((char *)(&channel.min_val), sizeof (channel.min_val));
    fs.read ((char *)(&channel.max_val), sizeof (channel.max_val));

    unsigned name_len = 0;
    fs.read ((char *)(&name_len), sizeof (name_len));
    channel.name.resize (name_len + 1); //+1 for '\0'
    fs.read ((char *)channel.name.data (), name_len + 1);

    unsigned size = 0;
    fs.read ((char *)(&size), sizeof (unsigned));
    if (channel.type == DataChannel::Type::INT) {
        channel.data_i.resize (size);
        fs.read ((char *)channel.data_i.data (), size * sizeof (int));
    } else if (channel.type == DataChannel::Type::FLOAT) {
        channel.data_f.resize (size);
        fs.read ((char *)channel.data_f.data (), size * sizeof (float));
    } else if (channel.type == DataChannel::Type::FP8) {
        channel.data_fp8.resize (size);
        fs.read((char *)channel.data_fp8.data(), size * sizeof(unsigned char));
    } else {
        assert (false && "invalid DataChannel::Type");
    }
}

uint64_t calculate_bytesize (const DataChannel &channel) {
    uint64_t size = 0;
    if (channel.type == DataChannel::Type::INT) {
        size = channel.data_i.size () * sizeof (int);
    } else if (channel.type == DataChannel::Type::FLOAT) {
        size = channel.data_f.size () * sizeof(float);
    } else if (channel.type == DataChannel::Type::FP8) {
        size = channel.data_fp8.size () * sizeof(unsigned char);
    } else {
        assert (false && "invalid DataChannel::Type");
    }

    return size + 2 * sizeof (int) + channel.name.size () + 1 + sizeof (unsigned) + 2 * sizeof (double);
}

DataChannelInfo get_info (const DataChannel &channel) {
    DataChannelInfo info;
    info.max_val = channel.max_val;
    info.min_val = channel.min_val;
    info.name = channel.name;
    info.num_components = channel.num_components;

    return info;
}

