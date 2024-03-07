
#include "core/exception.hpp"
#include "engine/engine.hpp"
#include "platform/window.hpp"
#include "precompiled.h"
#include <base64pp/base64pp.h>
#include <webview.hpp>

using namespace ionengine;

namespace project
{
    class MyEngine : public ionengine::Engine
    {
      public:
        MyEngine(core::ref_ptr<platform::Window> window, libwebview::App& app) : Engine(window)
        {
            app.bind("requestRenderImage", [&](libwebview::EventArgs const& e) {
                auto buffer = base_color->dump();
                std::string const b64image = base64pp::encode(buffer);
                std::string data = std::format("{{\"image\":\"data:image/png;base64,{}\"}}", b64image);
                app.result(e.index, true, data);
            });
        }

      protected:
        auto init() -> void override
        {
            model = asset_loader.load_model("models/cube.glb");
            shader_manager.load_shaders(std::vector<std::filesystem::path>{"shaders/basic.bin"});

            base_color = create_texture(800, 600, TextureFlags::RenderTarget);
        }

        auto update(float const dt) -> void override
        {
        }

        auto render() -> void override
        {
            renderer.begin_draw(std::vector<core::ref_ptr<Texture>>{base_color}, nullptr, {}, 0.0f, 0x0);
            renderer.end_draw();
        }

      private:
        core::ref_ptr<ionengine::Model> model;
        core::ref_ptr<ionengine::Texture> base_color;
    };
} // namespace project

auto main(int32_t argc, char** argv) -> int32_t
{
    try
    {
        libwebview::App app("ionengine", "Shader Graph", 800, 600, true, true);
        auto engine = core::make_ref<project::MyEngine>(nullptr, app);
        engine->run();
        app.idle([&]() { engine->tick(); });
        app.run("resources/index.html");
        return EXIT_SUCCESS;
    }
    catch (core::Exception e)
    {
        std::cout << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}