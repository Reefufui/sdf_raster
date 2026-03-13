
void dump_sdf_scom2_text (const SCom2Tree& /*scene*/, const std::string& path_to_dump) {
    std::ofstream dump_file (path_to_dump);
    if (!dump_file.is_open ()) {
        LOG_ERROR ("could not open file {} for dumping scom2", path_to_dump);
        return;
    }

    dump_file << "SDF SCom2 Dump:" << std::endl;
    dump_file << "----------------------------------------" << std::endl;

    // TODO

    dump_file.close ();
    LOG_INFO ("SDF SCom2 successfully dumped to '{}'", path_to_dump);
}

