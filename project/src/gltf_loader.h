#ifndef GLTF_LOADER_H
#define GLTF_LOADER_H

#include <memory>
#include <string>

class Scene;

class GLTF_Loader
{
    private:

    GLTF_Loader();
    ~GLTF_Loader();

    GLTF_Loader(const GLTF_Loader&) = delete;
    GLTF_Loader(GLTF_Loader&&) = delete;
    GLTF_Loader& operator=(const GLTF_Loader&) = delete;
    GLTF_Loader& operator=(GLTF_Loader&&) = delete;

    class Impl;
    std::unique_ptr<Impl> m_Impl { };

    public:

    [[nodiscard]] static GLTF_Loader& Get()
    {
        static GLTF_Loader instance { };

        return instance;
    }

    void LoadGltfScene(Scene& scene, const std::string& path);
};

#endif // !GLTF_LOADER_H