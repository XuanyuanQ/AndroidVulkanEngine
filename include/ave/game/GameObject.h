#pragma once

#include "ave/project/SceneDocument.h"

#include <string>
#include <vector>

namespace ave::game {

class GameObject {
public:
    explicit GameObject(project::GameObjectData data);

    std::string const& Id() const noexcept;
    std::string const& Name() const noexcept;
    std::string const& Parent() const noexcept;
    project::TransformData const& Transform() const noexcept;
    project::GameObjectData const& Data() const noexcept;

private:
    project::GameObjectData data_;
};

class GameScene {
public:
    void Load(project::SceneDocument document);
    project::SceneDocument const& Document() const noexcept;
    std::vector<GameObject> const& Objects() const noexcept;

private:
    project::SceneDocument document_;
    std::vector<GameObject> objects_;
};

} // namespace ave::game
