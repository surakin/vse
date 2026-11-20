#include "script/script_assets.h"

namespace vse::script
{
namespace
{

String<HeapAllocator> KeyOf(asset::ResourceId id)
{
    char hex[17];
    asset::FormatResourceId(id, hex);
    return String<HeapAllocator>(HeapAllocator::Default(), StringView(hex, 16));
}

}  // namespace

ScriptAssets::ScriptAssets(ScriptWorld* world, asset::ResourceManager* manager) : world_(world), manager_(manager) {}

asset::Status ScriptAssets::Add(asset::ResourceId id, asset::LoadFailure* failure)
{
    for (usize i = 0; i < added_.Size(); ++i)
    {
        if (added_[i] == id)
        {
            return asset::Status::Success();
        }
    }
    // The manager loads the whole closure or fails: nothing is added unless it succeeds.
    asset::Handle<asset::ScriptView> root;
    ENGINE_TRY(manager_->Load<asset::ScriptView>(id, &root, failure));

    // Post-order walk: every script after the scripts it requires.
    struct Frame
    {
        asset::Handle<asset::ScriptView> handle;
        usize next = 0;
    };
    Vector<Frame> stack;
    Vector<asset::Handle<asset::ScriptView>> order;
    Vector<asset::ResourceId> seen;
    {
        Frame f;
        f.handle = root;
        stack.Push(Move(f));
        seen.Push(id);
    }
    while (!stack.Empty())
    {
        Frame& top = stack[stack.Size() - 1];
        const asset::ScriptView* view = top.handle.Get();
        if (top.next < view->DependencyCount())
        {
            const asset::ResourceId dep = view->DependencyAt(top.next++);
            bool known = false;
            for (usize i = 0; i < seen.Size(); ++i)
            {
                known = known || seen[i] == dep;
            }
            for (usize i = 0; i < added_.Size(); ++i)
            {
                known = known || added_[i] == dep;
            }
            if (known)
            {
                continue;
            }
            seen.Push(dep);
            Frame next;
            const asset::Status loaded = manager_->Load<asset::ScriptView>(dep, &next.handle, failure);
            if (loaded.Failed())
            {
                return loaded;
            }
            stack.Push(Move(next));
            continue;
        }
        order.Push(top.handle);
        stack.Pop();
    }

    for (usize i = 0; i < order.Size(); ++i)
    {
        const asset::ScriptView& view = *order[i].Get();
        String<HeapAllocator> deps(HeapAllocator::Default());
        for (usize d = 0; d < view.DependencyCount(); ++d)
        {
            if (d != 0)
            {
                deps.Append('\n');
            }
            deps.Append(KeyOf(view.DependencyAt(d)).View());
        }
        const String<HeapAllocator> key = KeyOf(view.Id());
        world_->AddModule(key.View(), key.View(), view.Bytes(), view.SourceOffset(), view.SourceSize(), deps.View(),
                          true);
        added_.Push(view.Id());
        for (usize e = 0; e < view.EntryCount(); ++e)
        {
            Entry entry;
            entry.id = view.Id();
            entry.name = String<HeapAllocator>(HeapAllocator::Default(), view.Entry(e));
            behaviors_.Push(Move(entry));
        }
        held_.Push(order[i]);
    }
    return asset::Status::Success();
}

asset::ResourceId ScriptAssets::ScriptOf(StringView name) const
{
    for (usize i = 0; i < behaviors_.Size(); ++i)
    {
        if (behaviors_[i].name.View() == name)
        {
            return behaviors_[i].id;
        }
    }
    return asset::ResourceId::None();
}

void ScriptAssets::Release()
{
    held_.Clear();
}

}  // namespace vse::script
