// #include "scom2_traverse.hpp"
//
// #include <iostream>
// #include <string>
// #include <string_view> // Очень важен!
// #include <vector>
// #include <functional>
// #include <optional>
// #include <cstdint>
// #include <map>
//
// namespace sdf_raster {
//
// constexpr uint32_t MASK_TYPE = 0x80000000;
// constexpr uint32_t MASK_PAYLOAD = 0x7FFFFFFF;
// constexpr uint32_t TYPE_BRANCH = 0x80000000;
// constexpr uint32_t TYPE_LEAF = 0x00000000;
//
// struct TraversalContext {
//     const uint32_t* tree_buffer_start;
//     const char* string_table_start;
// };
//
// template <typename F_Enter, typename F_Leave, typename F_Leaf>
// struct TraversalCallbacks {
//     F_Enter on_enter_node; // (key, depth)
//     F_Leave on_leave_node; // (key, depth)
//     F_Leaf on_visit_leaf;   // (key, value, depth)
// };
//
//
// // --- 3. Новый, "умный" traverse для буфера ---
//
// template <typename Callbacks, typename PruneFunc>
// void traverse(
//     const uint32_t* current_node_ptr,      // Указатель на текущий узел в буфере
//     std::string_view key,                  // Ключ, по которому мы сюда пришли
//     TraversalContext& context,             // Глобальный контекст с буферами
//     Callbacks&& callbacks,
//     PruneFunc&& should_prune,
//     const std::optional<int>& max_depth,
//     int current_depth = 0) 
// {
//     if (max_depth.has_value() && current_depth > max_depth.value()) {
//         return;
//     }
//
//     // Передаем ключ в предикат отсечения - это гораздо полезнее!
//     if (should_prune(key, current_depth)) {
//         return;
//     }
//
//     const uint32_t header = *current_node_ptr;
//
//     // --- Случай 1: Ветвь ---
//     if ((header & MASK_TYPE) == TYPE_BRANCH) {
//         callbacks.on_enter_node(key, current_depth);
//
//         const uint32_t child_count = header & MASK_PAYLOAD;
//         const uint32_t* children_descriptors = current_node_ptr + 1;
//
//         if (child_count > 0 && !(max_depth.has_value() && current_depth >= max_depth.value())) {
//             for (uint32_t i = 0; i < child_count; ++i) {
//                 const uint32_t key_offset = children_descriptors[i * 2];
//                 const uint32_t node_offset = children_descriptors[i * 2 + 1];
//
//                 std::string_view child_key(context.string_table_start + key_offset);
//                 const uint32_t* child_node_ptr = context.tree_buffer_start + node_offset;
//
//                 traverse(child_node_ptr, child_key, context, callbacks, should_prune, max_depth, current_depth + 1);
//             }
//         }
//
//         callbacks.on_leave_node(key, current_depth);
//     }
//     // --- Случай 2: Лист ---
//     else { // TYPE_LEAF
//         const uint32_t value_offset = header & MASK_PAYLOAD;
//         std::string_view value(context.string_table_start + value_offset);
//         callbacks.on_visit_leaf(key, value, current_depth);
//     }
// }
//
//
// // --- 4. Функции-обертки (почти не изменились!) ---
//
// // Предикат по умолчанию
// auto no_prune() {
//     return [](std::string_view, int){ return false; };
// }
//
// // Сборщик листьев
// std::vector<std::string> collect_leaves(
//     TraversalContext& context,
//     std::optional<int> max_depth = std::nullopt,
//     std::function<bool(std::string_view, int)> should_prune = no_prune()) 
// {
//     std::vector<std::string> found_leaves;
//
//     auto callbacks = TraversalCallbacks {
//         [](std::string_view, int){}, // on_enter_node: ничего не делаем
//         [](std::string_view, int){}, // on_leave_node: ничего не делаем
//         [&found_leaves](std::string_view key, std::string_view value, int){ // on_visit_leaf
//             found_leaves.emplace_back(value); // Создаем std::string из string_view
//         }
//     };
//
//     traverse(context.tree_buffer_start, "root", context, callbacks, should_prune, max_depth);
//     return found_leaves;
// }
//
// // Генератор JSON
// void generate_json(
//     TraversalContext& context,
//     std::ostream& out,
//     std::optional<int> max_depth = std::nullopt,
//     std::function<bool(std::string_view, int)> should_prune = no_prune())
// {
//     // Стек для отслеживания, является ли текущий элемент первым в объекте 
//     // (чтобы правильно расставлять запятые).
//     std::vector<bool> is_first_child_stack;
//
//     auto add_comma_if_needed = [&](int depth) {
//         if (depth < is_first_child_stack.size() && !is_first_child_stack[depth]) {
//             out << ",";
//         }
//         out << "\n" << std::string(depth * 2, ' ');
//         if(depth < is_first_child_stack.size()) {
//             is_first_child_stack[depth] = false;
//         }
//     };
//
//     auto callbacks = TraversalCallbacks {
//         // on_enter_node
//         [&](std::string_view key, int depth) {
//             add_comma_if_needed(depth);
//             if (!key.empty() && key != "root") out << "\"" << key << "\": ";
//             out << "{";
//             is_first_child_stack.push_back(true);
//         },
//         // on_leave_node
//         [&](std::string_view, int depth) {
//             is_first_child_stack.pop_back();
//             out << "\n" << std::string(depth * 2, ' ') << "}";
//         },
//         // on_visit_leaf
//         [&](std::string_view key, std::string_view value, int depth) {
//             add_comma_if_needed(depth);
//             out << "\"" << key << "\": \"" << value << "\"";
//         }
//     };
//
//     // Начинаем обход с корневого узла (указатель на начало буфера)
//     traverse(context.tree_buffer_start, "root", context, callbacks, should_prune, max_depth, 0);
//     out << std::endl;
// }
//
// // --- 5. Демонстрация ---
//
// // Функция для "сборки" нашего сжатого буфера для теста
// void build_test_buffers(std::vector<uint32_t>& tree, std::vector<char>& strings) {
//     auto add_string = [&](const std::string& s) -> uint32_t {
//         uint32_t offset = strings.size();
//         strings.insert(strings.end(), s.begin(), s.end());
//         strings.push_back('\0'); // Нуль-терминатор
//         return offset;
//     };
//
//     // Таблица строк
//     uint32_t key_config_off = add_string("config");
//     uint32_t key_version_off = add_string("version");
//     uint32_t val_version_off = add_string("1.0");
//     uint32_t key_user_off = add_string("user");
//     uint32_t val_user_off = add_string("admin");
//     uint32_t key_data_off = add_string("data");
//     uint32_t key_payload_off = add_string("payload");
//     uint32_t val_payload_off = add_string("important_data");
//     uint32_t key_internal_off = add_string("internal_debug");
//     uint32_t key_secret_off = add_string("secret");
//     uint32_t val_secret_off = add_string("secret_password");
//
//     // Структура дерева (адресация в uint32_t)
//     // root (branch, 3 children) -> offset 0
//     //   config (branch, 2 children) -> offset 7
//     //   data (branch, 1 child) -> offset 12
//     //   internal_debug (branch, 1 child) -> offset 15
//     //     version (leaf) -> offset 18
//     //     user (leaf) -> offset 19
//     //     payload (leaf) -> offset 20
//     //     secret (leaf) -> offset 21
//
//     tree.resize(22);
//     // root @ 0
//     tree[0] = TYPE_BRANCH | 3;
//     tree[1] = key_config_off; tree[2] = 7;
//     tree[3] = key_data_off;   tree[4] = 12;
//     tree[5] = key_internal_off; tree[6] = 15;
//
//     // config @ 7
//     tree[7] = TYPE_BRANCH | 2;
//     tree[8] = key_version_off; tree[9] = 18;
//     tree[10] = key_user_off;   tree[11] = 19;
//
//     // data @ 12
//     tree[12] = TYPE_BRANCH | 1;
//     tree[13] = key_payload_off; tree[14] = 20;
//
//     // internal_debug @ 15
//     tree[15] = TYPE_BRANCH | 1;
//     tree[16] = key_secret_off; tree[17] = 21;
//
//     // leaves
//     tree[18] = TYPE_LEAF | val_version_off; // version
//     tree[19] = TYPE_LEAF | val_user_off;    // user
//     tree[20] = TYPE_LEAF | val_payload_off; // payload
//     tree[21] = TYPE_LEAF | val_secret_off;  // secret
// }
//
// void dump_sdf_scom2_text (const scom2::SCom2Tree& scom2, const std::string& path_to_dump) {
//     std::ofstream dump_file (path_to_dump);
//     if (!dump_file.is_open ()) {
//         LOG_ERROR ("could not open file {} for dumping scom2", path_to_dump);
//         return;
//     }
//
//     dump_file << "SDF SCom2 Dump:" << std::endl;
//     dump_file << "----------------------------------------" << std::endl;
//
//     // TODO
//
//     dump_file.close ();
//     LOG_INFO ("SDF SCom2 successfully dumped to '{}'", path_to_dump);
// }
//
// } // sdf_raster
//
