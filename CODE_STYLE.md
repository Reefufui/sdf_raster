# Code Style Guide — sdf_raster

## Whitespace Rules

### Space Before Opening Brackets

**Always** put a space before `(`, `<`, `[`:

```cpp
// GOOD
int main (int argc, char** argv);
std::vector <int> v {};
auto lambda = [&] (int x) { return x * 2; };
vkWaitForFences (device, 1, &fence, VK_TRUE, timeout);
this->descriptor_sets [0];
VkExtent2D extent {width, height};

// BAD
int main(int argc, char** argv);
std::vector<int> v {};
vkWaitForFences(device, 1, &fence, VK_TRUE, timeout);
this->descriptor_sets[0];
```

### Indentation

- **4 spaces** everywhere. No tabs.
- Exception: `namespace` blocks do **not** increase indentation level.

```cpp
namespace sdf_raster {

void foo () {
    if (condition) {
        do_something ();
    }
}

} // namespace sdf_raster
```

## `this->` Prefix

**Always** use `this->` when accessing class members and methods. This makes member access explicit and distinguishes it from local variables.

```cpp
// GOOD
SDFRasterizer::SDFRasterizer (std::shared_ptr <VulkanContext> vulkan_context)
    : context (vulkan_context) {
    if (!this->context) {
        throw std::invalid_argument ("...");
    }
    this->init_push_constants ();
    auto result = vkQueueSubmit (this->queue, ...);
}

// BAD
if (!context) {
    throw std::invalid_argument ("...");
}
init_push_constants ();
```

## Spacing in Enumerations

Prefer starting new lines with commas where it aids readability:

```cpp
// GOOD
this->gbuffer_formats = {
    VK_FORMAT_R16G16B16A16_SFLOAT,
    VK_FORMAT_R16G16B16A16_SFLOAT,
    VK_FORMAT_R8G8B8A8_UNORM
};

this->frustum_ds = std::make_unique <FrustumDescriptorSetInfo> (
    this->context->get_device (),
    this->context->get_physical_device (),
    VK_SHADER_STAGE_COMPUTE_BIT,
    this->context->get_total_frames ()
);
```

## Naming Conventions

| Construct        | Convention        | Example                        |
|------------------|-------------------|--------------------------------|
| Classes/structs  | PascalCase        | `SDFRasterizer`, `SessionState` |
| Methods/functions | snake_case       | `init_push_constants`, `load_session` |
| Member variables  | snake_case        | `clear_color`, `vulkan_context` |
| Local variables  | snake_case        | `device_properties`, `frame_index` |
| Constants/macros | SCREAMING_SNAKE_CASE | `RENDERER_NAME`, `VK_CHECK_RESULT` |
| Namespaces       | snake_case        | `sdf_raster`, `spdlog`          |

## Braces

Use K&R style (Egyptian brackets):

```cpp
// GOOD
void foo () {
    if (condition) {
        do_something ();
    } else {
        do_other ();
    }
}

for (uint32_t i = 0; i < count; i++) {
    process (i);
}

// BAD — allman style
void foo ()
{
    if (condition)
    {
        do_something ();
    }
}
```

## Includes

- Sort includes logically: paired header first, then third-party, then standard library.
- Use angle brackets for external headers, quotes for project headers.
- Empty line between groups.

```cpp
#include "sdf_rasterizer.hpp"

#include "application.hpp"
#include "gui.hpp"

#include <spdlog/stopwatch.h>
#include <vk_buffers.h>

#include <array>
#include <fstream>
#include <stdexcept>
```

## Type Conversions

Use `static_cast` for explicit type conversions. Always put spaces around the cast:

```cpp
// GOOD
for (int i = 0; i < static_cast <int> (vec.size ()); i++) {
    process (vec [i]);
}

// BAD
for (int i = 0; i < (int)vec.size(); i++) { }
for (int i = 0; i < int(vec.size()); i++) { }
```

## Commit Messages

Follow conventional commits style:

```
<type>(<scope>): <short description>

<longer description if needed>
```

Types: `feat`, `fix`, `refactor`, `test`, `docs`, `chore`

Example: `refactor(core): extract RendererCore from Application`
