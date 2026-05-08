#include "ave/game/GameObject.h"

namespace ave::game {

GameObject::GameObject(project::GameObjectData data)
    : data_(std::move(data))
{
}

std::string const& GameObject::Id() const noexcept
{
    return data_.id;
}

std::string const& GameObject::Name() const noexcept
{
    return data_.name;
}

std::string const& GameObject::Parent() const noexcept
{
    return data_.parent;
}

project::TransformData const& GameObject::Transform() const noexcept
{
    return data_.transform;
}

project::GameObjectData const& GameObject::Data() const noexcept
{
    return data_;
}

void GameScene::Load(project::SceneDocument document)
{
    document_ = std::move(document);
    objects_.clear();
    objects_.reserve(document_.objects.size());
    for (auto const& object : document_.objects) {
        objects_.emplace_back(object);
    }
}

project::SceneDocument const& GameScene::Document() const noexcept
{
    return document_;
}

std::vector<GameObject> const& GameScene::Objects() const noexcept
{
    return objects_;
}

} // namespace ave::game
