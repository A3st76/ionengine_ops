// Copyright © 2020-2022 Dmitriy Lukovenko. All rights reserved.

#include <precompiled.h>
#include <platform/window.h>
#include <platform/window_loop.h>
#include <renderer/renderer.h>
#include <scene/scene.h>
#include <scene/mesh_node.h>
#include <scene/camera_node.h>
#include <lib/exception.h>
#include <lib/algorithm.h>

#include <asset/asset_manager.h>
#include <asset/material.h>

using namespace ionengine;

int main(int* argc, char** agrv) {

    float rotate = 0.0f;

    // lib::ThreadPool thread_pool(3);

    try {
        platform::WindowLoop loop;
        platform::Window window("IONENGINE", 800, 600, false);

        asset::AssetManager asset_manager;

        {
            asset::AssetPtr<asset::Technique> mesh = asset_manager.get_technique("../../data/techniques/geometry.json5");
            
            std::cout << "is pending " << std::boolalpha << mesh.is_pending() << std::endl;

            std::cout << mesh->name() << std::endl;
        }

        renderer::Renderer renderer(window, asset_manager);

        scene::Scene test_scene;
        
        auto node_0 = test_scene.graph().add_node<scene::TransformNode>();
        node_0->position(lib::math::Vector3f(0.0f, 1.0f, 0.0f));

        test_scene.graph().root()->add_child(node_0);

        auto node_1 = test_scene.graph().add_node<scene::MeshNode>();
        node_1->name("mesh");
        node_1->position(lib::math::Vector3f(0.0f, -0.5f, 0.0f));
        node_1->rotation(lib::math::Quaternionf::angle_axis(0.0f, lib::math::Vector3f(0.0f, 1.0f, 0.0f)));
        node_1->scale(lib::math::Vector3f(1.0f, 1.0f, 1.0f));

        node_0->add_child(node_1);

        auto camera = test_scene.graph().add_node<scene::CameraNode>();
        camera->position(lib::math::Vector3f(0.0f, 2.0f, 3.0f));

        loop.run(
            window,
            [&](platform::WindowEvent const& event, platform::WindowEventFlow& flow) {
                flow = platform::WindowEventFlow::Poll;
                switch(event.type) {
                    case platform::WindowEventType::Closed: { flow = platform::WindowEventFlow::Exit; } break;
                    case platform::WindowEventType::Updated: {

                        rotate += 0.5f;
                        if(rotate >= 180.0f) {
                            rotate = 0.0f;
                        }

                        auto mesh = test_scene.graph().find_by_name<scene::MeshNode>("mesh");
                        mesh->rotation(lib::math::Quaternionf::angle_axis(rotate, lib::math::Vector3f(0.0f, 1.0f, 0.0f)));

                        asset_manager.update(0.5f);

                        test_scene.graph().update_hierarchical_data();
                        renderer.render(test_scene);
                    } break;
                    case platform::WindowEventType::Sized: {
                        auto event_size = std::get<platform::Size>(event.data);
                        renderer.resize(event_size.width, event_size.height);
                    } break;
                }
            }
        );

    } catch(lib::Exception& e) {
        std::cerr << std::format("[Exception] {}", e.what()) << std::endl;
        std::exit(EXIT_FAILURE);
    }

    std::cout << "[Debug] Main: Exit" << std::endl;

    //thread_pool.join();
    return EXIT_SUCCESS;
}
