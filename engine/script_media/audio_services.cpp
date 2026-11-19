#include "script_media/audio_services.h"

namespace engine_placeholder::script_media
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
    by_voice_.Remove(Pack(entry->voice));
    by_handle_.Remove(handle);
    return true;
}

}  // namespace engine_placeholder::script_media
