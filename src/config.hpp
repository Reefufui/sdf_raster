#pragma once // Вместо #ifndef/#define/#endif, в современном C++ можно использовать #pragma once
#include <string>     // Для std::string
#include <map>        // Можно убрать, так как cmd_line_params_ больше не std::map
#include <stdexcept>  // Для std::runtime_error
#include <memory>     // Для std::unique_ptr

// Подключение cxxopts и nlohmann/json как header-only библиотек.
// Их .hpp файлы должны находиться в пути поиска включаемых файлов.
// Убедитесь, что nlohmann/json.hpp доступен.
#include "cxxopts.hpp"
#include "nlohmann/json.hpp" // В вашем стиле

// Используем псевдоним для удобства
// using json = nlohmann::json; // Можно использовать, но для вашего стиля я пропишу nlohmann::json

/**
 * @brief Singleton class for managing application configuration.
 *
 * This class handles parsing command-line arguments, loading JSON
 * configuration files, and providing a unified access point to
 * all application parameters.
 */
class Config
{
public:

    // Удаляем конструктор копирования и оператор присваивания
    // для обеспечения единственности экземпляра (Singleton pattern).
    Config (const Config &) = delete;
    Config & operator = (const Config &) = delete;

    /**
     * @brief Get the single instance of the Config manager.
     * @return Reference to the singleton Config instance.
     */
    static Config & get_instance ();

    /**
     * @brief Initialize the configuration manager.
     *
     * This method parses command-line arguments and then loads parameters
     * from the specified JSON configuration file.
     * Command-line arguments take precedence over config file parameters.
     *
     * @param argc The argument count from main (forwarded).
     * @param argv The argument vector from main (forwarded).
     */
    void init (int argc, char * argv []);

    /**
     * @brief Get a configuration parameter by its key.
     *
     * This method first checks command-line arguments (including defaults set by cxxopts),
     * then the JSON config.
     * Throws std::runtime_error if the key is not found in either source.
     *
     * @tparam T The expected type of the parameter.
     * @param key The string key of the parameter.
     * @return The value of the parameter, cast to type T.
     */
    template < typename T >
    T get (const std::string & key) const
    {
        // 1. Поиск в результатах парсера командной строки cxxopts.
        // `parsed_cmd_line_result_` содержит все опции, определенные в cxxopts,
        // включая те, что были переданы явно, и те, что взяты по умолчанию.
        if (parsed_cmd_line_result_ && parsed_cmd_line_result_->count (key) ) {
            return parsed_cmd_line_result_->operator [ ] (key).as < T > ( ) ;
        }

        // 2. Поиск в JSON-конфиге.
        // Используем приватный вспомогательный метод для обработки вложенных ключей.
        const nlohmann::json * json_val_ptr = find_json_value (config_data_, key);
        if (json_val_ptr != nullptr) {
            return json_val_ptr->get < T > ( ) ;
        }

        // Если ничего не найдено в обоих источниках, выбрасываем исключение
        throw std::runtime_error ("Parameter '" + key + "' not found.");
    }

    /**
     * @brief Get a configuration parameter with a default value if not found.
     *
     * @tparam T The expected type of the parameter.
     * @param key The string key of the parameter.
     * @param default_value The value to return if the key is not found.
     * @return The value of the parameter, or default_value if not found.
     */
    template < typename T >
    T get (const std::string & key, const T & default_value) const
    {
        try {
            return get < T > (key);
        } catch (const std::runtime_error & /* e */) {
            return default_value;
        }
    }

    /**
     * @brief Prints all loaded configuration parameters (command-line and JSON).
     *        For debugging purposes.
     */
    void print_all () const ;

private:
    // Приватный конструктор для синглтона.
    Config ( ) ;

    /**
     * @brief Parses command-line arguments using cxxopts.
     * @param argc The argument count.
     * @param argv The argument vector.
     */
    void parse_command_line (int argc, char * argv []);

    /**
     * @brief Loads configuration data from the JSON file specified by config_file_name_.
     */
    void load_config_file ();

    /**
     * @brief Helper function to find a value in a JSON object using dot notation for nested keys.
     * @param j The JSON object to search.
     * @param k The key string (can use "parent.child.value" notation).
     * @return Pointer to the JSON value if found, nullptr otherwise.
     */
    const nlohmann::json * find_json_value (const nlohmann::json & j, const std::string & k) const;

    std::string config_file_name_;                 ///< Path to the JSON config file.
    nlohmann::json config_data_;                             ///< JSON object holding config file data.
    // Храним весь объект ParseResult, чтобы иметь доступ ко всем опциям
    // (включая дефолтные значения из cxxopts) по их имени.
    std::unique_ptr < cxxopts::ParseResult > parsed_cmd_line_result_;
};
