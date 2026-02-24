#include "config.hpp" // Включаем наш заголовочный файл

#include <iostream>   // Для std::cout, std::cerr
#include <fstream>    // Для std::ifstream
#include <cstdlib>    // Для exit
#include <iomanip>    // Для std::boolalpha, std::setw

// Приватный конструктор
Config::Config ( ) :
    config_file_name_ { "config.json" }, // Инициализация имени файла конфига по умолчанию
    parsed_cmd_line_result_ { nullptr }  // Инициализируем unique_ptr как nullptr
{
    // Здесь никакой сложной логики не должно быть, так как init ( ) будет вызван позднее.
}

// Метод для получения единственного экземпляра
Config & Config::get_instance ( )
{
    static Config instance; // Создается при первом вызове, потокобезопасно в C++11+
    return instance;
}

// Инициализация параметров
void Config::init (int argc, char * argv [])
{
    parse_command_line (argc, argv);
    load_config_file ();
}

// Парсинг аргументов командной строки
void Config::parse_command_line (int argc, char * argv [])
{
    try {
        cxxopts::Options options (argv [0], " - Application Configuration Manager");

        // Определяем опции командной строки.
        // include default_value для всех опций, которые могут быть полезны,
        // даже если не будут найдены в конфиге или в явном виде.
        options.add_options ()
            ("c,config-path", "Path to JSON config file", cxxopts::value <std::string> () -> default_value ("config.json"))
            ("p,port", "Port number for the service", cxxopts::value <int> () -> default_value ("8080"))
            ("v,verbose", "Enable verbose logging output", cxxopts::value <bool> ( ) -> default_value ("false"))
            ("h,help", "Print usage message");

        // Парсим аргументы командной строки и сохраняем весь результат в unique_ptr.
        // Этот объект parsed_cmd_line_result_ будет содержать значения для всех объявленных опций,
        // включая их default_value, если они не были переопределены в командной строке.
        parsed_cmd_line_result_ = std::make_unique <cxxopts::ParseResult> (options.parse (argc, argv) );

        if (parsed_cmd_line_result_->count ("help") ) {
            std::cout << options.help () << std::endl;
            exit (0); // Если пользователь запросил помощь, выходим.
        }

        // Извлекаем имя файла конфигурации из результатов парсинга cxxopts.
        // Это безопасно, так как "config-path" всегда имеет либо значение из командной строки,
        // либо default_value ("config.json").
        config_file_name_ = parsed_cmd_line_result_->operator [ ] ("config-path").as <std::string> ();

    }
    catch (const cxxopts::exceptions::exception & e) {
        // Ошибка парсинга командной строки - это фатальная ошибка, так как
        // мы не знаем, как действовать дальше без корректных аргументов.
        std::cerr << "Error parsing command-line options: " << e.what () << std::endl;
        exit (1);
    }
}

// Загрузка JSON-конфига
void Config::load_config_file ( )
{
    std::ifstream file (config_file_name_);
    if (file.is_open () ) {
        try {
            file >> config_data_; // Парсинг файла в json объект
            std::cout << "Successfully loaded configuration from " << config_file_name_ << std::endl;
        }
        catch (const nlohmann::json::parse_error & e) {
            // Если парсинг JSON не удался, печатаем предупреждение.
            // config_data_ останется пустым, что позволит приложению работать с дефолтами.
            std::cerr << "Error parsing JSON config file '" << config_file_name_ << "': " << e.what () << std::endl;
            config_data_ = nlohmann::json::object ();
        }
    } else {
        // Если файл не найден, печатаем предупреждение, но не выходим.
        // config_data_ останется пустым, что позволит приложению работать с дефолтами.
        std::cerr << "Warning: Could not open configuration file '" << config_file_name_ << "'. Using command-line arguments and hardcoded defaults only." << std::endl;
        config_data_ = nlohmann::json::object ();
    }
}

// Реализация вспомогательного метода для поиска в JSON
const nlohmann::json * Config::find_json_value (const nlohmann::json & j, const std::string & k) const {
    nlohmann::json current = j;
    std::string::size_type start = 0;
    std::string::size_type end = k.find ('.');

    // Проходим по компонентам ключа, разделенным точкой
    while (end != std::string::npos) {
        std::string sub_key = k.substr (start, end - start);
        if (current.is_object () && current.contains (sub_key) ) {
            current = current.at (sub_key); // Переходим на следующий уровень вложенности
        } else {
            return nullptr; // Компонент ключа не найден или текущий элемент не объект
        }
        start = end + 1;
        end = k.find ('.', start);
    }
    // После цикла обрабатываем последнюю часть ключа
    std::string last_key = k.substr (start);
    if (current.is_object () && current.contains (last_key) ) {
        return &current.at (last_key); // Возвращаем указатель на найденное значение
    }
    return nullptr; // Последний компонент ключа не найден
}


// Вывод всех параметров (для отладки)
void Config::print_all () const
{
    std::cout << "\n--- Current Configuration Parameters ---" << std::endl;

    std::cout << "  Command-line arguments (parsed, including cxxopts defaults):" << std::endl;
    if (parsed_cmd_line_result_) {
        // Чтобы вывести все опции, определенные в cxxopts, нужно явно запросить каждую.
        // `cxxopts::ParseResult` не предоставляет удобного итератора по всем объявленным опциям.
        // Поэтому здесь мы вручную выводим те, что определили как важные:
        std::cout << "    config-path: " << parsed_cmd_line_result_->operator [ ] ("config-path").as <std::string> () << std::endl;
        std::cout << "    port: " << parsed_cmd_line_result_->operator [ ] ("port").as <int> () << std::endl;
        std::cout << "    verbose: " << std::boolalpha << parsed_cmd_line_result_->operator [ ] ("verbose").as <bool> () << std::endl;
        // Добавьте сюда другие опции командной строки, которые вы определили в options.add_options ()
        // Например:
        // std::cout << "    custom_option: " << parsed_cmd_line_result_->operator [ ] ("custom_option").as <std::string> () << std::endl;
    } else {
        std::cout << "    Command-line arguments not parsed." << std::endl;
    }

    std::cout << "\n  JSON Config data from '" << config_file_name_ << "':" << std::endl;
    if (!config_data_.empty () ) {
        std::cout << std::setw (2) << config_data_ << std::endl; // Вывод JSON с отступами 2
    } else {
        std::cout << "    No JSON config data loaded or parsed." << std::endl;
    }
    std::cout << "----------------------------------------" << std::endl;
}
