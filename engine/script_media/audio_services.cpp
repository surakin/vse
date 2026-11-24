#include "script_media/audio_services.h"

namespace vse::script_media
{

void VoiceTable::Add(u64 handle, audio::VoiceId voice, u64 owner_key)
{
    Remove(handle);
    Entry entry;
    entry.voice = voice;
    entry.owner_key = owner_key;
    by_handle_.Insert(handle, entry);
    by_voice_.Insert(Pack(voice), handle);
}

void VoiceTable::AddPlaced(u64 handle, ecs::Entity emitter, u64 owner_key, bool transient)
{
    Remove(handle);
    Entry entry;
    entry.owner_key = owner_key;
    entry.emitter = emitter;
    entry.transient = transient;
    by_handle_.Insert(handle, entry);
}

void VoiceTable::PlacedHandles(Vector<u64>* out) const
{
    out->Clear();
    for (const auto& item : by_handle_)
    {
        if (item.value.emitter.IsValid())
        {
            out->Push(item.key);
        }
    }
    for (usize i = 1; i < out->Size(); ++i)  // ascending: the table's own order is not defined
    {
        const u64 held = (*out)[i];
        usize j = i;
        while (j > 0 && (*out)[j - 1] > held)
        {
            (*out)[j] = (*out)[j - 1];
            --j;
        }
        (*out)[j] = held;
    }
}

bool VoiceTable::MarkStopping(u64 handle)
{
    Entry* entry = by_handle_.Find(handle);
    if (entry == nullptr)
    {
        return false;
    }
    entry->stopping = true;
    return true;
}

u64 VoiceTable::HandleOf(audio::VoiceId voice) const
{
    const u64* handle = by_voice_.Find(Pack(voice));
    return handle != nullptr ? *handle : 0;
}

bool VoiceTable::Remove(u64 handle)
{
    const Entry* entry = by_handle_.Find(handle);
    if (entry == nullptr)
    {
        return false;
    }
    if (entry->voice.IsValid())
    {
        by_voice_.Remove(Pack(entry->voice));
    }
    by_handle_.Remove(handle);
    return true;
}

}  // namespace vse::script_media
